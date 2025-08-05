#pragma once

#include <string>
#include <vector>
#include <format>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include "debug.hpp"
#include "cppgpt.hpp"

namespace rouen::helpers {

    /**
     * Gemini API Adapter
     * Template-compatible adapter for Google's Gemini API
     * Provides the same interface as cppgpt for seamless template usage
     */
    class GeminiAdapter {
    public:
        struct Message {
            std::string role;
            std::string content;
        };

        // Use the same types as cppgpt for perfect compatibility
        using ChatCompletion = ignacionr::ChatCompletion;
        using ChatCompletionChoice = ignacionr::ChatCompletionChoice;
        using ChatCompletionMessage = ignacionr::ChatCompletionMessage;

    private:
        std::string api_key_;
        std::string model_;
        std::vector<Message> conversation_;
        std::chrono::steady_clock::time_point last_request_time_;
        static constexpr auto min_request_interval_ = std::chrono::milliseconds(100);

        void wait_min_time() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - last_request_time_;
            if (elapsed < min_request_interval_) {
                std::this_thread::sleep_for(min_request_interval_ - elapsed);
            }
            last_request_time_ = std::chrono::steady_clock::now();
        }

        std::string escape_json(const std::string& str) const {
            std::string escaped;
            escaped.reserve(str.size() + str.size() / 10 + 1); // Reserve extra space for escaping
            
            for (char c : str) {
                switch (c) {
                    case '"': escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\t': escaped += "\\t"; break;
                    default: escaped += c; break;
                }
            }
            return escaped;
        }

        std::string build_gemini_request(float temperature) const {
            std::string json = "{\"contents\":[";
            
            // Merge system messages and convert to Gemini format
            std::string system_instructions;
            std::vector<Message> user_assistant_messages;
            
            for (const auto& msg : conversation_) {
                if (msg.role == "system") {
                    if (!system_instructions.empty()) {
                        system_instructions += "\n\n";
                    }
                    system_instructions += msg.content;
                } else {
                    user_assistant_messages.push_back(msg);
                }
            }
            
            // Add system instructions to first user message if present
            if (!system_instructions.empty() && !user_assistant_messages.empty()) {
                if (user_assistant_messages[0].role == "user") {
                    user_assistant_messages[0].content = system_instructions + "\n\n" + user_assistant_messages[0].content;
                }
            }
            
            // Build contents array
            for (size_t i = 0; i < user_assistant_messages.size(); ++i) {
                if (i > 0) json += ",";
                const auto& msg = user_assistant_messages[i];
                
                std::string gemini_role = (msg.role == "assistant") ? "model" : "user";
                json += std::format("{{\"role\":\"{}\",\"parts\":[{{\"text\":\"{}\"}}]}}", 
                                   gemini_role, escape_json(msg.content));
            }
            
            json += std::format("],\"generationConfig\":{{\"temperature\":{},\"maxOutputTokens\":4096}}}}", temperature);
            
            return json;
        }

        std::string parse_gemini_response(const std::string& response) const {
            CONFIG_DEBUG_FMT("Parsing Gemini response: {}", response.substr(0, 200) + "...");
            
            // Simple JSON parser for Gemini response
            std::string text_key = "\"text\":\"";
            auto text_pos = response.find(text_key);
            if (text_pos == std::string::npos) {
                CONFIG_ERROR("Failed to find text content in Gemini response");
                throw std::runtime_error("Invalid Gemini response format");
            }
            
            text_pos += text_key.length();
            auto end_pos = text_pos;
            
            // Find the end of the text content (handling escaped quotes)
            while (end_pos < response.length()) {
                if (response[end_pos] == '"' && (end_pos == text_pos || response[end_pos - 1] != '\\')) {
                    break;
                }
                end_pos++;
            }
            
            if (end_pos >= response.length()) {
                CONFIG_ERROR("Malformed JSON in Gemini response");
                throw std::runtime_error("Malformed Gemini response");
            }
            
            std::string content = response.substr(text_pos, end_pos - text_pos);
            
            // Unescape JSON
            std::string unescaped;
            for (size_t i = 0; i < content.length(); ++i) {
                if (content[i] == '\\' && i + 1 < content.length()) {
                    switch (content[i + 1]) {
                        case '"': unescaped += '"'; i++; break;
                        case '\\': unescaped += '\\'; i++; break;
                        case 'n': unescaped += '\n'; i++; break;
                        case 't': unescaped += '\t'; i++; break;
                        case 'r': unescaped += '\r'; i++; break;
                        default: unescaped += content[i]; break;
                    }
                } else {
                    unescaped += content[i];
                }
            }
            
            CONFIG_DEBUG_FMT("Extracted Gemini response: {}", unescaped.substr(0, 100) + "...");
            return unescaped;
        }

    public:
        GeminiAdapter(const std::string& api_key, [[maybe_unused]] const std::string& base_url = "") 
            : api_key_(api_key), model_("gemini-1.5-pro"), last_request_time_(std::chrono::steady_clock::now()) {
            CONFIG_DEBUG_FMT("Created Gemini adapter with API key: {}...", api_key.substr(0, 8));
        }

        GeminiAdapter new_conversation() const {
            return GeminiAdapter(api_key_);
        }

        void add_instructions(std::string_view instructions, std::string_view role = "system") {
            conversation_.push_back({std::string(role), std::string(instructions)});
        }

        // Template-compatible sendMessage method that returns a cppgpt-compatible response structure
        template<typename DoPostFunc>
        ChatCompletion sendMessage(
            std::string_view message, 
            DoPostFunc do_post, 
            std::string_view role = "user", 
            std::string_view model = "gemini-1.5-pro", 
            [[maybe_unused]] std::string_view search_mode = {},
            float temperature = 0.45f
        ) {
            wait_min_time();
            
            // Add the new message to conversation
            conversation_.push_back({std::string(role), std::string(message)});

            // Build Gemini API request
            std::string request_body = build_gemini_request(temperature);

            // Use the provided model or default
            std::string model_name = model.empty() ? model_ : std::string(model);
            
            // Build URL for Gemini API
            auto url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                 model_name, api_key_);

            CONFIG_DEBUG_FMT("Sending Gemini request to: {}", url);

            // Make the HTTP request
            auto response = do_post(url, request_body, [](auto header_setter) {
                header_setter("Content-Type: application/json");
            });

            // Parse response and extract content
            std::string result = parse_gemini_response(response);
            
            // Add assistant response to conversation
            conversation_.push_back({"assistant", result});
            
            // Return cppgpt-compatible response structure
            ChatCompletion chat_completion;
            chat_completion.choices.resize(1);
            chat_completion.choices[0].message.content = result;
            
            return chat_completion;
        }
    };

} // namespace rouen::helpers
