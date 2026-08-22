#pragma once

#include <cstring>
#include <memory>
#include <string>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../../external/IconsMaterialDesign.h"

#include "../interface/card.hpp"
#include "../../hosts/footprints_host.hpp"

namespace rouen::cards {
    class footprints_card : public card {
    public:
        footprints_card() : host_(rouen::hosts::get_footprints_host()) {
            name("FootPrints");
            colors[0] = {0.15f, 0.45f, 0.65f, 1.0f};
            colors[1] = {0.25f, 0.55f, 0.75f, 0.7f};
            width = 320.0f;

            if (host_->has_saved_profile()) {
                std::strncpy(url_buffer_, host_->base_url().c_str(), sizeof(url_buffer_) - 1);
                std::strncpy(user_buffer_, host_->username().c_str(), sizeof(user_buffer_) - 1);
            } else {
                std::strncpy(url_buffer_, "http://fp.raptor.local", sizeof(url_buffer_) - 1);
            }
        }

        bool render() override {
            return render_window([this]() {
                if (host_->is_connected()) {
                    render_connected();
                } else {
                    render_login();
                }
            });
        }

        std::string get_uri() const override { return "footprints"; }

    private:
        void render_connected() {
            ImGui::TextColored(colors[0], ICON_MD_CONFIRMATION_NUMBER " FootPrints");
            ImGui::Separator();
            ImGui::Text("Connected as:");
            ImGui::TextColored(colors[0], "%s", host_->username().c_str());
            ImGui::TextWrapped("%s", host_->base_url().c_str());
            ImGui::Spacing();

            if (ImGui::Button("Open in Browser", ImVec2(-1, 0))) {
                rouen::platform::open_url(host_->homepage_url());
            }

            ImGui::Spacing();
            if (ImGui::Button("Log Out", ImVec2(-1, 0))) {
                host_->logout();
                std::memset(pass_buffer_, 0, sizeof(pass_buffer_));
            }
        }

        void render_login() {
            ImGui::TextColored(colors[0], ICON_MD_CONFIRMATION_NUMBER " Connect to FootPrints");
            ImGui::Separator();

            std::string const error{host_->get_last_error()};
            if (!error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                ImGui::TextWrapped("%s", error.c_str());
                ImGui::PopStyleColor();
                ImGui::Separator();
            }

            ImGui::Text("Server URL:");
            ImGui::InputText("##fp_url", url_buffer_, sizeof(url_buffer_));

            ImGui::Text("Username:");
            ImGui::InputText("##fp_user", user_buffer_, sizeof(user_buffer_));

            ImGui::Text("Password:");
            ImGui::InputText("##fp_pass", pass_buffer_, sizeof(pass_buffer_), ImGuiInputTextFlags_Password);

            ImGui::Checkbox("Remember me on this computer", &remember_);
            ImGui::TextColored(colors[1], "Only a session token is saved, never the password.");

            ImGui::Spacing();
            if (ImGui::Button("Log In", ImVec2(-1, 0))) {
                if (host_->login(url_buffer_, user_buffer_, pass_buffer_, remember_)) {
                    std::memset(pass_buffer_, 0, sizeof(pass_buffer_));
                }
            }
        }

        std::shared_ptr<rouen::hosts::footprints_host> host_;
        char url_buffer_[256]{0};
        char user_buffer_[64]{0};
        char pass_buffer_[128]{0};
        bool remember_{true};
    };
}
