#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <imgui/imgui.h>

#include "../../../models/mail/imap_host.hpp"
#include "../../../models/mail/message.hpp"
#include "../../../helpers/fetch.hpp"
#include "../../../helpers/cppgpt.hpp"
#include "../../interface/card.hpp"
#include "../../../registrar.hpp"

namespace mail {
    class mail_screen {
    public:
        mail_screen(std::shared_ptr<imap_host> host): host_(host) {}
        
        void render() noexcept {
            if (ImGui::BeginTable("Messages", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableHeadersRow();
                
                for (auto& msg : messages_) {
                    ImGui::PushID(static_cast<int>(msg->uid()));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(msg->from().data(), msg->from().data() + msg->from().size());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(msg->title().data(), msg->title().data() + msg->title().size());
                    ImGui::TableNextColumn();
                    auto date_str = std::format("{:%Y-%m-%d %H:%M}", msg->date());
                    ImGui::TextUnformatted(date_str.data(), date_str.data() + date_str.size());

                    // Show metadata on a second row
                    ImGui::TableNextRow();
                    // using the message's urgency, decide the background color
                    if (msg->urgency() == 1) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4{1.0f, 0.0f, 0.0f, 0.4f}));
                    } else if (msg->urgency() == 2) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4{1.0f, 1.0f, 0.0f, 0.4f}));
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(msg->category().data(), msg->category().data() + msg->category().size());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", msg->summary().data());
                    ImGui::TableNextColumn();
                    for (auto const& tag : msg->tags()) {
                        ImGui::TextUnformatted(tag.data(), tag.data() + tag.size());
                    }
                    if (ImGui::SmallButton("Delete")) {
                        try {
                            host_->delete_message(msg->uid());
                            messages_.erase(std::remove_if(messages_.begin(), messages_.end(), [deleted_uid = msg->uid()](auto const& msg) {
                                return msg->uid() == deleted_uid;
                            }), messages_.end());
                            ImGui::PopID();
                            break;
                        }
                        catch (...) {
                            ImGui::PopID();
                            ImGui::EndTable();
                            throw;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        
        void refresh() {
            try {
                host_->connect();
                auto uids = host_->get_mail_uids();
                
                // Find messages that are new
                std::vector<long long> new_uids;
                for (auto uid : uids) {
                    auto it = std::find_if(messages_.begin(), messages_.end(), 
                        [uid](const auto& msg) { return msg->uid() == uid; });
                    if (it == messages_.end()) {
                        new_uids.push_back(uid);
                    }
                }
                
                // Create message objects for new messages
                for (auto uid : new_uids) {
                    auto header = host_->get_mail_header(uid);
                    auto msg = std::make_shared<message>(uid, header);
                    messages_.emplace_back(msg);
                    
                    // Start a new thread to get the body of the mail and process metadata
                    pending_tasks_.push_back([this, uid, msg]() {
                        try {
                            // Get the body of the mail
                            auto body = host_->get_mail_body(uid);
                            // Combine with the header to get the full message
                            auto header_and_body = std::format("{}\n\n{}", msg->header(), body);
                            
                            // Process metadata using the cppgpt helper
                            std::string metadata_json = generate_email_metadata(header_and_body);
                            
                            msg->set_metadata(metadata_json);
                        } catch (const std::exception& e) {
                            "notify"_sfn(std::format("Failed to process message UID {}: {}", 
                                uid, e.what()));
                        }
                    });
                }
                
                // Process any pending tasks
                std::thread([this]() {
                    auto retry = std::vector<std::function<void()>>{};
                    auto tasks = std::move(pending_tasks_);
                    pending_tasks_.clear();
                    
                    while (!tasks.empty()) {
                        retry.clear();
                        for (auto& task : tasks) {
                            try {
                                task();
                            } catch (...) {
                                retry.push_back(task);
                            }
                        }
                        tasks = retry;
                    }
                }).detach();
                
                // Remove messages that no longer exist on the server
                messages_.erase(
                    std::remove_if(messages_.begin(), messages_.end(), 
                        [&uids](const auto& msg) {
                            return std::find(uids.begin(), uids.end(), msg->uid()) == uids.end();
                        }), 
                    messages_.end()
                );
                
                // Sort messages by date (newest first)
                std::sort(messages_.begin(), messages_.end(), 
                    [](const auto& a, const auto& b) { 
                        return a->date() > b->date(); 
                    }
                );
                
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to refresh messages: {}", e.what()));
            }
        }
        
    private:
        std::shared_ptr<imap_host> host_;
        std::vector<std::shared_ptr<message>> messages_;
        std::vector<std::function<void()>> pending_tasks_;
        http::fetch fetcher;
        
        // New method to analyze email content and generate metadata using Grok
        std::string generate_email_metadata(const std::string& email_content) {
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
                        return fetcher.post(url, data, header_client);
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
    };
}