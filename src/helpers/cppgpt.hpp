#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <string_view>

#include "glaze_include.hpp"

namespace ignacionr
{
    struct Message {
        std::string role;
        std::string content;
    };
    
    // Forward declarations for the nested structures
    struct ChatCompletionMessage {
        std::string role;
        std::string content;
        std::nullptr_t refusal = nullptr;
    };
    
    struct ChatCompletionChoice {
        int index;
        ChatCompletionMessage message;
        std::string finish_reason;
    };
    
    struct TokensDetails {
        int text_tokens = 0;
        int audio_tokens = 0;
        int image_tokens = 0;
        int cached_tokens = 0;
        int reasoning_tokens = 0;
        int accepted_prediction_tokens = 0;
        int rejected_prediction_tokens = 0;
    };
    
    struct UsageInfo {
        int prompt_tokens;
        int completion_tokens;
        int total_tokens;
        TokensDetails prompt_tokens_details;
        TokensDetails completion_tokens_details;
    };
    
    struct ChatCompletion {
        std::string id;
        std::string object;
        long created;
        std::string model;
        std::vector<ChatCompletionChoice> choices;
        UsageInfo usage;
        std::string system_fingerprint;
    };

    struct SearchParameters {
        std::string mode {"auto"};
    };
    
    struct Payload {
        std::string model;
        std::vector<Message> messages;
        std::optional<SearchParameters> search_parameters;
        float temperature;
    };

    struct ResponsesTool {
        std::string type;
    };

    struct ResponsesPayload {
        std::string model;
        std::vector<Message> input;
        std::vector<ResponsesTool> tools;
    };

    struct ResponseContent {
        std::string type;
        std::string text;
    };

    struct ResponseOutputItem {
        std::string type;
        std::string role;
        std::vector<ResponseContent> content;
    };

    struct ResponsesApiResponse {
        std::vector<ResponseOutputItem> output;
    };

    struct OpenAIToolCallFunction {
        std::string name;
        std::string arguments;
    };
    
    struct OpenAIToolCall {
        std::string id;
        std::string type;
        OpenAIToolCallFunction function;
    };
    
    struct OpenAIChatCompletionMessage {
        std::string role;
        std::optional<std::string> content;
        std::vector<OpenAIToolCall> tool_calls;
    };
    
    struct OpenAIChatCompletionChoice {
        int index;
        OpenAIChatCompletionMessage message;
        std::optional<std::string> finish_reason;
    };
    
    struct OpenAIChatCompletion {
        std::string id;
        std::string object;
        long created;
        std::string model;
        std::vector<OpenAIChatCompletionChoice> choices;
    };
    
    struct OpenAIMessageToolCallFunction {
        std::string name;
        std::string arguments;
    };
    
    struct OpenAIMessageToolCall {
        std::string id;
        std::string type{"function"};
        OpenAIMessageToolCallFunction function;
    };
    
    struct OpenAIMessage {
        std::string role;
        std::string content;
        std::string name;
        std::string tool_call_id;
        std::vector<OpenAIMessageToolCall> tool_calls;
    };
}

// Define glaze schema for all structures
template <>
struct glz::meta<ignacionr::Message> {
    using T = ignacionr::Message;
    static constexpr auto value = object(
        "role", &T::role,
        "content", &T::content
    );
};

template <>
struct glz::meta<ignacionr::ChatCompletionMessage> {
    using T = ignacionr::ChatCompletionMessage;
    static constexpr auto value = object(
        "role", &T::role,
        "content", &T::content,
        "refusal", &T::refusal
    );
};

template <>
struct glz::meta<ignacionr::ChatCompletionChoice> {
    using T = ignacionr::ChatCompletionChoice;
    static constexpr auto value = object(
        "index", &T::index,
        "message", &T::message,
        "finish_reason", &T::finish_reason
    );
};

