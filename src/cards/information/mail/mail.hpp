#pragma once

#include <chrono>
#include <format>
#include <imgui/imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>
#include <mutex>

#include "../../interface/card.hpp"
#include "../../../models/mail/imap_host.hpp"
#include "../../../models/mail/message.hpp"
#include "mail_screen.hpp"
#include "../../../registrar.hpp"

namespace rouen::cards {

class mail : public card {
public:
    mail(const std::string& host = "imaps://imap.gmail.com/", 
         const std::string& username = "ignacionr@gmail.com", 
         const std::string& password = "") {
        
        // Set custom colors for the Mail card
        colors[0] = {0.2f, 0.4f, 0.8f, 1.0f}; // Blue primary color
        colors[1] = {0.3f, 0.5f, 0.9f, 0.7f}; // Lighter blue secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(0.4f, 0.6f, 1.0f, 1.0f)); // Light blue for titles
        get_color(3, ImVec4(0.4f, 0.8f, 0.4f, 1.0f)); // Green for active/positive elements
        get_color(4, ImVec4(0.8f, 0.8f, 0.3f, 1.0f)); // Yellow for warnings/highlights
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        
        name("Mail");
        requested_fps = 1;  // Update once per second
        width = 700.0f;     // Wider to accommodate email content
        
        // Use environment variable for password if not provided
        std::string actual_password = password;
        if (actual_password.empty()) {
            const char* google_app = std::getenv("GOOGLE_APP");
            if (google_app) {
                actual_password = google_app;
            }
        }
        
        // Initialize the IMAP host
        host_ = std::make_shared<::mail::imap_host>(host, username, actual_password);
        mail_screen_ = std::make_unique<::mail::mail_screen>(host_);
        
        // Initial load of messages
        refresh_messages();
    }
    
    ~mail() override {
        // Make sure the refresh thread is stopped before destroying the object
        if (refresh_thread_.joinable()) {
            refresh_thread_.request_stop();
        }
    }
    
    bool render() override {
        // Check if it's time to refresh based on refresh interval
        auto now = std::chrono::steady_clock::now();
        if (now - last_refresh_ > refresh_interval_) {
            refresh_messages();
            last_refresh_ = now;
        }
        
        return render_window([this]() {
            // Mailbox selector
            if (ImGui::BeginCombo("Mailbox", current_mailbox_.c_str())) {
                if (mailboxes_.empty()) {
                    // Lazy load mailboxes on first access
                    try {
                        host_->connect();
                        host_->list_mailboxes([this](std::string_view mailbox) {
                            mailboxes_.push_back(std::string(mailbox));
                        });
                    } catch (const std::exception& e) {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
                    }
                }
                
                for (const auto& mailbox : mailboxes_) {
                    bool is_selected = (current_mailbox_ == mailbox);
                    if (ImGui::Selectable(mailbox.c_str(), is_selected)) {
                        if (current_mailbox_ != mailbox) {
                            current_mailbox_ = mailbox;
                            try {
                                host_->select_mailbox(current_mailbox_);
                                refresh_messages();
                            } catch (const std::exception& e) {
                                // Handle mailbox selection error
                            }
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                refresh_messages();
            }
            
            ImGui::Separator();
            
            // Mail screen rendering
            if (mail_screen_) {
                mail_screen_->render();
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Mail screen not initialized!");
            }
        });
    }
    
    void refresh_messages() {
        // Start a new background thread to refresh messages
        // If there's an existing thread, stop it first
        if (refresh_thread_.joinable()) {
            refresh_thread_.request_stop();
        }
        
        last_refresh_ = std::chrono::steady_clock::now();
        
        // Start a new thread for refreshing
        refresh_thread_ = std::jthread([this](std::stop_token stop_token) {
            try {
                if (mail_screen_) {
                    mail_screen_->refresh();
                }
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to refresh messages: {}", e.what()));
            }
        });
    }
    
private:
    std::shared_ptr<::mail::imap_host> host_;
    std::unique_ptr<::mail::mail_screen> mail_screen_;
    std::vector<std::string> mailboxes_;
    std::string current_mailbox_ = "INBOX";
    std::chrono::steady_clock::time_point last_refresh_ = std::chrono::steady_clock::now();
    std::chrono::seconds refresh_interval_{300}; // Refresh every 5 minutes
    std::jthread refresh_thread_; // Thread for refreshing messages in the background
};

} // namespace rouen::cards