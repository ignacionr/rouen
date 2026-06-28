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
