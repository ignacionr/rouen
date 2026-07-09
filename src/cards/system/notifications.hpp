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
            std::string label = std::format("{}  {}", entry.timestamp, entry.preview);
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
};

} // namespace rouen::cards
