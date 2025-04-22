#pragma once

#include <string>
#include <format>
#include <cstdlib>
#include <vector>
#include <optional>
#include <sstream>

#include <glaze/glaze.hpp>

#include "cppgpt.hpp"
#include "fetch.hpp"
#include "../registrar.hpp"
#include "../models/mail/metadata_repo.hpp"

namespace mail {
    class EmailMetadataAnalyzer {
    public:
        EmailMetadataAnalyzer(const std::string& db_path = "email_metadata.db") 
            : repository_(db_path) {}

        // Analyze email content and generate metadata using Grok AI
        std::string generate_metadata(const std::string& email_content) {
            try {
                // Extract email ID from headers
                std::string email_id = extract_email_id(email_content);
                
                // First check if we already have metadata for this email
                auto existing = repository_.get(email_id);
                if (existing) {
                    // We already have metadata for this email, return it
                    std::string json_result;
                    auto write_res = glz::write_json(*existing, json_result);
                    if (write_res) {
                        "notify"_sfn(std::format("JSON serialization error: {}", glz::format_error(write_res)));
                    }
                    return json_result;
                }
                
                // Create cppgpt instance with API key and base URL
                // Use Grok API instead of OpenAI
                std::string api_key = std::getenv("GROK_API_KEY");
                if (api_key.empty()) {
                    "notify"_sfn("Warning: GROK_API_KEY environment variable not set");
                    
                    // Create and return basic metadata with error message
                    EmailMetadata metadata;
                    metadata.id = email_id;
                    metadata.summary = "Email metadata could not be processed. Grok API key not provided.";
                    
                    // Store the basic metadata
                    repository_.store(metadata);
                    
                    std::string json_result;
                    auto write_res = glz::write_json(metadata, json_result);
                    if (write_res) {
                        "notify"_sfn(std::format("JSON serialization error: {}", glz::format_error(write_res)));
                    }
                    return json_result;
                }
                
                ignacionr::cppgpt gpt(api_key, ignacionr::cppgpt::grok_base);
                
                // Add system instructions for the AI
                gpt.add_instructions(
                    "You are an email analyzer. Your task is to analyze the email content and extract metadata. "
                    "Determine the urgency level (0-2, where 0 is not urgent, 1 is moderate, 2 is highly urgent), "
                    "categorize the email (work, personal, updates, promotions, etc.), "
                    "create a concise summary of the content, suggest relevant tags, and identify potential actions with links. "
                    "For actions, identify any URLs or actions the user might want to take based on the email content (e.g., 'Register' -> event registration URL). "
                    "Respond only with a valid JSON object with the following keys: "
                    "urgency (number), category (string), summary (string), tags (array of strings), "
                    "action_links (object with action names as keys and URLs as values)."
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
                
                // Attempt to parse the AI's JSON response
                EmailMetadata metadata;
                
                // Clean up any markdown or extra text around the JSON
                size_t json_start = ai_reply.find('{');
                size_t json_end = ai_reply.rfind('}') + 1;
                
                if (json_start != std::string::npos && json_end != std::string::npos) {
                    ai_reply = ai_reply.substr(json_start, json_end - json_start);
                }
                
                // Try to parse the JSON using Glaze
                auto result = glz::read_json(metadata, ai_reply);
                
                if (result) {
                    "notify"_sfn("Warning: Invalid JSON response from Grok");
                    
                    // Create a fallback metadata with error information
                    EmailMetadata fallback;
                    fallback.id = email_id;
                    fallback.summary = "AI response was not in valid JSON format.";
                    fallback.tags = {"error", "unprocessed"};
                    
                    // Store the fallback metadata
                    repository_.store(fallback);
                    
                    std::string json_result;
                    auto write_res = glz::write_json(fallback, json_result);
                    if (write_res) {
                        "notify"_sfn(std::format("JSON serialization error: {}", glz::format_error(write_res)));
                    }
                    return json_result;
                }
                
                // Add the email ID to the metadata
                metadata.id = email_id;
                
                // Store the metadata in the repository
                repository_.store(metadata);
                
                // Serialize the metadata back to JSON using Glaze
                std::string json_result;
                auto write_res = glz::write_json(metadata, json_result);
                if (write_res) {
                    "notify"_sfn(std::format("JSON serialization error: {}", glz::format_error(write_res)));
                }
                return json_result;
            }
            catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to generate email metadata: {}", e.what()));
                
                // Create an error metadata
                EmailMetadata error_metadata;
                
                // Try to extract the email ID even in error case
                try {
                    error_metadata.id = extract_email_id(email_content);
                } catch (...) {
                    // Ignore extraction errors in the error handler
                }
                
                error_metadata.summary = std::format("Error during processing: {}", e.what());
                error_metadata.tags = {"error"};
                
                // Store the error metadata
                repository_.store(error_metadata);
                
                std::string json_result;
                auto write_res = glz::write_json(error_metadata, json_result);
                if (write_res) {
                    "notify"_sfn(std::format("JSON serialization error: {}", glz::format_error(write_res)));
                }
                return json_result;
            }
        }
        
