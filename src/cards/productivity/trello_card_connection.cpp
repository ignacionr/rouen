#include "trello_card.hpp"

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <chrono>
#include <format>

// 2. Libraries used in the project, in alphabetic order
// None

// 3. All other includes
#include "../../helpers/api_keys.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

void trello_card::render_connection_screen() {
    ImGui::TextColored(colors[0], ICON_MD_DASHBOARD " Connect to Trello");
    ImGui::Separator();
    
    if (!connection_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[2]); // Error color
        ImGui::TextWrapped("%s", connection_error_.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    
    // Try environment connection first
    if (ImGui::Button("Connect from Environment", ImVec2(200, 0))) {
        try_environment_connection();
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
    
    if (ImGui::Button("Connect", ImVec2(100, 0))) {
        attempt_connection();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Save Profile", ImVec2(100, 0))) {
        if (validate_connection_form()) {
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

void trello_card::attempt_connection() {
    if (!validate_connection_form()) return;
    
    try {
        std::string profile_name = strlen(profile_name_buffer_) > 0 ? profile_name_buffer_ : "Manual Connection";
        bool success = trello_host_->connect_with_credentials(api_key_buffer_, token_buffer_, profile_name);
        
        if (success) {
            clear_error();
            reset_ui_state();
        } else {
            show_error("Failed to connect with provided credentials");
        }
    } catch (const std::exception& e) {
        show_error(std::format("Connection error: {}", e.what()));
    }
}

void trello_card::try_environment_connection() {
    try {
        bool success = trello_host_->connect_from_environment();
        if (success) {
            clear_error();
            reset_ui_state();
        } else {
            show_error("Environment variables not found or invalid");
        }
    } catch (const std::exception& e) {
        show_error(std::format("Environment connection error: {}", e.what()));
    }
}

bool trello_card::validate_connection_form() {
    if (strlen(api_key_buffer_) == 0) {
        show_error("API Key is required");
        return false;
    }
    if (strlen(token_buffer_) == 0) {
        show_error("Token is required");
        return false;
    }
    return true;
}

} // namespace rouen::cards
