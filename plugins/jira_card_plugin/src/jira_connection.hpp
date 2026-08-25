#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <chrono>
#include <format>
#include <cstring>

#include "helpers/imgui_include.hpp"
#include "jira_model.hpp"
#include "helpers/debug.hpp"
#include "IconsMaterialDesign.h"
#include "jira_ui_components.hpp"

namespace rouen::cards {

class jira_connection_handler {
public:
    jira_connection_handler() {
        jira_host_ = std::make_shared<models::jira_model>();
        refresh_profiles();
        
        auto env_profiles = jira_host_->detect_environment_profiles();
        if (!env_profiles.empty()) {
            try_connect(env_profiles[0]);
        }
    }
    
    void render_connection_screen() {
        ImGui::TextColored(get_color(0), ICON_MD_CLOUD_QUEUE " Connect to Jira");
        ImGui::Separator();
        
        if (!error_message_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, get_color(2));
            ImGui::TextWrapped("%s", error_message_.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        
        render_saved_profiles();
        ImGui::Separator();
        
        render_connection_form();
    }
    
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
        
        jira_ui::render_input_field("Profile Name", profile_name_buffer_);
        jira_ui::render_input_field("Server URL", url_buffer_);
        jira_ui::render_input_field("Username", username_buffer_);
        jira_ui::render_input_field("API Token", token_buffer_, ImGuiInputTextFlags_Password);
        
        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            try_connect(create_profile_from_form());
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Save Profile", ImVec2(120, 0))) {
            jira_host_->save_profile(create_profile_from_form());
            refresh_profiles();
        }
        
        ImGui::SameLine();
        
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
    
    void try_connect(const models::jira_connection_profile& profile) {
        error_message_.clear();
        
        try {
            jira_host_->connect(profile);
            JIRA_INFO_FMT("Connected to JIRA server: {}", profile.server_url);
        } catch (const std::exception& e) {
            error_message_ = std::format("Connection error: {}", e.what());
            JIRA_ERROR_FMT("JIRA connection error: {}", e.what());
        }
    }
    
    void refresh_profiles() {
        available_profiles_ = jira_host_->load_profiles();
        auto env_profiles = jira_host_->detect_environment_profiles();
        available_profiles_.insert(available_profiles_.end(), env_profiles.begin(), env_profiles.end());
    }
    
    ImVec4 get_color(int index) const {
        static const ImVec4 colors[] = {
            {0.0f, 0.4f, 0.8f, 1.0f},
            {0.1f, 0.5f, 0.9f, 0.7f},
            {0.8f, 0.2f, 0.2f, 1.0f},
            {0.2f, 0.7f, 0.2f, 1.0f},
            {0.9f, 0.7f, 0.0f, 1.0f},
            {0.5f, 0.5f, 0.5f, 1.0f},
        };
        
        if (index >= 0 && index < 6) {
            return colors[index];
        }
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    std::shared_ptr<models::jira_model> jira_host_;
    std::vector<models::jira_connection_profile> available_profiles_;
    models::jira_connection_profile current_profile_;
    std::string error_message_;
    
    char url_buffer_[256] = "";
    char username_buffer_[128] = "";
    char token_buffer_[256] = "";
    char profile_name_buffer_[128] = "Default";
};

} // namespace rouen::cards