        // Get metadata from repository by ID
        std::optional<EmailMetadata> get_metadata(const std::string& email_id) {
            return repository_.get(email_id);
        }
        
        // Get recent email metadata
        std::vector<EmailMetadata> get_recent(int limit = 20) {
            return repository_.get_recent(limit);
        }
        
        // Get email metadata by category
        std::vector<EmailMetadata> get_by_category(const std::string& category) {
            return repository_.get_by_category(category);
        }
        
        // Get email metadata by tag
        std::vector<EmailMetadata> get_by_tag(const std::string& tag) {
            return repository_.get_by_tag(tag);
        }

    private:
        // Extract email unique ID from headers
        std::string extract_email_id(const std::string& email_content) {
            // Common header fields that contain unique IDs
            // MessageID is the standard unique identifier for emails
            const std::vector<std::string> id_headers = {
                "Message-ID:", "Message-Id:", "Message-id:",
                "X-GM-MSGID:", // Gmail specific
                "X-MS-Exchange-MessageId:" // Microsoft specific
            };
            
            // Find header section (headers are at the top, followed by a blank line)
            size_t header_end = email_content.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                header_end = email_content.find("\n\n");
            }
            
            if (header_end != std::string::npos) {
                std::string headers = email_content.substr(0, header_end);
                
                // Search for ID headers - we'll iterate through headers line by line
                std::stringstream ss(headers);
                std::string line;
                
                while (std::getline(ss, line)) {
                    // Check if this line starts with any of our ID headers
                    for (const auto& header : id_headers) {
                        if (line.compare(0, header.length(), header) == 0) {
                            // Extract the value after the header
                            std::string id = line.substr(header.length());
                            
                            // Trim whitespace and angle brackets
                            id.erase(0, id.find_first_not_of(" \t<"));
                            size_t last = id.find_last_not_of(" \t\r\n>");
                            if (last != std::string::npos) {
                                id.erase(last + 1);
                            }
                            
                            // Validate the ID - it should look like an email address or contain '@' for a Message-ID
                            if (!id.empty() && (id.find('@') != std::string::npos || id.find('.') != std::string::npos)) {
                                return id;
                            }
                        }
                    }
                }
            }
            
            // If we couldn't find a valid Message-ID, generate a hash-based one
            // using a combination of other headers
            std::string alt_id = "generated-";
            size_t hash_value = 0;
            
            // Extract Subject, From, and Date if available to generate a more stable hash
            auto extract_header = [&email_content](const std::string& header_name) -> std::string {
                size_t pos = email_content.find(header_name);
                if (pos != std::string::npos) {
                    size_t start = pos + header_name.length();
                    size_t end = email_content.find("\n", start);
                    if (end != std::string::npos) {
                        return email_content.substr(start, end - start);
                    }
                }
                return {};
            };
            
            std::string subject = extract_header("Subject:");
            std::string from = extract_header("From:");
            std::string date = extract_header("Date:");
            
            std::string combined = subject + from + date;
            if (!combined.empty()) {
                // Simple string hash
                for (char c : combined) {
                    hash_value = ((hash_value << 5) + hash_value) + c;
                }
                alt_id += std::to_string(hash_value);
            } else {
                // Truly last resort - use the first 100 chars of email content
                std::string sample = email_content.substr(0, std::min(size_t(100), email_content.length()));
                for (char c : sample) {
                    hash_value = ((hash_value << 5) + hash_value) + c;
                }
                alt_id += std::to_string(hash_value);
            }
            
            return alt_id;
        }

        http::fetch fetcher_;
        MetadataRepository repository_;
    };
}
