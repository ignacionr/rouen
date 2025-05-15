#pragma once

#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../../helpers/imgui_include.hpp"
// Include our compatibility layer for C++20/23 features
#include "../../../helpers/compat/compat.hpp"

#include "../../../models/mail/imap_host.hpp"
#include "../../../models/mail/message.hpp"
#include "../../../registrar.hpp"
#include "../../interface/card.hpp"
#include "mail_screen.hpp"

namespace rouen::cards
{
    class mail : public card
    {
    public:
        mail(const std::string &host = "imaps://imap.gmail.com/",
             const std::string &username = {},
             const std::string &password = {})
        {
            setup_colors();
            
            name("Mail");
            requested_fps = 1; // Update once per second
            width = 770.0f;    // Wider to accommodate email content

            // Helper function to get value from env var if input is empty
            auto get_from_env = [](const std::string &value, const char *env_var) -> std::string {
                if (!value.empty()) {
                    return value;
                }
                
                const char *env_value = std::getenv(env_var);
                return env_value ? env_value : "";
            };
            
            // Use environment variables for username and password if not provided
            std::string actual_username = get_from_env(username, "GOOGLE_LOGIN");
            std::string actual_password = get_from_env(password, "GOOGLE_APP");

            // Initialize the IMAP host
            host_ = std::make_shared<::mail::imap_host>(host, actual_username, actual_password);
            mail_screen_ = std::make_unique<::mail::mail_screen>(host_);

            // Store connection information
            host_url_ = host;
            username_ = actual_username;
            password_ = actual_password;

            // Initial load of messages
            refresh_messages();
        }

        ~mail() override
        {
            stop_refresh_thread();
        }

        bool render() override
        {
            // Check if it's time to refresh based on refresh interval
            auto now = std::chrono::steady_clock::now();
            if (now - last_refresh_ > refresh_interval_)
            {
                refresh_messages();
                last_refresh_ = now;
            }

            return render_window([this]()
            {
                // Mailbox selector
                render_mailbox_selector();
                
                ImGui::SameLine();
                if (ImGui::Button("Refresh")) {
                    refresh_messages();
                }
                
                ImGui::Separator();
                
                // Mail screen rendering
                if (mail_screen_) {
                    mail_screen_->render();
                } else {
                    render_error("Mail screen not initialized!");
                }
            });
        }

        std::string get_uri() const override
        {
            return host_url_.empty() ? "mail" : std::format("mail:{}:{}", host_url_, username_);
        }

        void refresh_messages()
        {
            stop_refresh_thread();
            last_refresh_ = std::chrono::steady_clock::now();

            // Start a new thread for refreshing
            refresh_thread_ = std::jthread([this] {
                execute_safely([this]() {
                    if (mail_screen_) {
                        mail_screen_->refresh();
                    }
                }, "Failed to refresh messages");
            });
        }

    private:
        // Member variables to store connection information for get_uri method
        std::string host_url_;
        std::string username_;
        std::string password_;

        // Helper methods for DRY code
        void setup_colors()
        {
            // Set custom colors for the Mail card
            colors[0] = {0.2f, 0.4f, 0.8f, 1.0f}; // Blue primary color
            colors[1] = {0.3f, 0.5f, 0.9f, 0.7f}; // Lighter blue secondary color

            // Additional colors for specific elements
            get_color(2, ImVec4(0.4f, 0.6f, 1.0f, 1.0f)); // Light blue for titles
            get_color(3, ImVec4(0.4f, 0.8f, 0.4f, 1.0f)); // Green for active/positive elements
            get_color(4, ImVec4(0.8f, 0.8f, 0.3f, 1.0f)); // Yellow for warnings/highlights
            get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        }
        
        void render_error(const std::string& message)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", message.c_str());
        }
        
        void stop_refresh_thread()
        {
            // Make sure the refresh thread is stopped
            if (refresh_thread_.joinable())
            {
                refresh_thread_.request_stop();
            }
        }
        
        template<typename Func>
        void execute_safely(Func func, const std::string& error_prefix)
        {
            try {
                func();
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("{}: {}", error_prefix, e.what()));
            }
        }
        
        // Helper method for connection with retry logic (following C++23 style)
        bool connect_with_retry(int max_retries = 3)
        {
            static std::mutex connection_mutex;
            std::lock_guard lock(connection_mutex);
            
            for (int attempt = 0; attempt < max_retries; ++attempt) {
                try {
                    host_->connect();
                    return true; // Successfully connected
                } catch (const std::exception& e) {
                    // Log the error with the attempt number
                    "notify"_sfn(std::format("Connection attempt {}/{} failed: {}", 
                        attempt + 1, max_retries, e.what()));
                    
                    // Only sleep between retries, not after the last attempt
                    if (attempt < max_retries - 1) {
                        // Exponential backoff with jitter for better retry behavior
                        const auto base_delay = std::chrono::milliseconds(500 * (attempt + 1));
                        const auto jitter = std::chrono::milliseconds(std::rand() % 200);
                        std::this_thread::sleep_for(base_delay + jitter);
                    }
                }
            }
            
            "notify"_sfn(std::format("Failed to connect after {} attempts", max_retries));
            return false;
        }
        
        void render_mailbox_selector()
        {
            if (ImGui::BeginCombo("Mailbox", current_mailbox_.c_str())) {
                if (mailboxes_.empty()) {
                    // Lazy load mailboxes on first access with robust error handling
                    try {
                        // First ensure we have a valid connection
                        if (connect_with_retry()) {
                            // Now that we're connected, try to list mailboxes
                            try {
                                host_->list_mailboxes([this](std::string_view mailbox) {
                                    mailboxes_.push_back(std::string(mailbox));
                                });
                                
                                // If no mailboxes were found, add a default one
                                if (mailboxes_.empty()) {
                                    mailboxes_.push_back("INBOX");
                                }
                            } catch (const std::exception& e) {
                                "notify"_sfn(std::format("Failed to list mailboxes: {}", e.what()));
                                // Add default mailbox for fallback
                                mailboxes_.push_back("INBOX");
                            }
                        } else {
                            // Connection failed after retries, add default mailbox
                            mailboxes_.push_back("INBOX");
                        }
                    } catch (const std::exception& e) {
                        "notify"_sfn(std::format("Unexpected error loading mailboxes: {}", e.what()));
                        // Add default mailbox as fallback
                        mailboxes_.push_back("INBOX");
                    }
                }
                
                for (const auto& mailbox : mailboxes_) {
                    bool is_selected = (current_mailbox_ == mailbox);
                    if (ImGui::Selectable(mailbox.c_str(), is_selected)) {
                        if (current_mailbox_ != mailbox) {
                            current_mailbox_ = mailbox;
                            execute_safely([this]() {
                                host_->select_mailbox(current_mailbox_);
                                refresh_messages();
                            }, "Failed to select mailbox");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        std::shared_ptr<::mail::imap_host> host_;
        std::unique_ptr<::mail::mail_screen> mail_screen_;
        std::vector<std::string> mailboxes_;
        std::string current_mailbox_ = "INBOX";
        std::chrono::steady_clock::time_point last_refresh_ = std::chrono::steady_clock::now();
        std::chrono::seconds refresh_interval_{300}; // Refresh every 5 minutes
        std::jthread refresh_thread_;                // Thread for refreshing messages in the background
    };

} // namespace rouen::cards
