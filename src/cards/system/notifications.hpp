#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/notify_service.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct notifications_card : public card {
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
        width = 900.0f;
        requested_fps = 4;
    }

    std::string get_uri() const override {
        return "notifications";
    }

    bool render() override {
        return render_window([this]() {
            render_content();
        });
    }

private:
    std::optional<std::uint64_t> selected_notification_id_;
    std::string status_message_;
    double status_message_expires_at_ = 0.0;

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

        float available_width = ImGui::GetContentRegionAvail().x;
        float left_width = std::max(260.0f, available_width * 0.38f);

        if (ImGui::BeginChild("notifications_list", ImVec2(left_width, 0), true)) {
            render_notifications_list(notifications);
        }
        ImGui::EndChild();

        ImGui::SameLine();

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
                ImGui::SameLine();
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

        if (ImGui::Button("Copy to Clipboard")) {
            ImGui::SetClipboardText(selected_it->message.c_str());
            status_message_ = "Copied to clipboard!";
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

} // namespace rouen::cards
