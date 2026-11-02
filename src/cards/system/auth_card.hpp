#pragma once

#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include "../../helpers/auth_service.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class auth_card : public card {
public:
    auth_card() {
        colors[0] = {0.18f, 0.45f, 0.68f, 1.0f};
        colors[1] = {0.12f, 0.32f, 0.50f, 0.75f};
        name("Operator SSO Identity");
        width = 440.0f;
    }

    [[nodiscard]] std::string get_uri() const override {
        return "auth";
    }

    [[nodiscard]] bool matches_uri(std::string_view uri) const override {
        return uri == "auth" || uri.starts_with("auth:");
    }

    bool render() override {
        return render_window([this]() {
            auto& auth = services::auth_service::instance();
            auto op = auth.get_active_operator();

            ImGui::TextColored(ImVec4{0.55f, 0.85f, 1.0f, 1.0f}, "OT / SCADA OPERATOR IDENTITY (OIDC / OAuth 2.0)");
            ImGui::Separator();

            if (op.has_value()) {
                auto const now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                int64_t const rem_sec = std::max<int64_t>(0, op->exp - now_epoch);
                int64_t const hrs = rem_sec / 3600;
                int64_t const mins = (rem_sec % 3600) / 60;

                ImGui::TextColored(ImVec4{0.25f, 0.85f, 0.45f, 1.0f}, "ACTIVE SHIFT OPERATOR");
                ImGui::Text("Name:  %s", op->name.c_str());
                ImGui::Text("Email: %s", op->email.c_str());
                ImGui::Text("Sub:   %s", op->sub.c_str());
                std::string roles_str;
                for (size_t i = 0; i < op->roles.size(); ++i) {
                    if (i > 0) roles_str += ", ";
                    roles_str += op->roles[i];
                }
                ImGui::Text("Roles: %s", roles_str.empty() ? "(none)" : roles_str.c_str());
                ImGui::Text("Shift Token Remaining: %02lldh %02lldm", static_cast<long long>(hrs), static_cast<long long>(mins));
                ImGui::Spacing();
                if (ImGui::Button("Lock / Shift Handover")) {
                    auth.lock_or_handover_session();
                }
            } else {
                ImGui::TextColored(ImVec4{0.95f, 0.65f, 0.20f, 1.0f}, "No Active Operator Session (Read-Only Telemetry Mode)");
                ImGui::Spacing();
                if (ImGui::Button("Sign In with Corporate SSO (OIDC + PKCE)")) {
                    std::string const url = auth.begin_pkce_authorization_url();
                    static_cast<void>(rouen::platform::open_url(url));
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Paste Offline / Kiosk JWT:");
                ImGui::InputText("##jwt_input", jwt_buf_, sizeof(jwt_buf_));
                ImGui::SameLine();
                if (ImGui::Button("Activate Token")) {
                    if (auth.set_active_session_jwt(jwt_buf_)) {
                        status_msg_ = "Operator session activated.";
                        jwt_buf_[0] = '\0';
                    } else {
                        status_msg_ = "JWT verification failed (expired, untrusted issuer, or invalid signature).";
                    }
                }
                if (!status_msg_.empty()) {
                    ImGui::TextWrapped("%s", status_msg_.c_str());
                }
            }
        });
    }

    [[nodiscard]] std::string get_adaptive_card_json() const override {
        return services::auth_service::instance().to_status_json();
    }

private:
    char jwt_buf_[4096]{};
    std::string status_msg_;
};

} // namespace rouen::cards
