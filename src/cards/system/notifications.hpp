#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/notify_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct notifications_card : public card {
    struct notification_entry {
        std::string id;
        std::string severity{"INFO"};
        std::string title;
        std::string message;
        std::string timestamp;
        bool is_unread{true};
    };

    notifications_card() {
        colors[0] = {0.35f, 0.32f, 0.58f, 1.0f};
        colors[1] = {0.50f, 0.45f, 0.78f, 0.75f};
        get_color(2, {0.76f, 0.72f, 0.95f, 1.0f});
        get_color(3, {0.18f, 0.17f, 0.28f, 0.92f});
        get_color(4, {0.27f, 0.25f, 0.42f, 0.70f});
        get_color(5, {0.87f, 0.87f, 0.95f, 1.0f});
        get_color(6, {0.55f, 0.85f, 0.55f, 1.0f});
        get_color(7, {0.95f, 0.70f, 0.35f, 1.0f});

        name("Notifications");
        width = 450.0f;
        requested_fps = 4;

        // Default sample notifications matching the specification
        notifications_ = {
            {"notif-1", "INFO", "Build", "Target 'rouen' compiled successfully in 12.4s (Today 16:50)", "Today 16:50", true},
            {"notif-2", "WARN", "Memory", "Host memory usage reached 12.8 GB / 16.0 GB (Today 16:40)", "Today 16:40", true}
        };
        unread_count_ = 2;
        severity_filter_ = "ALL";
    }

    ~notifications_card() override = default;

    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override;

    [[nodiscard]] std::string get_uri() const override {
        return "notifications";
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "notifications";
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] size_t get_unread_count() const noexcept { return unread_count_; }
    void set_unread_count(size_t count) noexcept { unread_count_ = count; }
    [[nodiscard]] const std::string& get_severity_filter() const noexcept { return severity_filter_; }
    void set_severity_filter(std::string filter) noexcept { severity_filter_ = std::move(filter); }
    [[nodiscard]] const std::vector<notification_entry>& get_notifications() const noexcept { return notifications_; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::string& get_last_dismissed_id() const noexcept { return last_dismissed_id_; }
    [[nodiscard]] const std::string& get_last_test_message() const noexcept { return last_test_message_; }
    [[nodiscard]] const std::string& get_last_test_severity() const noexcept { return last_test_severity_; }

private:
    std::optional<std::uint64_t> selected_notification_id_;
    std::string status_message_;
    double status_message_expires_at_ = 0.0;

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // State Synchronization
    std::vector<notification_entry> notifications_{};
    size_t unread_count_{0};
    std::string severity_filter_{"ALL"};
    std::string last_action_{};
    std::string last_dismissed_id_{};
    std::string last_test_message_{};
    std::string last_test_severity_{};
    uint64_t next_notif_id_{3};

    void render_content() {
        auto notifications = notify_service::history_snapshot();
        std::sort(notifications.begin(), notifications.end(), [](const auto& a, const auto& b) {
            return a.id > b.id;
        });
        reconcile_selection(notifications);

        render_header(notifications.size());
        ImGui::Separator();

        if (notifications.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(get_color(5), "No notifications captured yet.");
            ImGui::TextWrapped("Notifications emitted through the existing \"notify\" sink will appear here.");
            return;
        }

        float available_height = ImGui::GetContentRegionAvail().y;
        float list_height = std::max(120.0f, available_height * 0.40f);

        if (ImGui::BeginChild("notifications_list", ImVec2(0, list_height), true)) {
            render_notifications_list(notifications);
        }
        ImGui::EndChild();

        if (ImGui::BeginChild("notification_detail", ImVec2(0, 0), true)) {
            render_notification_detail(notifications);
        }
        ImGui::EndChild();
    }

    void render_header(std::size_t notification_count) {
        ImGui::Text("Notification Center");
        ImGui::SameLine();
        ImGui::TextColored(get_color(2), "(%zu stored)", notification_count);

        bool spoken_enabled = notify_service::spoken_notifications_enabled();
        int mode = spoken_enabled ? 0 : 1;

        ImGui::Spacing();
        ImGui::Text("Delivery:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Spoken", mode == 0)) {
            update_spoken_mode(true);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Silent", mode == 1)) {
            update_spoken_mode(false);
        }

        if (!status_message_.empty()) {
            if (ImGui::GetTime() > status_message_expires_at_) {
                status_message_.clear();
            } else {
                ImGui::TextColored(get_color(6), "%s", status_message_.c_str());
            }
        }
    }

    void render_notifications_list(const std::vector<notify_service::notification_entry>& notifications) {
        for (const auto& entry : notifications) {
            ImGui::PushID(static_cast<int>(entry.id));

            bool selected = selected_notification_id_.has_value() && selected_notification_id_.value() == entry.id;
            std::string label = entry.preview;
            if (ImGui::Selectable(label.c_str(), selected)) {
                selected_notification_id_ = entry.id;
            }

            if (ImGui::IsItemHovered() && entry.preview != entry.message) {
                ImGui::SetTooltip("%s", entry.preview.c_str());
            }

            ImGui::PopID();
        }
    }

    void render_notification_detail(const std::vector<notify_service::notification_entry>& notifications) {
        auto selected_it = std::find_if(notifications.begin(), notifications.end(), [this](const auto& entry) {
            return selected_notification_id_.has_value() && selected_notification_id_.value() == entry.id;
        });

        if (selected_it == notifications.end()) {
            ImGui::TextColored(get_color(5), "Select a notification to inspect it.");
            return;
        }

        ImGui::TextColored(get_color(2), "%s", selected_it->preview.c_str());
        ImGui::Separator();
        ImGui::Text("Received: %s", selected_it->timestamp.c_str());
        ImGui::Spacing();

        if (ImGui::Button(ICON_MD_CONTENT_COPY " Copy to Clipboard")) {
            ImGui::SetClipboardText(selected_it->message.c_str());
            status_message_ = "Copied to clipboard!";
            status_message_expires_at_ = ImGui::GetTime() + 4.0;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_VOLUME_UP " Read Out Loud")) {
            notify_service::speak_notification(selected_it->message);
            status_message_ = "Reading notification out loud...";
            status_message_expires_at_ = ImGui::GetTime() + 4.0;
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Full text:");
        ImGui::Spacing();

        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(selected_it->message.c_str());
        ImGui::PopTextWrapPos();
    }

    void reconcile_selection(const std::vector<notify_service::notification_entry>& notifications) {
        if (notifications.empty()) {
            selected_notification_id_.reset();
            return;
        }

        if (!selected_notification_id_.has_value()) {
            selected_notification_id_ = notifications.front().id;
            return;
        }

        bool selection_still_exists = std::any_of(notifications.begin(), notifications.end(), [this](const auto& entry) {
            return selected_notification_id_.has_value() && selected_notification_id_.value() == entry.id;
        });

        if (!selection_still_exists) {
            selected_notification_id_ = notifications.front().id;
        }
    }

    void update_spoken_mode(bool enabled) {
        if (notify_service::set_spoken_notifications_enabled(enabled)) {
            status_message_ = enabled ? "Spoken notifications enabled" : "Silent notifications enabled";
        } else {
            status_message_ = "Failed to save notification preference";
        }
        status_message_expires_at_ = ImGui::GetTime() + 4.0;
    }

