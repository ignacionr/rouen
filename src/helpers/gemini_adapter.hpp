#pragma once

#include <string>
#include <vector>
#include <optional>
#include <map>
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
    struct GeminiFunctionCall {
        std::string name;
        glz::json_t args; // Use glz::json_t to dynamically parse any JSON structure
    };

    struct GeminiPart {
        std::string text;
        std::optional<GeminiFunctionCall> functionCall;
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
            struct FunctionCall {
                std::string name;
                std::string args; // JSON string of arguments
            };

            struct FunctionResponse {
                std::string name;
                std::string response; // JSON string of response
            };

            std::string role;
            std::string content;
            std::vector<FunctionCall> function_calls{};
            std::vector<FunctionResponse> function_responses{};

            // Default constructor
            Message() = default;

            // Constructor for standard text messages
            Message(std::string r, std::string c)
                : role(std::move(r)), content(std::move(c)) {}
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

        std::string build_gemini_request(const std::vector<Message>& conversation, float temperature, bool enable_search = false) const {
            return build_gemini_request(conversation, temperature, {}, enable_search);
        }

        // Enhanced method with function calling support
        std::string build_gemini_request(const std::vector<Message>& conversation, float temperature, const std::vector<std::string>& function_schemas, bool enable_search = false) const {
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
                
                std::string gemini_role;
                if (msg.role == "assistant" || msg.role == "model") {
                    gemini_role = "model";
                } else {
                    gemini_role = "user";
                }
                
                json += std::format("{{\"role\":\"{}\",\"parts\":[", gemini_role);
                
                bool first_part = true;
                if (!msg.content.empty() || (msg.function_calls.empty() && msg.function_responses.empty())) {
                    json += std::format("{{\"text\":\"{}\"}}", escape_json(msg.content));
                    first_part = false;
                }
                
                // Add function calls
                for (const auto& fc : msg.function_calls) {
                    if (!first_part) json += ",";
                    std::string args_json = fc.args.empty() ? "{}" : fc.args;
                    json += std::format("{{\"functionCall\":{{\"name\":\"{}\",\"args\":{}}}}}", 
                                       fc.name, args_json);
                    first_part = false;
                }
                
                // Add function responses
                for (const auto& fr : msg.function_responses) {
                    if (!first_part) json += ",";
                    std::string resp_json;
                    if (!fr.response.empty() && fr.response.front() == '{') {
                        resp_json = std::format("{{\"name\":\"{}\",\"response\":{}}}", fr.name, fr.response);
                    } else {
                        resp_json = std::format("{{\"name\":\"{}\",\"response\":{{\"result\":\"{}\"}}}}", 
                                               fr.name, escape_json(fr.response));
                    }
                    json += std::format("{{\"functionResponse\":{}}}", resp_json);
                    first_part = false;
                }
                
                json += "]}";
            }
            
            json += std::format("],\"generationConfig\":{{\"temperature\":{},\"maxOutputTokens\":4096}}", temperature);
            
            // Add tools if search or function calling is enabled
            if (enable_search || !function_schemas.empty()) {
                json += ",\"tools\":[";
                bool has_previous_tool = false;
                
                if (enable_search) {
                    json += "{\"google_search\":{}}";
                    has_previous_tool = true;
                }
                
                if (!function_schemas.empty()) {
                    if (has_previous_tool) json += ",";
                    json += "{\"functionDeclarations\":[";
                    for (size_t i = 0; i < function_schemas.size(); ++i) {
                        if (i > 0) json += ",";
                        json += function_schemas[i];
                    }
                    json += "]}";
                }
                
                json += "]";
            }
            
            json += "}";  // Close the main JSON object
            
            CONFIG_DEBUG_FMT("Built Gemini JSON request: {}", json);
            return json;
        }

        // Backward compatibility method that uses local conversation_
        std::string build_gemini_request(float temperature, bool enable_search = false) const {
            return build_gemini_request(conversation_, temperature, enable_search);
        }

        // Parse Gemini response and return the full response structure for function call handling
        GeminiResponse parse_gemini_response_full(const std::string& response) const {
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
                
                return gemini_response;
                
            } catch (const std::exception& e) {
                CONFIG_ERROR_FMT("Exception parsing Gemini response: {}", e.what());
                CONFIG_DEBUG_FMT("Full response for debugging: {}", response);
                throw;
            }
        }

        std::string parse_gemini_response(const std::string& response) const {
            auto gemini_response = parse_gemini_response_full(response);
            
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
        }

        static std::string trim(std::string_view str) {
            auto first = str.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return "";
            auto last = str.find_last_not_of(" \t\r\n");
            return std::string(str.substr(first, last - first + 1));
        }

    public:
        GeminiAdapter(const std::string& api_key, [[maybe_unused]] const std::string& base_url = "") 
            : api_key_(trim(api_key)), model_("gemini-2.5-flash-lite"), last_request_time_(std::chrono::steady_clock::now()) {
            CONFIG_DEBUG_FMT("Created Gemini adapter with API key: {}...", api_key_.empty() ? "" : api_key_.substr(0, std::min(size_t(8), api_key_.length())));
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
            std::string_view search_mode = {},
            float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr,
            const std::vector<std::string>* function_schemas = nullptr
        ) {
            wait_min_time();
            
            // Build conversation either from full_conversation or local history
            std::vector<Message> current_conversation;
            
            if (full_conversation) {
                // Use the provided full conversation history
                current_conversation.reserve(conversation_.size() + full_conversation->size() + 1);
                // Copy any system instructions from the local conversation_
                for (const auto& local_msg : conversation_) {
                    if (local_msg.role == "system") {
                        current_conversation.push_back(local_msg);
                    }
                }
                for (const auto& [msg_role, msg_content] : *full_conversation) {
                    current_conversation.emplace_back(msg_role, msg_content);
                }
                // Add the new message only if it's not already the last message in the history
                if (current_conversation.empty() || current_conversation.back().content != message) {
                    current_conversation.emplace_back(std::string(role), std::string(message));
                }
            } else {
                // Fallback to local conversation + new message
                current_conversation = conversation_;
                current_conversation.emplace_back(std::string(role), std::string(message));
            }

            bool enable_search = (search_mode == "on");
            // Build Gemini API request using current_conversation and function schemas
            std::string request_body = function_schemas ? 
                build_gemini_request(current_conversation, temperature, *function_schemas, enable_search) :
                build_gemini_request(current_conversation, temperature, enable_search);

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

        // Extended sendMessage that handles function calling responses
        template<typename DoPostFunc>
        ChatCompletion sendMessageWithFunctionCalling(
            std::string_view message, 
            DoPostFunc do_post, 
            std::function<std::string(const std::string&, const std::string&)> function_executor,
            std::string_view role = "user", 
            std::string_view model = "gemini-2.5-flash-lite", 
            std::string_view search_mode = {},
            float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr,
            const std::vector<std::string>* function_schemas = nullptr
        ) {
            wait_min_time();
            
            // Build conversation either from full_conversation or local history
            std::vector<Message> current_conversation;
            
            if (full_conversation) {
                // Use the provided full conversation history
                current_conversation.reserve(conversation_.size() + full_conversation->size() + 1);
                // Copy any system instructions from the local conversation_
                for (const auto& local_msg : conversation_) {
                    if (local_msg.role == "system") {
                        current_conversation.push_back(local_msg);
                    }
                }
                for (const auto& [msg_role, msg_content] : *full_conversation) {
                    current_conversation.emplace_back(msg_role, msg_content);
                }
                // Add the new message only if it's not already the last message in the history
                if (current_conversation.empty() || current_conversation.back().content != message) {
                    current_conversation.emplace_back(std::string(role), std::string(message));
                }
            } else {
                // Fallback to local conversation + new message
                current_conversation = conversation_;
                current_conversation.emplace_back(std::string(role), std::string(message));
            }

            bool enable_search = (search_mode == "on");
            
            // Use the provided model or default
            std::string model_name = model.empty() ? model_ : std::string(model);
            
            // Build URL for Gemini API
            auto url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}", 
                                 model_name, api_key_);

            std::string final_text;
            bool keep_calling = true;
            int iterations = 0;
            const int max_iterations = 5; // Prevent infinite loops
            
            while (keep_calling && iterations < max_iterations) {
                iterations++;
                
                // Build Gemini API request using current_conversation and function schemas
                std::string request_body = function_schemas ? 
                    build_gemini_request(current_conversation, temperature, *function_schemas, enable_search) :
                    build_gemini_request(current_conversation, temperature, enable_search);

                CONFIG_DEBUG_FMT("Sending Gemini request (iteration {}) to: {}", iterations, url);

                // Make the HTTP request
                auto response = do_post(url, request_body, [](auto header_setter) {
                    header_setter("Content-Type: application/json");
                });

                // Parse response and check for function calls
                auto gemini_response = parse_gemini_response_full(response);
                
                if (gemini_response.candidates.empty()) {
                    throw std::runtime_error("Gemini response contains no candidates");
                }
                
                const auto& candidate = gemini_response.candidates[0];
                
                // Check if candidate content has function calls
                std::vector<Message::FunctionCall> current_calls;
                std::string turn_text;
                
                for (const auto& part : candidate.content.parts) {
                    if (part.functionCall.has_value()) {
                        const auto& fc = part.functionCall.value();
                        // Serialize arguments to JSON string
                        std::string args_json;
                        auto write_err = glz::write_json(fc.args, args_json);
                        if (write_err) {
                            CONFIG_ERROR_FMT("Failed to serialize function arguments: {}", glz::format_error(write_err));
                            args_json = "{}";
                        }
                        
                        current_calls.push_back({fc.name, args_json});
                    }
                    if (!part.text.empty()) {
                        turn_text += part.text;
                    }
                }
                
                if (!current_calls.empty()) {
                    // 1. Add model turn containing functionCall to history
                    Message model_msg;
                    model_msg.role = "model";
                    model_msg.content = turn_text;
                    model_msg.function_calls = current_calls;
                    current_conversation.push_back(model_msg);
                    
                    // 2. Execute each function call and collect responses
                    Message response_msg;
                    response_msg.role = "function"; // Maps to role: "user" in build_gemini_request
                    
                    for (const auto& call : current_calls) {
                        CONFIG_DEBUG_FMT("Executing function: {} with args: {}", call.name, call.args);
                        std::string result;
                        try {
                            result = function_executor(call.name, call.args);
                        } catch (const std::exception& e) {
                            result = std::format("{{\"error\":\"{}\"}}", e.what());
                        }
                        response_msg.function_responses.push_back({call.name, result});
                    }
                    
                    // 3. Add function response turn to history
                    current_conversation.push_back(response_msg);
                    
                    // Continue the loop to send the response back to Gemini
                    keep_calling = true;
                } else {
                    // No more function calls, we have the final text!
                    final_text = turn_text;
                    keep_calling = false;
                }
            }
            
            // Only add to local conversation if not using external conversation management
            if (!full_conversation) {
                conversation_.push_back({"assistant", final_text});
            }
            
            // Return cppgpt-compatible response structure
            ChatCompletion chat_completion{};
            chat_completion.choices.resize(1);
            chat_completion.choices[0].index = 0;
            chat_completion.choices[0].message.role = "assistant";
            chat_completion.choices[0].message.content = final_text;
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
        "text", &T::text,
        "functionCall", &T::functionCall
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
struct glz::meta<rouen::helpers::GeminiFunctionCall> {
    using T = rouen::helpers::GeminiFunctionCall;
    static constexpr auto value = object(
        "name", &T::name,
        "args", &T::args
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
