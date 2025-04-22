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
#include "../../../helpers/email_metadata_analyzer.hpp"
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
                    
                    // Display action links as buttons
                    if (!msg->action_links().empty()) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TableNextColumn();
                        // Create a button for each action link
                        int count{-1};
                        for (const auto& [action, link] : msg->action_links()) {
                            if ((++count %= 3) > 0) {
                                ImGui::SameLine();
                            }
                            if (ImGui::SmallButton(action.c_str())) {
                                // Open the URL using the system's default browser
                                // This is platform-specific, but since we're providing a Linux implementation:
                                std::string cmd = std::format("xdg-open \"{}\"", link);
                                system(cmd.c_str());
                            }
                        }
                        ImGui::NewLine();
                        ImGui::TableNextColumn();
                    }
                    
                    // Display email actions like Delete, Archive, etc.
                    // when render_action_buttons changes the message list, it returns true
                    if (render_action_buttons(msg)) {
                        ImGui::PopID();
                        break; // we can't trust the old list
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
                            
                            // Process metadata using the EmailMetadataAnalyzer helper
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
        EmailMetadataAnalyzer metadata_analyzer_;
        
        // Define a structure for email actions
        struct EmailAction {
            std::string label;
            std::function<void(std::shared_ptr<imap_host>, long long)> action;
            bool removes_from_list;
        };
        
        // Static list of all available email actions
        static const std::vector<EmailAction>& get_email_actions() {
            static const std::vector<EmailAction> email_actions = {
                {"Delete", [](std::shared_ptr<imap_host> host, long long uid) {
                    host->delete_message(uid);
                }, true},
                {"Archive", [](std::shared_ptr<imap_host> host, long long uid) {
                    host->move_message(uid, "Archives");
                }, true}
            };
            return email_actions;
        }
        
        // Render action buttons for a message
        bool render_action_buttons(const std::shared_ptr<message>& msg) {
            for (const auto& action_item : get_email_actions()) {
                
                if (ImGui::SmallButton(action_item.label.c_str())) {
                    try {
                        // Execute the action
                        action_item.action(host_, msg->uid());
                        if (action_item.removes_from_list) {   
                            // Remove the message from the list
                            messages_.erase(std::remove_if(messages_.begin(), messages_.end(), 
                                [removed_uid = msg->uid()](auto const& msg) {
                                    return msg->uid() == removed_uid;
                                }), messages_.end());
                            
                            return true; // Exit early as the message is now deleted
                        }
                    }
                    catch (std::exception const& e) {
                        "notify"_sfn(std::format("Failed to perform {}: {}", 
                            action_item.label, e.what()));
                        throw;
                    }
                }
                ImGui::SameLine();
            }
            return false;
        }
        
        // Delegate email metadata generation to the dedicated analyzer class
        std::string generate_email_metadata(const std::string& email_content) {
            return metadata_analyzer_.generate_metadata(email_content);
        }
    };
}