#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <chrono>
#include <format>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include "../../helpers/debug.hpp"
#include "../../../external/IconsMaterialDesign.h"
#include "jira_ui_components.hpp"

namespace rouen::cards {

class jira_connection_handler {
public:
    jira_connection_handler() {
        // Initialize the JIRA model
        jira_host_ = std::make_shared<models::jira_model>();
        
        // Load available profiles
        refresh_profiles();
        
        // Check for direct connection via environment variables
        auto env_profiles = jira_host_->detect_environment_profiles();
        if (!env_profiles.empty()) {
            // Try to connect using the first environment profile
            try_connect(env_profiles[0]);
        }
    }
    
    // Render the connection screen
    void render_connection_screen() {
        ImGui::TextColored(get_color(0), ICON_MD_CLOUD_QUEUE " Connect to Jira");
        ImGui::Separator();
        
        // Error message if login failed
        if (!error_message_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, get_color(2)); // Error color
            ImGui::TextWrapped("%s", error_message_.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        
        // Profiles selection
        render_saved_profiles();
        ImGui::Separator();
        
        // Manual connection form
        render_connection_form();
    }
    
    // Connection getters and helpers
    bool is_connected() const { return jira_host_->is_connected(); }
    void disconnect() { jira_host_->disconnect(); }
    std::string get_error() const { return error_message_; }
    std::shared_ptr<models::jira_model> get_jira_host() const { return jira_host_; }
    
private:
    void render_saved_profiles() {
        if (available_profiles_.empty()) return;
        
        ImGui::Text("Select a saved profile:");
        
        for (const auto& profile : available_profiles_) {
            std::string label = profile.name;
            if (profile.is_environment) {
                label += " (environment)";
            }
            
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    try_connect(profile);
                } else {
                    populate_form_with_profile(profile);
                }
            }
        }
    }
    
    void populate_form_with_profile(const models::jira_connection_profile& profile) {
        current_profile_ = profile;
        std::strncpy(url_buffer_, profile.server_url.c_str(), sizeof(url_buffer_) - 1);
        std::strncpy(username_buffer_, profile.username.c_str(), sizeof(username_buffer_) - 1);
        std::strncpy(token_buffer_, profile.api_token.c_str(), sizeof(token_buffer_) - 1);
        std::strncpy(profile_name_buffer_, profile.name.c_str(), sizeof(profile_name_buffer_) - 1);
    }
    
    void render_connection_form() {
        ImGui::Text("Connect manually:");
        
        // Profile name (only for saving)
        jira_ui::render_input_field("Profile Name", profile_name_buffer_);
        
        // Server URL
        jira_ui::render_input_field("Server URL", url_buffer_);
        
        // Username
        jira_ui::render_input_field("Username", username_buffer_);
        
        // API Token (password field)
        jira_ui::render_input_field("API Token", token_buffer_, ImGuiInputTextFlags_Password);
        
        // Action buttons
        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            try_connect(create_profile_from_form());
        }
        
        ImGui::SameLine();
        
        // Save profile button
        if (ImGui::Button("Save Profile", ImVec2(120, 0))) {
            jira_host_->save_profile(create_profile_from_form());
            refresh_profiles();
        }
        
        ImGui::SameLine();
        
        // Refresh profiles button
        if (ImGui::Button("Refresh", ImVec2(120, 0))) {
            refresh_profiles();
        }
    }
    
    models::jira_connection_profile create_profile_from_form() {
        models::jira_connection_profile profile;
        profile.name = profile_name_buffer_;
        profile.server_url = url_buffer_;
        profile.username = username_buffer_;
        profile.api_token = token_buffer_;
        return profile;
    }
    
    // Connect to JIRA with specified profile
    void try_connect(const models::jira_connection_profile& profile) {
        // Clear previous errors
        error_message_.clear();
        
        try {
            if (jira_host_->connect(profile)) {
                // Connection successful
                JIRA_INFO_FMT("Connected to JIRA server: {}", profile.server_url);
            } else {
                error_message_ = "Failed to connect to JIRA server";
                JIRA_ERROR_FMT("Failed to connect to JIRA server: {}", profile.server_url);
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Connection error: {}", e.what());
            JIRA_ERROR_FMT("JIRA connection error: {}", e.what());
        }
    }
    
    // Load profiles from model
    void refresh_profiles() {
        available_profiles_ = jira_host_->load_profiles();
        
        // Also add environment profiles
        auto env_profiles = jira_host_->detect_environment_profiles();
        available_profiles_.insert(available_profiles_.end(), env_profiles.begin(), env_profiles.end());
    }
    
    // Helper for getting colors - simulation of the parent class's get_color method
    ImVec4 get_color(int index) const {
        static const ImVec4 colors[] = {
            {0.0f, 0.4f, 0.8f, 1.0f},  // Jira blue - primary color
            {0.1f, 0.5f, 0.9f, 0.7f},  // Light blue - secondary color
            {0.8f, 0.2f, 0.2f, 1.0f},  // Error color - red
            {0.2f, 0.7f, 0.2f, 1.0f},  // Success color - green
            {0.9f, 0.7f, 0.0f, 1.0f},  // Warning color - amber
            {0.5f, 0.5f, 0.5f, 1.0f},  // Neutral color - gray
        };
        
        if (index >= 0 && index < 6) {
            return colors[index];
        }
        
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // JIRA model
    std::shared_ptr<models::jira_model> jira_host_;
    
    // Connection state and profiles
    std::vector<models::jira_connection_profile> available_profiles_;
    models::jira_connection_profile current_profile_;
    std::string error_message_;
    
    // Connection form
    char url_buffer_[256] = "";
    char username_buffer_[128] = "";
    char token_buffer_[256] = "";
    char profile_name_buffer_[128] = "Default";
};

} // namespace rouen::cards
