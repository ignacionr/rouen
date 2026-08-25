#include "trello_card.hpp"

#include <cstring>
#include <exception>
#include <format>
#include "helpers/imgui_include.hpp"
#include "IconsMaterialDesign.h"
#include "trello_model.hpp"

namespace rouen::cards {

void trello_card::render_connection_screen() {
    ImGui::TextColored(colors[0], ICON_MD_DASHBOARD " Connect to Trello");
    ImGui::Separator();
    
    if (!connection_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
        ImGui::TextWrapped("%s", connection_error_.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    
    auto try_env = [this]() {
        try {
            bool const success = trello_host_->connect_from_environment();
            if (success) {
                clear_error();
                reset_ui_state();
            } else {
                show_error("Environment variables not found or invalid");
            }
        } catch (const std::exception& e) {
            show_error(std::format("Environment connection error: {}", e.what()));
        }
    };

    if (ImGui::Button("Connect from Environment", ImVec2(200, 0))) {
        try_env();
    }
    ImGui::SameLine();
    ImGui::TextColored(colors[5], "Uses TRELLO_API_KEY and TRELLO_TOKEN");
    
    ImGui::Separator();
    
    render_saved_profiles();
    ImGui::Separator();
    render_connection_form();
}

void trello_card::render_connection_form() {
    ImGui::Text("Manual Connection:");
    
    ImGui::Text("Profile Name:");
    ImGui::InputText("##profile_name", profile_name_buffer_, sizeof(profile_name_buffer_));
    
    ImGui::Text("API Key:");
    ImGui::InputText("##api_key", api_key_buffer_, sizeof(api_key_buffer_));
    
    ImGui::Text("Token:");
    ImGui::InputText("##token", token_buffer_, sizeof(token_buffer_), ImGuiInputTextFlags_Password);
    
    ImGui::Separator();

    auto validate_form = [this]() -> bool {
        if (strlen(api_key_buffer_) == 0) {
            show_error("API Key is required");
            return false;
        }
        if (strlen(token_buffer_) == 0) {
            show_error("Token is required");
            return false;
        }
        return true;
    };

    auto attempt = [this, validate_form]() {
        if (!validate_form()) return;
        
        try {
            std::string const profile_name = strlen(profile_name_buffer_) > 0 ? profile_name_buffer_ : "Manual Connection";
            bool const success = trello_host_->connect_with_credentials(api_key_buffer_, token_buffer_, profile_name);
            
            if (success) {
                clear_error();
                reset_ui_state();
            } else {
                show_error("Failed to connect with provided credentials");
            }
        } catch (const std::exception& e) {
            show_error(std::format("Connection error: {}", e.what()));
        }
    };
    
    if (ImGui::Button("Connect", ImVec2(100, 0))) {
        attempt();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Save Profile", ImVec2(100, 0))) {
        if (validate_form()) {
            models::trello::trello_connection_profile profile;
            profile.name = strlen(profile_name_buffer_) > 0 ? profile_name_buffer_ : "Manual Connection";
            profile.api_key = api_key_buffer_;
            profile.token = token_buffer_;
            profile.is_environment = false;
            
            trello_host_->save_profile(profile);
            clear_error();
        }
    }
}

void trello_card::render_saved_profiles() {
    auto profiles = trello_host_->get_saved_profiles();
    if (profiles.empty()) {
        return;
    }
    
    ImGui::Text("Saved Profiles:");
    
    for (const auto& profile : profiles) {
        ImGui::PushID(profile.name.c_str());
        
        if (ImGui::Button(profile.name.c_str(), ImVec2(150, 0))) {
            try {
                trello_host_->connect_with_credentials(profile.api_key, profile.token, profile.name);
                clear_error();
                reset_ui_state();
            } catch (const std::exception& e) {
                show_error(std::format("Failed to connect with profile '{}': {}", profile.name, e.what()));
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(60, 0))) {
            trello_host_->delete_profile(profile.name);
        }
        
        ImGui::PopID();
    }
}

} // namespace rouen::cards
