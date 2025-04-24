#pragma once

#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <string_view>

#include <glaze/glaze.hpp>

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
    
    struct Payload {
        std::string model;
        std::vector<Message> messages;
        float temperature;
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
        "temperature", &T::temperature
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
            conversation.push_back({std::string(role), std::string(instructions)});
        }

        // Function to send a message to GPT and receive the reply
        auto sendMessage(
            std::string_view message, 
            auto do_post, 
            std::string_view role = "user", 
            std::string_view model = "grok-2-latest", 
            float temperature = 0.45f)
        {
            wait_min_time();
            // Append the new message to the conversation history
            conversation.push_back({std::string(role), std::string(message)});

            // Prepare the API request payload
            Payload payload{
                std::string(model),
                conversation,
                temperature
            };

            // Send the API request
            auto url = std::format("{}/chat/completions", base_url_);
            std::string body;
            auto error = glz::write_json(payload, body);
            if (error) {
                throw std::runtime_error("Failed to serialize payload: " + glz::format_error(error));
            }
            
            auto r = do_post(url, body, [this](auto header_setter){
                header_setter("Authorization: Bearer " + api_key_);
                header_setter("Content-Type: application/json");
            });

            // Parse the API response
            ChatCompletion response;
            auto read_error = glz::read_json(response, r);
            if (read_error) {
                throw std::runtime_error("Failed to parse response: " + glz::format_error(read_error));
            }
            
            std::string gpt_reply = response.choices[0].message.content;

            // Append GPT's reply to the conversation history
            conversation.push_back({"assistant", gpt_reply});

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