template <>
struct glz::meta<ignacionr::TokensDetails> {
    using T = ignacionr::TokensDetails;
    static constexpr auto value = object(
        "text_tokens", &T::text_tokens,
        "audio_tokens", &T::audio_tokens,
        "image_tokens", &T::image_tokens,
        "cached_tokens", &T::cached_tokens,
        "reasoning_tokens", &T::reasoning_tokens,
        "accepted_prediction_tokens", &T::accepted_prediction_tokens,
        "rejected_prediction_tokens", &T::rejected_prediction_tokens
    );
};

template <>
struct glz::meta<ignacionr::UsageInfo> {
    using T = ignacionr::UsageInfo;
    static constexpr auto value = object(
        "prompt_tokens", &T::prompt_tokens,
        "completion_tokens", &T::completion_tokens,
        "total_tokens", &T::total_tokens,
        "prompt_tokens_details", &T::prompt_tokens_details,
        "completion_tokens_details", &T::completion_tokens_details
    );
};

template <>
struct glz::meta<ignacionr::ChatCompletion> {
    using T = ignacionr::ChatCompletion;
    static constexpr auto value = object(
        "id", &T::id,
        "object", &T::object,
        "created", &T::created,
        "model", &T::model,
        "choices", &T::choices,
        "usage", &T::usage,
        "system_fingerprint", &T::system_fingerprint
    );
};

template <>
struct glz::meta<ignacionr::Payload> {
    using T = ignacionr::Payload;
    static constexpr auto value = object(
        "model", &T::model,
        "messages", &T::messages,
        "search_parameters", &T::search_parameters,
        "temperature", &T::temperature
    );
};

template <>
struct glz::meta<ignacionr::ResponsesTool> {
    using T = ignacionr::ResponsesTool;
    static constexpr auto value = object(
        "type", &T::type
    );
};

template <>
struct glz::meta<ignacionr::ResponsesPayload> {
    using T = ignacionr::ResponsesPayload;
    static constexpr auto value = object(
        "model", &T::model,
        "input", &T::input,
        "tools", &T::tools
    );
};

template <>
struct glz::meta<ignacionr::ResponseContent> {
    using T = ignacionr::ResponseContent;
    static constexpr auto value = object(
        "type", &T::type,
        "text", &T::text
    );
};

template <>
struct glz::meta<ignacionr::ResponseOutputItem> {
    using T = ignacionr::ResponseOutputItem;
    static constexpr auto value = object(
        "type", &T::type,
        "role", &T::role,
        "content", &T::content
    );
};

template <>
struct glz::meta<ignacionr::OpenAIToolCallFunction> {
    using T = ignacionr::OpenAIToolCallFunction;
    static constexpr auto value = object(
        "name", &T::name,
        "arguments", &T::arguments
    );
};

template <>
struct glz::meta<ignacionr::OpenAIToolCall> {
    using T = ignacionr::OpenAIToolCall;
    static constexpr auto value = object(
        "id", &T::id,
        "type", &T::type,
        "function", &T::function
    );
};

template <>
struct glz::meta<ignacionr::OpenAIChatCompletionMessage> {
    using T = ignacionr::OpenAIChatCompletionMessage;
    static constexpr auto value = object(
        "role", &T::role,
        "content", &T::content,
        "tool_calls", &T::tool_calls
    );
};

template <>
struct glz::meta<ignacionr::OpenAIChatCompletionChoice> {
    using T = ignacionr::OpenAIChatCompletionChoice;
    static constexpr auto value = object(
        "index", &T::index,
        "message", &T::message,
        "finish_reason", &T::finish_reason
    );
};

template <>
struct glz::meta<ignacionr::OpenAIChatCompletion> {
    using T = ignacionr::OpenAIChatCompletion;
    static constexpr auto value = object(
        "id", &T::id,
        "object", &T::object,
        "created", &T::created,
        "model", &T::model,
        "choices", &T::choices
    );
};

template <>
struct glz::meta<ignacionr::ResponsesApiResponse> {
    using T = ignacionr::ResponsesApiResponse;
    static constexpr auto value = object(
        "output", &T::output
    );
};