private:
    float anim_y = 1080.0f;
    float scroll_x = 1920.0f;
    double last_time = 0.0;
    std::uint64_t last_seen_notification_id = 0;

    bool has_video_overlay() const override { return true; }

    void render_video_ui() override {
        auto now_system = std::chrono::system_clock::now();
        std::vector<notify_service::notification_entry> active_notifications;
        auto history = notify_service::history_snapshot();
        for (const auto& entry : history) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now_system - entry.created_at).count();
            if (age <= 60) {
                active_notifications.push_back(entry);
            }
        }
        std::reverse(active_notifications.begin(), active_notifications.end());

        double current_time = ImGui::GetTime();
        float dt = (last_time == 0.0) ? 0.016f : static_cast<float>(current_time - last_time);
        last_time = current_time;

        std::string ticker_text;
        for (size_t i = 0; i < active_notifications.size(); ++i) {
            if (i > 0) ticker_text += "    " ICON_MD_FIBER_MANUAL_RECORD "    ";
            ticker_text += active_notifications[i].preview;
        }

        float disp_w = (ImGui::GetIO().DisplaySize.x > 0.0f) ? ImGui::GetIO().DisplaySize.x : 1920.0f;
        float disp_h = (ImGui::GetIO().DisplaySize.y > 0.0f) ? ImGui::GetIO().DisplaySize.y : 1080.0f;

        std::uint64_t newest_id = active_notifications.empty() ? 0 : active_notifications.back().id;
        if (newest_id > last_seen_notification_id) {
            last_seen_notification_id = newest_id;
            scroll_x = disp_w; // Scroll in new notification from the right side
        }

        float target_y = active_notifications.empty() ? disp_h : (disp_h - 100.0f); // 100px height band
        
        // Slide up/down animation
        anim_y += (target_y - anim_y) * 8.0f * dt;
        if (active_notifications.empty()) {
            if (anim_y > disp_h - 0.5f) {
                anim_y = disp_h;
                scroll_x = disp_w;
            }
        } else {
            if (anim_y < disp_h - 99.5f) anim_y = disp_h - 100.0f;
        }

        if (anim_y < disp_h) {
            // Calculate text scrolling offset
            float text_width = ImGui::CalcTextSize(ticker_text.c_str()).x;
            scroll_x -= 150.0f * dt; // 150 pixels per second speed
            
            // Allow buffer space before resetting scroll
            if (scroll_x < -(text_width + 100.0f)) {
                scroll_x = disp_w;
            }

            ImGui::SetNextWindowPos(ImVec2(0.0f, anim_y));
            ImGui::SetNextWindowSize(ImVec2(disp_w, 100.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 15.0f));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.06f, 0.94f));

            if (ImGui::Begin("##NotificationVideoOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse)) {
                // Top border accent line using the notification card's primary theme color
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddLine(
                    ImVec2(0.0f, anim_y),
                    ImVec2(disp_w, anim_y),
                    ImGui::ColorConvertFloat4ToU32(colors[0]),
                    4.0f
                );

                // Badge + Ticker Area
                ImGui::SetCursorPos(ImVec2(24.0f, 32.0f));
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextColored(colors[0], "%s NOTIFICATIONS", ICON_MD_NOTIFICATIONS_ACTIVE);

                // Elegant vertical divider line separating the static badge from the moving news text
                draw_list->AddLine(
                    ImVec2(250.0f, anim_y + 16.0f),
                    ImVec2(250.0f, anim_y + 84.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.5f, 0.4f)),
                    2.0f
                );

                // News ticker clip region
                ImGui::PushClipRect(ImVec2(270.0f, anim_y), ImVec2(disp_w - 20.0f, anim_y + 100.0f), true);

                ImGui::SetCursorPos(ImVec2(scroll_x, 32.0f));
                ImGui::SetWindowFontScale(2.2f); // Double-size text ticker font scale
                ImGui::TextUnformatted(ticker_text.c_str());

                ImGui::PopClipRect();
            }
            ImGui::End();

            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(3);
        }
    }
};

