#pragma once

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../../../helpers/imgui_include.hpp"
// Include our compatibility layer for C++20/23 features
#include "../../../helpers/compat/compat.hpp"
#include "../../../helpers/adaptive_cards/parser.hpp"
#include "../../../helpers/adaptive_cards/renderer.hpp"
#include "../../../helpers/glaze_include.hpp"
#include "../../../helpers/imgui_ui_context.hpp"
#include "../../../helpers/platform_utils.hpp"
#include "../../../fonts.hpp"

#include "../../../models/mail/imap_host.hpp"
#include "../../../models/mail/message.hpp"
#include "../../../registrar.hpp"
#include "../../interface/card.hpp"
#include "mail_screen.hpp"

namespace rouen::cards
{
    struct email_summary {
        std::string id;
        std::string from;
        std::string subject;
        std::string date_str;
        bool is_unread{true};
        std::string body;
        std::string folder{"INBOX"};
    };

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

            // Store connection information
            host_url_ = host;
            username_ = actual_username;
            password_ = actual_password;
            current_folder_ = current_mailbox_;

            // Default sample messages matching the specification
            messages_ = {
                {"msg-101", "Linus Torvalds", "Git 2.47 release notes review", "Today 14:15", true, "", "INBOX"},
                {"msg-102", "GitHub Notifications", "Pull request #42 merged into main", "Today 11:30", true, "", "INBOX"},
                {"msg-103", "AWS CloudWatch", "Billing alert threshold reached for Oct 2026", "Yesterday", true, "", "INBOX"}
            };
            unread_count_ = 3;

            // Initialize the IMAP host
            host_ = std::make_shared<::mail::imap_host>(host, actual_username, actual_password);
            mail_screen_ = std::make_unique<::mail::mail_screen>(host_);