namespace ignacionr
{
    class cppgpt
    {
    public:
        static constexpr auto open_ai_base = "https://api.openai.com/v1/";
        static constexpr auto groq_base = "https://api.groq.com/openai/v1";
        static constexpr auto grok_base = "https://api.x.ai/v1";
        cppgpt(const std::string &api_key, const std::string &base_url) : api_key_(api_key), base_url_{base_url} {}

        cppgpt new_conversation() {
            return cppgpt(api_key_, base_url_);
        }

        void add_instructions(std::string_view instructions, std::string_view role = "system")
        {
            Message instruction_message;
            instruction_message.role = std::string(role);
            instruction_message.content = std::string(instructions);
            conversation.push_back(instruction_message);  // copy instead of move
        }

        // Function to send a message to GPT and receive the reply
        auto sendMessage(
            std::string_view message, 
            auto do_post, 
            std::string_view role = "user", 
            std::string_view model = "grok-3-latest", 
            std::string_view search_mode = {},
            float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr
        )
        {
            wait_min_time();
            
            // Build conversation history either from full_conversation or our local history
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
                // Fallback to local conversation history + new message
                current_conversation = conversation;
                current_conversation.emplace_back(std::string(role), std::string(message));
            }

            // Check if we should use the xAI Responses API for web search
            if (base_url_.find("api.x.ai") != std::string::npos && search_mode == "on") {
                // Prepare the Responses API payload
                ResponsesPayload responses_payload{
                    std::string(model),
                    std::move(current_conversation),
                    {ResponsesTool{"web_search"}}
                };
                
                auto url = std::format("{}/responses", base_url_);
                
                std::string body;
                auto result = glz::write_json(responses_payload, body);
                if (result) {
                    throw std::runtime_error("Failed to serialize xAI Responses request payload");
                }
                
                auto r = do_post(url, body, [this](auto header_setter){
                    header_setter("Authorization: Bearer " + api_key_);
                    header_setter("Content-Type: application/json");
                });
                
                // Parse the Responses API response
                ResponsesApiResponse responses_api_response;
                auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(responses_api_response, r);
                if (read_error) {
                    std::cerr << "Error reading Responses response: " << glz::format_error(read_error, r) << '\n';
                    std::cerr << "Response: " << r << '\n';
                    throw std::runtime_error("Failed to parse Responses API response: " + glz::format_error(read_error, r));
                }
                
                // Extract assistant message
                std::string reply = "Error: No response generated from search";
                for (const auto& item : responses_api_response.output) {
                    if (item.type == "message" && item.role == "assistant" && !item.content.empty()) {
                        reply = item.content[0].text;
                        break;
                    }
                }
                
                // Only maintain local conversation if not using external conversation management
                if (!full_conversation) {
                    conversation.push_back({"assistant", reply});
                }
                
                // Build a ChatCompletion response compatible with the caller
                ChatCompletion response;
                response.model = std::string(model);
                response.choices.resize(1);
                response.choices[0].index = 0;
                response.choices[0].message.role = "assistant";
                response.choices[0].message.content = reply;
                response.choices[0].finish_reason = "stop";
                
                return response;
            }

            // Prepare the API request payload
            Payload payload{
                std::string(model),
                std::move(current_conversation),
                search_mode.empty() ? std::optional<SearchParameters>{} : SearchParameters{std::string(search_mode)},
                temperature
            };

            // Send the API request
            auto url = std::format("{}/chat/completions", base_url_);
            
            // Debug logging to check URL construction - need to include debug.hpp first
            // For now, let's ensure we have a valid base URL
            if (base_url_.empty()) {
                throw std::runtime_error("Base URL is empty in cppgpt instance");
            }
            
            std::string body;
            auto result = glz::write_json(payload, body);
            if (result) {
                throw std::runtime_error("Failed to serialize ChatGPT request payload");
            }
            
            auto r = do_post(url, body, [this](auto header_setter){
                header_setter("Authorization: Bearer " + api_key_);
                header_setter("Content-Type: application/json");
            });

