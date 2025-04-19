#pragma once

#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

using json = nlohmann::json;

namespace ignacionr
{
    class cppgpt
    {
    public:
        static constexpr auto open_ai_base = "https://api.openai.com/v1/";
        static constexpr auto  groq_base = "https://api.groq.com/openai/v1";
        static constexpr auto  grok_base = "https://api.x.ai/v1";
        cppgpt(const std::string &api_key, const std::string &base_url) : api_key_(api_key), base_url_{base_url} {}

        cppgpt new_conversation() {
            return cppgpt(api_key_, base_url_);
        }

        void add_instructions(std::string_view instructions, std::string_view role = "system")
        {
            conversation.push_back({{"role", role}, {"content", instructions}});
        }

        // Function to send a message to GPT and receive the reply
        json sendMessage(std::string_view message, auto do_post, std::string_view role = "user", std::string_view model = "grok-2-latest", float temperature = 0.45)
        {
            wait_min_time();
            // Append the new message to the conversation history
            conversation.push_back({{"role", role}, {"content", message}});

            // Prepare the API request payload
            json payload = {
                {"model", model},
                {"messages", conversation},
                {"temperature", temperature}};

            

            // Send the API request
            auto url = std::format("{}/chat/completions", base_url_);
            auto body = payload.dump();
            auto r = do_post(url, body, [this](auto header_setter){
                header_setter("Authorization: Bearer " + api_key_);
                header_setter("Content-Type: application/json");
            });

            // Parse the API response
            auto response = json::parse(r);
            auto gpt_reply = response["choices"][0]["message"]["content"];

            // Append GPT's reply to the conversation history
            conversation.push_back({{"role", "assistant"}, {"content", gpt_reply}});

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
        std::vector<json> conversation; // To keep track of the conversation history
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