            // Initial load of messages if credentials are provided
            if (!username_.empty() && !password_.empty()) {
                refresh_messages();
            }
        }

        ~mail() override
        {
            stop_refresh_thread();
        }

        bool render() override
        {
            rouen::ui::imgui_ui_context_impl ui;
            return render(ui);
        }

        bool render(rouen::ui::ui_context& ui) override
        {
            // Check if it's time to refresh based on refresh interval
            auto now = std::chrono::steady_clock::now();
            if (now - last_refresh_ > refresh_interval_)
            {
                refresh_messages();
                last_refresh_ = now;
            }

            return render_window([this, &ui]()
            {
                // Presentation mode selector
                ui.checkbox("Adaptive Card View", &adaptive_view_mode);
                ui.separator();

                if (adaptive_view_mode) {
                    auto render_now = std::chrono::steady_clock::now();
                    auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(render_now - last_adaptive_parse_time_).count();
                    if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                        try {
                            adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                            adaptive_error_.clear();
                        } catch (const std::exception& e) {
                            adaptive_error_ = e.what();
                        }
                        last_adaptive_parse_time_ = render_now;
                    }

                    if (!adaptive_error_.empty()) {
                        ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                    } else {
                        adaptive_renderer_.render(
                            adaptive_bound_, adaptive_input_state_,
                            helpers::adaptive_cards::renderer::action_callbacks{
                                .open_url = [](const std::string& url) {
                                    static_cast<void>(rouen::platform::open_url(url));
                                },
                                .on_submit = [this](const std::string& payload) {
                                    handle_action(payload);
                                }
                            },
                            helpers::adaptive_cards::render_config{
                                .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                                .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                                .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                            }
                        );
                    }
                    return;
                }

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

        /// Returns the Adaptive Cards 1.5 JSON representation of the card state
        [[nodiscard]] std::string get_adaptive_card_json() const override
        {
            glz::json_t card;
            card["type"] = "AdaptiveCard";
            card["version"] = "1.5";
            card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

            std::vector<glz::json_t> body;

            // 1. Header Container
            {
                glz::json_t header;
                header["type"] = "Container";
                header["style"] = "emphasis";

                glz::json_t col_set;
                col_set["type"] = "ColumnSet";
                std::vector<glz::json_t> columns;

                // Column 1: Emoji icon
                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "auto";
                    std::vector<glz::json_t> items;
                    glz::json_t icon_tb;
                    icon_tb["type"] = "TextBlock";
                    icon_tb["text"] = "\u2709\ufe0f"; // ✉️
                    icon_tb["size"] = "Large";
                    items.push_back(std::move(icon_tb));
                    col["items"] = std::move(items);
                    columns.push_back(std::move(col));
                }

                // Column 2: Title and Host/User Subtitle
                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "stretch";
                    std::vector<glz::json_t> items;

                    glz::json_t title_tb;
                    title_tb["type"] = "TextBlock";
                    title_tb["text"] = "Email Inbox (IMAP)";
                    title_tb["weight"] = "Bolder";
                    title_tb["size"] = "Large";
                    items.push_back(std::move(title_tb));

                    std::string account_desc = !username_.empty() 
                        ? std::format("{} on {}", username_, host_url_) 
                        : "ignacio@domain.com on imap.domain.com:993";

                    glz::json_t sub_tb;
                    sub_tb["type"] = "TextBlock";
                    sub_tb["text"] = account_desc;
                    sub_tb["isSubtle"] = true;
                    sub_tb["size"] = "Small";
                    items.push_back(std::move(sub_tb));

                    col["items"] = std::move(items);
                    columns.push_back(std::move(col));
                }

                // Column 3: Unread Badge
                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "auto";
                    std::vector<glz::json_t> items;

                    glz::json_t badge_tb;
                    badge_tb["type"] = "TextBlock";
                    badge_tb["text"] = std::format("{} UNREAD", unread_count_);
                    badge_tb["weight"] = "Bolder";
                    badge_tb["color"] = "Attention";
                    badge_tb["size"] = "Small";
                    items.push_back(std::move(badge_tb));

                    col["items"] = std::move(items);
                    columns.push_back(std::move(col));
                }

                col_set["columns"] = std::move(columns);
                std::vector<glz::json_t> header_items;
                header_items.push_back(std::move(col_set));
                header["items"] = std::move(header_items);
                body.push_back(std::move(header));
            }

            // 2. Folder Selection Container
            {
                glz::json_t folder_container;
                folder_container["type"] = "Container";
                std::vector<glz::json_t> folder_items;

                glz::json_t choice_set;
                choice_set["type"] = "Input.ChoiceSet";
                choice_set["id"] = "mail_folder";
                choice_set["label"] = "Folder";
                choice_set["value"] = current_folder_.empty() ? "INBOX" : current_folder_;

                std::vector<glz::json_t> choices;
                choices.push_back(glz::json_t::object_t{
                    {"title", std::format("Inbox ({} unread)", unread_count_)},
                    {"value", "INBOX"}
                });
                choices.push_back(glz::json_t::object_t{
                    {"title", "Sent Items"},
                    {"value", "Sent"}
                });
                choices.push_back(glz::json_t::object_t{
                    {"title", "Drafts"},
                    {"value", "Drafts"}
                });
                choices.push_back(glz::json_t::object_t{
                    {"title", "Archive"},
                    {"value", "Archive"}
                });
                choice_set["choices"] = std::move(choices);
                folder_items.push_back(std::move(choice_set));

                folder_container["items"] = std::move(folder_items);
                body.push_back(std::move(folder_container));
            }

            // 3. FactSet (Messages)
            {
                glz::json_t fact_set;
                fact_set["type"] = "FactSet";
                std::vector<glz::json_t> facts;

                if (messages_.empty()) {
                    facts.push_back(glz::json_t::object_t{{"title", "Inbox:"}, {"value", "No messages found"}});
                } else {
                    for (const auto& msg : messages_) {
                        std::string title = msg.from.ends_with(':') ? msg.from : (msg.from + ":");
                        std::string value;
                        if (!msg.date_str.empty()) {
                            value = std::format("{} ({})", msg.subject, msg.date_str);
                        } else {
                            value = msg.subject;
                        }
                        facts.push_back(glz::json_t::object_t{
                            {"title", std::move(title)},
                            {"value", std::move(value)}
                        });
                    }
                }
                fact_set["facts"] = std::move(facts);
                body.push_back(std::move(fact_set));
            }

            // 4. Quick Compose / Reply Container
            {
                glz::json_t compose_container;
                compose_container["type"] = "Container";
                compose_container["separator"] = true;
                std::vector<glz::json_t> compose_items;

                glz::json_t title_tb;
                title_tb["type"] = "TextBlock";
                title_tb["text"] = "Quick Compose / Reply";
                title_tb["weight"] = "Bolder";
                compose_items.push_back(std::move(title_tb));

                glz::json_t to_input;
                to_input["type"] = "Input.Text";
                to_input["id"] = "reply_to";
                to_input["placeholder"] = "To: recipient@example.com";
                compose_items.push_back(std::move(to_input));

                glz::json_t subj_input;
                subj_input["type"] = "Input.Text";
                subj_input["id"] = "reply_subject";
                subj_input["placeholder"] = "Subject...";
                compose_items.push_back(std::move(subj_input));

                glz::json_t body_input;
                body_input["type"] = "Input.Text";
                body_input["id"] = "reply_body";
                body_input["placeholder"] = "Write your reply message...";
                body_input["isMultiline"] = true;
                compose_items.push_back(std::move(body_input));

                compose_container["items"] = std::move(compose_items);
                body.push_back(std::move(compose_container));
            }

            card["body"] = std::move(body);

            // Actions
            std::vector<glz::json_t> actions;
            {
                glz::json_t act;
                act["type"] = "Action.Execute";
                act["title"] = "Send Message";
                act["verb"] = "mail_send_reply";
                actions.push_back(std::move(act));
            }
            {
                glz::json_t act;
                act["type"] = "Action.Execute";
                act["title"] = "Refresh Inbox";
                act["verb"] = "mail_fetch";
                actions.push_back(std::move(act));
            }
            card["actions"] = std::move(actions);

            std::string out;
            auto ec = glz::write_json(card, out);
            if (ec) {
                return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"body\":[]}";
            }
            return out;
        }

        /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
        void handle_action(std::string_view action_json) override
        {
            if (action_json.empty()) return;
            try {
                glz::json_t payload{};
                std::string verb;
                glz::json_t data{};

                auto err = glz::read_json(payload, std::string(action_json));
                if (!err && payload.is_object()) {
                    if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                        verb = payload["verb"].get<std::string>();
                    } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                        verb = payload["action"].get<std::string>();
                    }

                    if (payload.contains("data") && payload["data"].is_object()) {
                        data = payload["data"];
                    } else {
                        data = payload;
                    }
                } else {
                    verb = std::string(action_json);
                }

                if (verb.empty()) return;
                last_action_ = verb;

                if (verb == "mail_fetch") {
                    if (data.is_object()) {
                        if (data.contains("mail_folder") && data["mail_folder"].holds<std::string>()) {
                            current_folder_ = data["mail_folder"].get<std::string>();
                            current_mailbox_ = current_folder_;
                        } else if (data.contains("folder") && data["folder"].holds<std::string>()) {
                            current_folder_ = data["folder"].get<std::string>();
                            current_mailbox_ = current_folder_;
                        }
                    }
                    messages_refreshed_ = true;
                    refresh_messages();
                } else if (verb == "mail_send_reply") {
                    if (data.is_object()) {
                        if (data.contains("reply_to") && data["reply_to"].holds<std::string>()) {
                            last_reply_to_ = data["reply_to"].get<std::string>();
                        } else if (data.contains("recipient") && data["recipient"].holds<std::string>()) {
                            last_reply_to_ = data["recipient"].get<std::string>();
                        } else if (data.contains("to") && data["to"].holds<std::string>()) {
                            last_reply_to_ = data["to"].get<std::string>();
                        }

                        if (data.contains("reply_subject") && data["reply_subject"].holds<std::string>()) {
                            last_reply_subject_ = data["reply_subject"].get<std::string>();
                        } else if (data.contains("subject") && data["subject"].holds<std::string>()) {
                            last_reply_subject_ = data["subject"].get<std::string>();
                        }

                        if (data.contains("reply_body") && data["reply_body"].holds<std::string>()) {
                            last_reply_body_ = data["reply_body"].get<std::string>();
                        } else if (data.contains("body") && data["body"].holds<std::string>()) {
                            last_reply_body_ = data["body"].get<std::string>();
                        }
                    }
                    sent_replies_count_++;
                } else if (verb == "mail_mark_read") {
                    std::string target_id;
                    if (data.is_object()) {
                        if (data.contains("message_id") && data["message_id"].holds<std::string>()) {
                            target_id = data["message_id"].get<std::string>();
                        } else if (data.contains("id") && data["id"].holds<std::string>()) {
                            target_id = data["id"].get<std::string>();
                        } else if (data.contains("uid") && data["uid"].holds<std::string>()) {
                            target_id = data["uid"].get<std::string>();
                        }
                    }
                    last_marked_message_id_ = target_id;
                    if (!target_id.empty()) {
                        for (auto& msg : messages_) {
                            if (msg.id == target_id) {
                                if (msg.is_unread) {
                                    msg.is_unread = false;
                                    if (unread_count_ > 0) unread_count_--;
                                }
                                break;
                            }
                        }
                    }
                } else if (verb == "mail_delete") {
                    std::string target_id;
                    if (data.is_object()) {
                        if (data.contains("message_id") && data["message_id"].holds<std::string>()) {
                            target_id = data["message_id"].get<std::string>();
                        } else if (data.contains("id") && data["id"].holds<std::string>()) {
                            target_id = data["id"].get<std::string>();
                        } else if (data.contains("uid") && data["uid"].holds<std::string>()) {
                            target_id = data["uid"].get<std::string>();
                        }
                    }
                    last_deleted_message_id_ = target_id;
                    if (!target_id.empty()) {
                        auto it = std::find_if(messages_.begin(), messages_.end(), [&target_id](const email_summary& m) {
                            return m.id == target_id;
                        });
                        if (it != messages_.end()) {
                            if (it->is_unread && unread_count_ > 0) {
                                unread_count_--;
                            }
                            messages_.erase(it);
                        }
                    }
                } else if (verb == "toggle_adaptive_view") {
                    adaptive_view_mode = !adaptive_view_mode;
                }
            } catch (const std::exception& e) {
                std::cerr << "[mail] Action error: " << e.what() << std::endl;
            }
        }

        [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
        void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }
        [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
        [[nodiscard]] const std::string& get_current_folder() const noexcept { return current_folder_; }
        [[nodiscard]] size_t get_unread_count() const noexcept { return unread_count_; }
        [[nodiscard]] const std::vector<email_summary>& get_messages() const noexcept { return messages_; }
        [[nodiscard]] const std::string& get_last_reply_to() const noexcept { return last_reply_to_; }
        [[nodiscard]] const std::string& get_last_reply_subject() const noexcept { return last_reply_subject_; }
        [[nodiscard]] const std::string& get_last_reply_body() const noexcept { return last_reply_body_; }
        [[nodiscard]] const std::string& get_last_marked_message_id() const noexcept { return last_marked_message_id_; }
        [[nodiscard]] const std::string& get_last_deleted_message_id() const noexcept { return last_deleted_message_id_; }
        [[nodiscard]] bool is_messages_refreshed() const noexcept { return messages_refreshed_; }
        [[nodiscard]] size_t get_sent_replies_count() const noexcept { return sent_replies_count_; }

        void add_message(email_summary msg) {
            if (msg.is_unread) {
                unread_count_++;
            }
            messages_.push_back(std::move(msg));
        }

        void sync_from_mail_screen()
        {
            if (!mail_screen_) return;
            const auto& msgs = mail_screen_->messages();
            if (msgs.empty()) return;

            messages_.clear();
            unread_count_ = 0;
            for (const auto& m : msgs) {
                auto date_str = std::format("{:%Y-%m-%d %H:%M}", m->date());
                messages_.push_back(email_summary{
                    .id = std::to_string(m->uid()),
                    .from = std::string(m->from()),
                    .subject = std::string(m->title()),
                    .date_str = date_str,
                    .is_unread = true,
                    .body = std::string(m->summary()),
                    .folder = current_folder_
                });
                unread_count_++;
            }
        }

        void refresh_messages()
        {
            stop_refresh_thread();
            last_refresh_ = std::chrono::steady_clock::now();
            messages_refreshed_ = true;

            if (username_.empty() || password_.empty()) {
                return;
            }

            // Start a new thread for refreshing
            refresh_thread_ = std::jthread([this] {
                execute_safely([this]() {
                    if (mail_screen_) {
                        mail_screen_->refresh();
                        sync_from_mail_screen();
                    }
                }, "Failed to refresh messages");
            });
        }

    private:
        // Member variables to store connection information for get_uri method
        std::string host_url_;
        std::string username_;
        std::string password_;

        // Adaptive Card presentation override state
        bool adaptive_view_mode{false};
        mutable helpers::adaptive_cards::parser adaptive_parser_{};
        mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
        mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
        mutable helpers::adaptive_cards::card_document adaptive_bound_{};
        std::string adaptive_error_{};
        std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

        // State tracking
        std::string current_folder_{"INBOX"};
        std::vector<email_summary> messages_{};
        size_t unread_count_{0};
        std::string last_action_{};
        std::string last_reply_to_{};
        std::string last_reply_subject_{};
        std::string last_reply_body_{};
        std::string last_marked_message_id_{};
        std::string last_deleted_message_id_{};
        bool messages_refreshed_{false};
        size_t sent_replies_count_{0};

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
                auto notify = registrar::try_get<std::function<void(const std::string&)>>("notify");
                if (notify && *notify) {
                    (*notify)(std::format("{}: {}", error_prefix, e.what()));
                }
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
                    auto notify = registrar::try_get<std::function<void(const std::string&)>>("notify");
                    if (notify && *notify) {
                        (*notify)(std::format("Connection attempt {}/{} failed: {}", 
                            attempt + 1, max_retries, e.what()));
                    }
                    
                    // Only sleep between retries, not after the last attempt
                    if (attempt < max_retries - 1) {
                        // Exponential backoff with jitter for better retry behavior
                        const auto base_delay = std::chrono::milliseconds(500 * (attempt + 1));
                        const auto jitter = std::chrono::milliseconds(std::rand() % 200);
                        std::this_thread::sleep_for(base_delay + jitter);
                    }
                }
            }
            
            auto notify = registrar::try_get<std::function<void(const std::string&)>>("notify");
            if (notify && *notify) {
                (*notify)(std::format("Failed to connect after {} attempts", max_retries));
            }
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
                                auto notify = registrar::try_get<std::function<void(const std::string&)>>("notify");
                                if (notify && *notify) {
                                    (*notify)(std::format("Failed to list mailboxes: {}", e.what()));
                                }
                                // Add default mailbox for fallback
                                mailboxes_.push_back("INBOX");
                            }
                        } else {
                            // Connection failed after retries, add default mailbox
                            mailboxes_.push_back("INBOX");
                        }
                    } catch (const std::exception& e) {
                        auto notify = registrar::try_get<std::function<void(const std::string&)>>("notify");
                        if (notify && *notify) {
                            (*notify)(std::format("Unexpected error loading mailboxes: {}", e.what()));
                        }
                        // Add default mailbox as fallback
                        mailboxes_.push_back("INBOX");
                    }
                }
                
                for (const auto& mailbox : mailboxes_) {
                    bool is_selected = (current_mailbox_ == mailbox);
                    if (ImGui::Selectable(mailbox.c_str(), is_selected)) {
                        if (current_mailbox_ != mailbox) {
                            current_mailbox_ = mailbox;
                            current_folder_ = mailbox;
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