            // Parse the API response
            ChatCompletion response;
            auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(response, r);
            if (read_error) {
                std::cerr << "Error reading response: " << glz::format_error(read_error, r) << '\n';
                std::cerr << "Response: " << r << '\n';
                throw std::runtime_error("Failed to parse response: " + glz::format_error(read_error, r));
            }

            // Only maintain local conversation if not using external conversation management
            if (!full_conversation) {
                std::string gpt_reply = response.choices[0].message.content;
                // Append GPT's reply to the local conversation history
                Message assistant_message;
                assistant_message.role = std::string("assistant");
                assistant_message.content = std::string(gpt_reply);
                conversation.push_back(assistant_message);
            }

            return response;
        }

        template<typename DoPostFunc>
        ChatCompletion sendMessageWithFunctionCalling(
            std::string_view message, 
            DoPostFunc do_post, 
            std::function<std::string(const std::string&, const std::string&)> function_executor,
            std::string_view role = "user", 
            std::string_view model = "grok-3-latest", 
            [[maybe_unused]] std::string_view search_mode = {},
            float temperature = 0.45f,
            const std::vector<std::pair<std::string, std::string>>* full_conversation = nullptr,
            const std::vector<std::string>* function_schemas = nullptr
        ) {
            wait_min_time();
            
            // Build conversation history
            std::vector<OpenAIMessage> chat_history;
            
            // Copy system instructions from local conversation first
            for (const auto& local_msg : conversation) {
                if (local_msg.role == "system") {
                    chat_history.push_back({local_msg.role, local_msg.content});
                }
            }
            
            if (full_conversation) {
                for (const auto& [msg_role, msg_content] : *full_conversation) {
                    chat_history.push_back({msg_role, msg_content});
                }
                chat_history.push_back({std::string(role), std::string(message)});
            } else {
                for (const auto& msg : conversation) {
                    if (msg.role != "system") {
                        chat_history.push_back({msg.role, msg.content});
                    }
                }
                chat_history.push_back({std::string(role), std::string(message)});
            }
            
            auto escape_json = [](const std::string& str) -> std::string {
                std::string escaped;
                escaped.reserve(str.size() + str.size() / 10 + 1);
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
            };
            
            auto serialize_messages = [&](const std::vector<OpenAIMessage>& msgs) -> std::string {
                std::string json = "[";
                for (size_t i = 0; i < msgs.size(); ++i) {
                    if (i > 0) json += ",";
                    const auto& m = msgs[i];
                    json += "{";
                    json += std::format("\"role\":\"{}\"", m.role);
                    
                    if (m.role == "tool") {
                        json += std::format(",\"tool_call_id\":\"{}\"", m.tool_call_id);
                        json += std::format(",\"content\":\"{}\"", escape_json(m.content));
                    } else if (m.role == "assistant" && !m.tool_calls.empty() && m.content.empty()) {
                        json += ",\"content\":null";
                    } else {
                        json += std::format(",\"content\":\"{}\"", escape_json(m.content));
                    }
                    
                    if (!m.tool_calls.empty()) {
                        json += ",\"tool_calls\":[";
                        for (size_t j = 0; j < m.tool_calls.size(); ++j) {
                            if (j > 0) json += ",";
                            const auto& tc = m.tool_calls[j];
                            json += "{";
                            json += std::format("\"id\":\"{}\",", tc.id);
                            json += "\"type\":\"function\",";
                            json += std::format("\"function\":{{\"name\":\"{}\",\"arguments\":\"{}\"}}", 
                                                tc.function.name, escape_json(tc.function.arguments));
                            json += "}";
                        }
                        json += "]";
                    }
                    json += "}";
                }
                json += "]";
                return json;
            };
            
            std::string url = std::format("{}/chat/completions", base_url_);
            if (base_url_.empty()) {
                throw std::runtime_error("Base URL is empty in cppgpt instance");
            }
            
            std::string final_text;
            bool keep_calling = true;
            int iterations = 0;
            const int max_iterations = 5;
            
            while (keep_calling && iterations < max_iterations) {
                iterations++;
                
                std::string body = "{";
                body += std::format("\"model\":\"{}\",", model);
                body += std::format("\"temperature\":{},", temperature);
                body += std::format("\"messages\":{}", serialize_messages(chat_history));
                
                if (function_schemas && !function_schemas->empty()) {
                    body += ",\"tools\":[";
                    for (size_t i = 0; i < function_schemas->size(); ++i) {
                        if (i > 0) body += ",";
                        body += std::format("{{\"type\":\"function\",\"function\":{}}}", (*function_schemas)[i]);
                    }
                    body += "]";
                    body += ",\"tool_choice\":\"auto\"";
                }
                body += "}";
                
                auto r = do_post(url, body, [this](auto header_setter){
                    header_setter("Authorization: Bearer " + api_key_);
                    header_setter("Content-Type: application/json");
                });
                
                OpenAIChatCompletion response;
                auto read_error = glz::read<glz::opts{.error_on_unknown_keys=false}>(response, r);
                if (read_error) {
                    throw std::runtime_error("Failed to parse response: " + glz::format_error(read_error, r));
                }
                
                if (response.choices.empty()) {
                    throw std::runtime_error("OpenAI response contains no choices");
                }
                
                const auto& choice = response.choices[0];
                
                if (!choice.message.tool_calls.empty()) {
                    // Append assistant message containing tool calls
                    OpenAIMessage assistant_msg;
                    assistant_msg.role = "assistant";
                    assistant_msg.content = choice.message.content.value_or("");
                    for (const auto& tc : choice.message.tool_calls) {
                        OpenAIMessageToolCall call;
                        call.id = tc.id;
                        call.function.name = tc.function.name;
                        call.function.arguments = tc.function.arguments;
                        assistant_msg.tool_calls.push_back(call);
                    }
                    chat_history.push_back(assistant_msg);
                    
                    // Execute each tool and collect responses
                    for (const auto& tc : choice.message.tool_calls) {
                        std::string result;
                        try {
                            result = function_executor(tc.function.name, tc.function.arguments);
                        } catch (const std::exception& e) {
                            result = std::format("{{\"error\":\"{}\"}}", e.what());
                        }
                        
                        OpenAIMessage tool_msg;
                        tool_msg.role = "tool";
                        tool_msg.tool_call_id = tc.id;
                        tool_msg.name = tc.function.name;
                        tool_msg.content = result;
                        chat_history.push_back(tool_msg);
                    }
                    
                    keep_calling = true;
                } else {
                    final_text = choice.message.content.value_or("");
                    keep_calling = false;
                }
            }
            
            // Maintain local conversation history if needed
            if (!full_conversation) {
                conversation.push_back({"assistant", final_text});
            }
            
            // Build ChatCompletion response structure
            ChatCompletion response_obj{};
            response_obj.choices.resize(1);
            response_obj.choices[0].index = 0;
            response_obj.choices[0].message.role = "assistant";
            response_obj.choices[0].message.content = final_text;
            response_obj.choices[0].finish_reason = "stop";
            
            return response_obj;
        }

        void clear()
        {
            conversation.clear();
        }

        auto const &get_conversation() const
        {
            return conversation;
        }

    private:
        std::string api_key_;            // Your OpenAI API key
        std::vector<Message> conversation; // To keep track of the conversation history
        std::string const base_url_;

        static std::chrono::system_clock::duration min_time_between_requests() {
            return std::chrono::seconds(1);
        }

        static void wait_min_time() {
            static std::mutex mutex;
            std::lock_guard lock(mutex);
            static auto last_request = std::chrono::system_clock::now();
            auto now = std::chrono::system_clock::now();
            auto elapsed = now - last_request;
            if (elapsed < min_time_between_requests()) {
                std::this_thread::sleep_for(min_time_between_requests() - elapsed);
            }
            last_request = std::chrono::system_clock::now();
        }
    };
}
