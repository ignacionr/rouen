#pragma once

#include <string>
#include <vector>
#include <optional>
#include <format>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include "debug.hpp"
#include "cppgpt.hpp"
#include "glaze_include.hpp"

namespace rouen::helpers {

    // Gemini API response structures for glaze parsing
    // Note: Using glz::opts{.error_on_unknown_keys=false} so we only need to define fields we use
    struct GeminiPart {
        std::string text;
    };

    struct GeminiContent {
        std::vector<GeminiPart> parts;
        std::string role;
    };

    struct GeminiCandidate {
        GeminiContent content;
        std::string finishReason;
        int index;
    };

    struct GeminiResponse {
        std::vector<GeminiCandidate> candidates;
        // Don't include optional fields - we'll handle unknown_key errors differently
    };

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

        std::string build_gemini_request(const std::vector<Message>& conversation, float temperature) const {
            std::string json = "{\"contents\":[";
            
            // Merge system messages and convert to Gemini format
            std::string system_instructions;
            std::vector<Message> user_assistant_messages;
            
            for (const auto& msg : conversation) {
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

        // Backward compatibility method that uses local conversation_
        std::string build_gemini_request(float temperature) const {
            return build_gemini_request(conversation_, temperature);
        }

        std::string parse_gemini_response(const std::string& response) const {
            CONFIG_DEBUG_FMT("Parsing Gemini response: {}", response);
            
            try {
                CONFIG_DEBUG("Creating GeminiResponse object");
                GeminiResponse gemini_response;
                
                // Simple workaround: manually clean JSON to only keep candidates
                std::string cleaned_response = "{\"candidates\":";
                
                // Find the candidates array
                auto candidates_start = response.find("\"candidates\":");
                if (candidates_start == std::string::npos) {
                    throw std::runtime_error("No candidates found in response");
                }
                
                // Find the start of the array
                auto array_start = response.find('[', candidates_start);
                if (array_start == std::string::npos) {
                    throw std::runtime_error("Malformed candidates array");
                }
                
                // Find the end of the candidates array
                auto array_end = array_start + 1;
                int bracket_count = 1;
                bool in_string = false;
                
                for (size_t i = array_start + 1; i < response.length() && bracket_count > 0; ++i) {
                    char c = response[i];
                    if (c == '"' && (i == 0 || response[i-1] != '\\')) {
                        in_string = !in_string;
                    } else if (!in_string) {
                        if (c == '[') {
                            bracket_count++;
                        } else if (c == ']') {
                            bracket_count--;
                            if (bracket_count == 0) {
                                array_end = i + 1;
                                break;
                            }
                        }
                    }
                }
                
                // Extract just the candidates array
                std::string candidates_array = response.substr(array_start, array_end - array_start);
                cleaned_response += candidates_array + "}";
                
                CONFIG_DEBUG_FMT("Cleaned JSON: {}", cleaned_response);
                
                CONFIG_DEBUG("Calling glz::read_json on cleaned response");
                auto error = glz::read_json(gemini_response, cleaned_response);
                
                if (error) {
                    CONFIG_ERROR_FMT("Failed to parse cleaned Gemini JSON response: {}", glz::format_error(error, cleaned_response));
                    throw std::runtime_error("Invalid Gemini response JSON format");
                }
                
                CONFIG_DEBUG("Checking candidates");
                if (gemini_response.candidates.empty()) {
                    CONFIG_ERROR("No candidates found in Gemini response");
                    throw std::runtime_error("Gemini response contains no candidates");
                }
                
                CONFIG_DEBUG("Getting candidate reference");
                const auto& candidate = gemini_response.candidates[0];
                
                CONFIG_DEBUG("Checking parts");
                if (candidate.content.parts.empty()) {
                    CONFIG_ERROR("No parts found in Gemini candidate content");
                    throw std::runtime_error("Gemini candidate contains no content parts");
                }
                
                CONFIG_DEBUG("Extracting text");
                // Make a copy of the text to avoid dangling reference when gemini_response goes out of scope
                std::string text = candidate.content.parts[0].text;
                
                CONFIG_DEBUG_FMT("Successfully extracted text: {}", text.substr(0, 50) + "...");
                CONFIG_DEBUG_FMT("Text length: {}", text.length());
                
                CONFIG_DEBUG("About to return text");
                return text;
                
            } catch (const std::exception& e) {
                CONFIG_ERROR_FMT("Exception parsing Gemini response: {}", e.what());
                CONFIG_DEBUG_FMT("Full response for debugging: {}", response);
                throw;
            }
        }

    public:
        GeminiAdapter(const std::string& api_key, [[maybe_unused]] const std::string& base_url = "") 
            : api_key_(api_key), model_("gemini-2.5-flash-lite"), last_request_time_(std::chrono::steady_clock::now()) {
            CONFIG_DEBUG_FMT("Created Gemini adapter with API key: {}...", api_key.substr(0, 8));
        }

        GeminiAdapter new_conversation() const {
            return GeminiAdapter(api_key_);
        }

        void add_instructions(std::string_view instructions, std::string_view role = "system") {
            conversation_.push_back({std::string(role), std::string(instructions)});
        }
        
        void clear() {
            conversation_.clear();
        }

        // Template-compatible sendMessage method that returns a cppgpt-compatible response structure
        template<typename DoPostFunc>
        ChatCompletion sendMessage(
            std::string_view message, 
            DoPostFunc do_post, 
            std::string_view role = "user", 
            std::string_view model = "gemini-2.5-flash-lite", 
            [[maybe_unused]] std::string_view search_mode = {},
            float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr
        ) {
            wait_min_time();
            
            // Build conversation either from full_conversation or local history
            std::vector<Message> current_conversation;
            
            if (full_conversation) {
                // Use the provided full conversation history
                current_conversation.reserve(full_conversation->size() + 1);
                for (const auto& [msg_role, msg_content] : *full_conversation) {
                    current_conversation.emplace_back(msg_role, msg_content);
                }
                // Add the new message
                current_conversation.emplace_back(std::string(role), std::string(message));
            } else {
                // Fallback to local conversation + new message
                current_conversation = conversation_;
                current_conversation.emplace_back(std::string(role), std::string(message));
            }

            // Build Gemini API request using current_conversation
            std::string request_body = build_gemini_request(current_conversation, temperature);

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
            
            // Only add to local conversation if not using external conversation management
            if (!full_conversation) {
                conversation_.push_back({"assistant", result});
            }
            
            // Return cppgpt-compatible response structure
            ChatCompletion chat_completion{};
            chat_completion.choices.resize(1);
            chat_completion.choices[0].index = 0;
            chat_completion.choices[0].message.role = "assistant";
            chat_completion.choices[0].message.content = result;
            chat_completion.choices[0].finish_reason = "stop";
            
            return chat_completion;
        }
    };

} // namespace rouen::helpers

// Glaze metadata for Gemini API response structures
template <>
struct glz::meta<rouen::helpers::GeminiPart> {
    using T = rouen::helpers::GeminiPart;
    static constexpr auto value = object(
        "text", &T::text
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiContent> {
    using T = rouen::helpers::GeminiContent;
    static constexpr auto value = object(
        "parts", &T::parts,
        "role", &T::role
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiCandidate> {
    using T = rouen::helpers::GeminiCandidate;
    static constexpr auto value = object(
        "content", &T::content,
        "finishReason", &T::finishReason,
        "index", &T::index
    );
};

template <>
struct glz::meta<rouen::helpers::GeminiResponse> {
    using T = rouen::helpers::GeminiResponse;
    static constexpr auto value = object(
        "candidates", &T::candidates
    );
};