inline bool notifications_card::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now;
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

        render_content();
    });
}

inline std::string notifications_card::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::vector<glz::json_t> body;

    // Header Container
    {
        glz::json_t header_container;
        header_container["type"] = "Container";
        header_container["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> columns;

        // Column 1: Bell icon
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> items;

            glz::json_t tb_icon;
            tb_icon["type"] = "TextBlock";
            tb_icon["text"] = "\U0001F514";
            tb_icon["size"] = "Large";
            items.push_back(std::move(tb_icon));

            col["items"] = std::move(items);
            columns.push_back(std::move(col));
        }

        // Column 2: Title and subtitle
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "stretch";
            std::vector<glz::json_t> items;

            glz::json_t tb_title;
            tb_title["type"] = "TextBlock";
            tb_title["text"] = "System Notifications History";
            tb_title["weight"] = "Bolder";
            tb_title["size"] = "Large";
            items.push_back(std::move(tb_title));

            glz::json_t tb_sub;
            tb_sub["type"] = "TextBlock";
            tb_sub["text"] = "Desktop alert queue and security logs";
            tb_sub["isSubtle"] = true;
            tb_sub["size"] = "Small";
            items.push_back(std::move(tb_sub));

            col["items"] = std::move(items);
            columns.push_back(std::move(col));
        }

        // Column 3: Unread count badge
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> items;

            glz::json_t tb_badge;
            tb_badge["type"] = "TextBlock";
            tb_badge["text"] = std::to_string(unread_count_) + " NEW";
            tb_badge["weight"] = "Bolder";
            tb_badge["color"] = "Attention";
            tb_badge["size"] = "Small";
            items.push_back(std::move(tb_badge));

            col["items"] = std::move(items);
            columns.push_back(std::move(col));
        }

        col_set["columns"] = std::move(columns);
        header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
        body.push_back(std::move(header_container));
    }

    // FactSet
    {
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        for (const auto& notif : notifications_) {
            if (severity_filter_ != "ALL" && !severity_filter_.empty() && notif.severity != severity_filter_) {
                continue;
            }
            std::string title;
            if (notif.title.starts_with("[")) {
                title = notif.title;
            } else {
                title = "[" + notif.severity + "] " + notif.title + ":";
            }
            if (!title.ends_with(':')) {
                title += ":";
            }
            facts.push_back(glz::json_t::object_t{
                {"title", title},
                {"value", notif.message}
            });
        }

        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));
    }

    card["body"] = std::move(body);

    // Actions
    {
        std::vector<glz::json_t> actions;

        glz::json_t act_clear;
        act_clear["type"] = "Action.Execute";
        act_clear["title"] = "Clear All Notifications";
        act_clear["verb"] = "notif_clear_all";
        actions.push_back(std::move(act_clear));

        glz::json_t act_test;
        act_test["type"] = "Action.Execute";
        act_test["title"] = "Send Test Alert";
        act_test["verb"] = "notif_test";
        actions.push_back(std::move(act_test));

        card["actions"] = std::move(actions);
    }

    std::string json_str;
    auto err = glz::write_json(card, json_str);
    if (err) {
        return "{}";
    }
    return json_str;
}

