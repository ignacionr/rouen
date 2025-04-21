#pragma once

#include <string>
#include <format>
#include <cstdlib>

#include "cppgpt.hpp"
#include "fetch.hpp"
#include "../registrar.hpp"

namespace mail {
    class EmailMetadataAnalyzer {
    public:
        EmailMetadataAnalyzer() = default;

        // Analyze email content and generate metadata using Grok AI
        std::string generate_metadata(const std::string& email_content) {
            try {
                // Create cppgpt instance with API key and base URL
                // Use Grok API instead of OpenAI
                std::string api_key = std::getenv("GROK_API_KEY");
                if (api_key.empty()) {
                    "notify"_sfn("Warning: GROK_API_KEY environment variable not set");
                    return R"({
                        "urgency": 0,
                        "category": "unprocessed",
                        "summary": "Email metadata could not be processed. Grok API key not provided.",
                        "tags": ["unprocessed"]
                    })";
                }
                
                ignacionr::cppgpt gpt(api_key, ignacionr::cppgpt::grok_base);
                
                // Add system instructions for the AI
                gpt.add_instructions(
                    "You are an email analyzer. Your task is to analyze the email content and extract metadata. "
                    "Determine the urgency level (0-2, where 0 is not urgent, 1 is moderate, 2 is highly urgent), "
                    "categorize the email (work, personal, updates, promotions, etc.), "
                    "create a concise summary of the content, and suggest relevant tags. "
                    "Respond only with a valid JSON object with the following keys: "
                    "urgency (number), category (string), summary (string), tags (array of strings)."
                );
                
                // Prepare the message with the email content
                // Truncate if needed to avoid excessive token usage
                std::string truncated_content = email_content;
                if (truncated_content.size() > 10000) {
                    truncated_content = truncated_content.substr(0, 10000) + "... [content truncated]";
                }
                
                // Send the message to the Grok model
                auto response = gpt.sendMessage(truncated_content, 
                    [this](const std::string& url, const std::string& data, auto header_client) {
                        return fetcher_.post(url, data, header_client);
                    }, 
                    "user", 
                    "grok-2-latest"  // Use Grok's model
                );
                
                // Extract the AI's response from the structured response
                std::string ai_reply = response.choices[0].message.content;
                
                // Validate JSON structure (basic check)
                if (ai_reply.find('{') == std::string::npos || ai_reply.find('}') == std::string::npos) {
                    "notify"_sfn("Warning: Invalid JSON response from Grok");
                    return R"({
                        "urgency": 0,
                        "category": "unprocessed",
                        "summary": "AI response was not in valid JSON format.",
                        "tags": ["error", "unprocessed"]
                    })";
                }
                
                // Clean up any markdown or extra text around the JSON
                size_t json_start = ai_reply.find('{');
                size_t json_end = ai_reply.rfind('}') + 1;
                
                if (json_start != std::string::npos && json_end != std::string::npos) {
                    ai_reply = ai_reply.substr(json_start, json_end - json_start);
                }
                
                return ai_reply;
            }
            catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to generate email metadata: {}", e.what()));
                return R"({
                    "urgency": 0,
                    "category": "unprocessed",
                    "summary": "Error during processing.",
                    "tags": ["error"]
                })";
            }
        }

    private:
        http::fetch fetcher_;
    };
}