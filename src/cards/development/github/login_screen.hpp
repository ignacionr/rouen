#pragma once

#include <string>

#include <imgui.h>

#include "../../../models/github/login_host.hpp"
#include "../../../../external/IconsMaterialDesign.h"

namespace rouen::cards::github {
    struct login_screen {
        login_screen(models::github::login_host& host): host_{host} {}
        
        bool render() noexcept {
            if (personal_token_.reserve(256); ImGui::InputText("Personal Token", personal_token_.data(), personal_token_.capacity())) {
                personal_token_ = personal_token_.data();
            }
            
            if (ImGui::Button(ICON_MD_SAVE " Save")) {
                host_.set_personal_token(personal_token_);
                return true;
            }
            
            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_HELP " Help")) {
                // Show help info on how to create a GitHub personal token
                show_help_ = !show_help_;
            }
            
            if (show_help_) {
                ImGui::Separator();
                ImGui::TextWrapped("GitHub requires a Personal Access Token for API access:");
                ImGui::BulletText("Go to GitHub.com and sign in");
                ImGui::BulletText("Click your profile picture > Settings");
                ImGui::BulletText("Navigate to Developer Settings > Personal access tokens > Fine-grained tokens");
                ImGui::BulletText("Click \"Generate new token\"");
                ImGui::BulletText("Set permissions: repo (all) and workflow");
                ImGui::BulletText("Copy the token and paste it here");
            }
            
            return false;
        }
        
    private:
        models::github::login_host& host_;
        std::string personal_token_;
        bool show_help_{false};
    };
}