inline void notifications_card::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        std::string verb;
        glz::json_t data{};
        glz::json_t payload{};

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

        if (verb == "notif_clear_all") {
            last_action_ = "notif_clear_all";
            notifications_.clear();
            unread_count_ = 0;
            last_adaptive_parse_time_ = {};
        } else if (verb == "notif_dismiss") {
            last_action_ = "notif_dismiss";
            std::string target_id;
            if (data.is_object() && data.contains("id")) {
                if (data["id"].holds<std::string>()) {
                    target_id = data["id"].get<std::string>();
                } else if (data["id"].holds<double>()) {
                    target_id = std::to_string(static_cast<uint64_t>(data["id"].get<double>()));
                }
            }
            last_dismissed_id_ = target_id;
            auto it = std::find_if(notifications_.begin(), notifications_.end(), [&](const auto& n) {
                return n.id == target_id;
            });
            if (it != notifications_.end()) {
                if (it->is_unread && unread_count_ > 0) {
                    unread_count_--;
                }
                notifications_.erase(it);
            }
            last_adaptive_parse_time_ = {};
        } else if (verb == "notif_test") {
            last_action_ = "notif_test";
            std::string severity = "INFO";
            std::string message = "Sample desktop toast notification";
            std::string title = "Alert";

            if (data.is_object()) {
                if (data.contains("severity") && data["severity"].holds<std::string>()) {
                    severity = data["severity"].get<std::string>();
                }
                if (data.contains("message") && data["message"].holds<std::string>()) {
                    message = data["message"].get<std::string>();
                }
                if (data.contains("title") && data["title"].holds<std::string>()) {
                    title = data["title"].get<std::string>();
                }
            }

            last_test_severity_ = severity;
            last_test_message_ = message;

            std::string id = "notif-" + std::to_string(next_notif_id_++);
            notifications_.push_back(notification_entry{
                .id = id,
                .severity = severity,
                .title = title,
                .message = message,
                .timestamp = "Just now",
                .is_unread = true
            });
            unread_count_++;

            // Trigger desktop notification sink if registered
            auto notify_fn = registrar::try_get<std::function<void(const std::string&)>>("notify");
            if (notify_fn) {
                (*notify_fn)(message);
            }

            last_adaptive_parse_time_ = {};
        } else if (verb == "toggle_adaptive_view") {
            last_action_ = "toggle_adaptive_view";
            adaptive_view_mode = !adaptive_view_mode;
            last_adaptive_parse_time_ = {};
        } else if (verb == "set_severity_filter" || verb == "filter_severity") {
            last_action_ = verb;
            if (data.is_object()) {
                if (data.contains("severity") && data["severity"].holds<std::string>()) {
                    severity_filter_ = data["severity"].get<std::string>();
                } else if (data.contains("filter") && data["filter"].holds<std::string>()) {
                    severity_filter_ = data["filter"].get<std::string>();
                }
            }
            last_adaptive_parse_time_ = {};
        }
    } catch (const std::exception& e) {
        std::cerr << "[notifications] Action error: " << e.what() << std::endl;
    } catch (...) {}
}

} // namespace rouen::cards
