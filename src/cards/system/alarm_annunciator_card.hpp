#pragma once

#include <format>
#include <string>
#include <string_view>

#include "../../helpers/alarm_manager.hpp"
#include "../../helpers/auth_service.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class alarm_annunciator_card : public card {
public:
    alarm_annunciator_card() {
        colors[0] = {0.65f, 0.16f, 0.16f, 1.0f};
        colors[1] = {0.42f, 0.10f, 0.10f, 0.75f};
        name("ISA-18.2 Alarm Annunciator");
        width = 620.0f;
    }

    [[nodiscard]] std::string get_uri() const override {
        return "alarms";
    }

    [[nodiscard]] bool matches_uri(std::string_view uri) const override {
        return uri == "alarms" || uri.starts_with("alarms:");
    }

    bool render() override {
        return render_window([this]() {
            auto& mgr = helpers::alarm_manager::instance();
            auto const alarms = mgr.list_alarms(show_normal_);
            auto const op = services::auth_service::current_effective_operator();
            std::string const op_email = op.has_value() ? op->email : "console-operator";

            ImGui::Checkbox("Show Cleared (NORMAL)", &show_normal_);
            ImGui::Separator();

            if (alarms.empty()) {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "NO ACTIVE PROCESS ALARMS (ALL NORMAL)");
                return;
            }

            if (ImGui::BeginTable("Isa182AlarmTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableSetupColumn("Alarm ID & Title", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Ack By", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 175.0f);
                ImGui::TableHeadersRow();

                for (const auto& alm : alarms) {
                    ImGui::TableNextRow();
                    ImGui::PushID(alm.id.c_str());

                    ImGui::TableSetColumnIndex(0);
                    ImVec4 state_col{0.75f, 0.75f, 0.75f, 1.0f};
                    if (alm.state == "UNACK_ALM") state_col = ImVec4{1.0f, 0.25f, 0.25f, 1.0f};
                    else if (alm.state == "ACK_ALM") state_col = ImVec4{0.95f, 0.75f, 0.20f, 1.0f};
                    else if (alm.state == "UNACK_RTN") state_col = ImVec4{0.35f, 0.75f, 1.0f, 1.0f};
                    else if (alm.state == "SHELVED") state_col = ImVec4{0.65f, 0.55f, 0.85f, 1.0f};
                    ImGui::TextColored(state_col, "%s", alm.state.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(alm.severity.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s (%s)", alm.title.c_str(), alm.id.c_str());

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(alm.value.c_str());

                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(alm.acknowledged_by.empty() ? "-" : alm.acknowledged_by.c_str());

                    ImGui::TableSetColumnIndex(5);
                    if (alm.state == "UNACK_ALM" || alm.state == "UNACK_RTN") {
                        if (ImGui::SmallButton("Ack")) {
                            mgr.acknowledge(alm.id, op_email);
                        }
                        ImGui::SameLine();
                    }
                    if (alm.state != "SHELVED" && alm.state != "NORMAL") {
                        if (ImGui::SmallButton("Shelve 15m")) {
                            mgr.shelve(alm.id, 900, "Operator shelved 15m", op_email);
                        }
                        ImGui::SameLine();
                    }
                    if (!alm.source_uri.empty() && ImGui::SmallButton("Asset")) {
                        try {
                            (*registrar::get<std::function<void(std::string)>>("create_card"))(alm.source_uri);
                        } catch (...) {}
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        });
    }

    [[nodiscard]] std::string get_adaptive_card_json() const override {
        auto const alarms = helpers::alarm_manager::instance().list_alarms(true);
        glz::json_t card = glz::json_t::object_t{};
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        glz::json_t body = glz::json_t::array_t{};
        glz::json_t header = glz::json_t::object_t{};
        header["type"] = "TextBlock";
        header["text"] = "ISA-18.2 / API RP 1167 Alarm Annunciator";
        header["weight"] = "Bolder";
        header["size"] = "Medium";
        body.get<glz::json_t::array_t>().push_back(std::move(header));

        for (const auto& alm : alarms) {
            glz::json_t badge = glz::json_t::object_t{};
            badge["type"] = "Rouen.StatusBadge";
            badge["label"] = std::format("[{}] {}", alm.state, alm.title);
            badge["text"] = alm.value;
            badge["severity"] = alm.severity;
            body.get<glz::json_t::array_t>().push_back(std::move(badge));
        }
        card["body"] = std::move(body);
        std::string out = "{}";
        (void)glz::write_json(card, out);
        return out;
    }

    void handle_action(std::string_view action_json) override {
        glz::json_t doc{};
        if (glz::read_json(doc, action_json) || !doc.is_object()) {
            return;
        }
        auto const& obj = doc.get<glz::json_t::object_t>();
        std::string verb;
        std::string alarm_id;
        if (auto it = obj.find("verb"); it != obj.end() && it->second.is_string()) {
            verb = it->second.get<std::string>();
        }
        if (auto it = obj.find("alarm_id"); it != obj.end() && it->second.is_string()) {
            alarm_id = it->second.get<std::string>();
        }
        auto const op = services::auth_service::current_effective_operator();
        std::string const op_email = op.has_value() ? op->email : "console-operator";
        if (verb == "ack_alarm" && !alarm_id.empty()) {
            helpers::alarm_manager::instance().acknowledge(alarm_id, op_email);
        } else if (verb == "shelve_alarm" && !alarm_id.empty()) {
            helpers::alarm_manager::instance().shelve(alarm_id, 900, "Shelved via action", op_email);
        }
    }

private:
    bool show_normal_{false};
};

} // namespace rouen::cards
