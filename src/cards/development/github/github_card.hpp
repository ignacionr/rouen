#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>

#include "../../../helpers/imgui_include.hpp"
#include "../../../helpers/glaze_include.hpp"

#include "../../interface/card.hpp"
#include "../../../models/github/host.hpp"
#include "../../../models/github/login_host.hpp"
#include "../../../registrar.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "../../../../external/IconsMaterialDesign.h"
#include "login_screen.hpp"
#include "repo_screen.hpp"

namespace rouen::cards {
    struct github_card : public card {
        github_card(std::string_view config_name = "default") : config_name_{std::string(config_name)} {
            // Set card colors - GitHub dark theme inspired colors
            colors[0] = {0.13f, 0.37f, 0.71f, 1.0f}; // Primary color (GitHub blue)
            colors[1] = {0.18f, 0.18f, 0.18f, 0.7f}; // Secondary color (dark gray)
            colors[2] = {0.15f, 0.68f, 0.38f, 1.0f}; // Success color (green)
            colors[3] = {0.85f, 0.25f, 0.25f, 1.0f}; // Failure color (red)
            
            // Set window properties
            name("GitHub");
            width = 800.0f; // GitHub card needs more width for tables
            
            // Initialize the GitHub host and login host
            try {
                // First try to load from the registrar (in-memory)
                login_host_ = registrar::get<models::github::login_host>(std::string(config_name));
            } catch (std::exception const &) {
                // If not found in registrar, create a new instance
                login_host_ = std::make_shared<models::github::login_host>();
                
                // Try to load token from file
                load_token_from_file();
                
                if (login_host_->personal_token().empty()) {
                    // If token is still empty, we need configuration
                    config_mode_ = true;
                }
            }
            
            host_ = std::make_shared<models::github::host>();
            host_->set_login_host(login_host_);
        }
        
        std::string get_uri() const override {
            if (config_name_ == "default") {
                return "github";
            }
            return std::format("github:{}", config_name_);
        }
        
        private:
            // New method to save token to file
            void save_token_to_file() {
                try {
                    // Create GitHub config directory if it doesn't exist
                    std::filesystem::path config_dir = get_config_directory();
                    std::filesystem::create_directories(config_dir);
                    
                    // Save token to file
                    std::string filename = get_config_filename();
                    glz::json_t config;
                    config["token"] = login_host_->personal_token();
                    
                    std::ofstream file(filename);
                    if (file.is_open()) {
                        std::string json_str;
                        auto result = glz::write_json(config, json_str);
                        if (!result) {
                            file << json_str;
                        }
                        file.close();
                    }
                } catch (std::exception const &) {
                    // Log error or handle it
                }
            }
            
            // New method to load token from file
            void load_token_from_file() {
                try {
                    std::string filename = get_config_filename();
                    
                    // Check if file exists
                    if (!std::filesystem::exists(filename)) {
                        return;
                    }
                    
                    // Read and parse JSON
                    std::ifstream file(filename);
                    if (file.is_open()) {
                        std::string json_str;
                        file >> json_str;
                        
                        glz::json_t config;
                        auto ec = glz::read_json(config, json_str);
                        if (!ec && config.contains("token")) {
                            try {
                                // For the new Glaze API, we need to handle token access differently
                                std::string token = config["token"].get<std::string>();
                                login_host_->set_personal_token(token);
                            } catch (const std::exception&) {
                                // Handle token access error
                            }
                        }
                    }
                } catch (std::exception const &) {
                    // Log error or handle it
                }
            }
            
            // Helper to get config directory
            std::filesystem::path get_config_directory() const {
                return std::filesystem::path(getenv("HOME")) / ".config" / "rouen" / "github";
            }
            
            // Helper to get config filename
            std::string get_config_filename() const {
                return (get_config_directory() / (std::string(config_name_) + ".json")).string();
            }
            
            // Override the existing login_screen_->render() section
            bool render() override {
                return render_window([this]() {
                    if (config_mode_) {
                        if (!login_screen_) {
                            login_screen_ = std::make_unique<github::login_screen>(*login_host_);
                        }
                        
                        if (login_screen_->render()) {
                            // Save login configuration to registrar
                            registrar::add<models::github::login_host>(std::string(config_name_), login_host_);
                            
                            // Also save to file for persistence
                            save_token_to_file();
                            
                            config_mode_ = false;
                        }
                    } else {
                        render_main_interface();
                    }
                });
            }
            
            void render_main_interface() {
                if (ImGui::BeginTabBar("GitHubTabs")) {
                    if (ImGui::BeginTabItem(ICON_MD_DASHBOARD " Overview")) {
                        render_overview_tab();
                        ImGui::EndTabItem();
                    }
                    
                    if (ImGui::BeginTabItem(ICON_MD_FOLDER " Repositories")) {
                        render_repositories_tab();
                        ImGui::EndTabItem();
                    }
                    
                    if (ImGui::BeginTabItem(ICON_MD_ACCOUNT_CIRCLE " Profile")) {
                        render_profile_tab();
                        ImGui::EndTabItem();
                    }
                    
                    if (ImGui::BeginTabItem(ICON_MD_SETTINGS " Settings")) {
                        render_settings_tab();
                        ImGui::EndTabItem();
                    }
                    
                    ImGui::EndTabBar();
                }
            }
            
            void render_overview_tab() {
                try {
                    // Quick stats
                    ImGui::TextColored(colors[0], ICON_MD_INFO " GitHub Overview");
                    
                    if (user_info_.empty()) {
                        if (ImGui::Button("Load Profile")) {
                            user_info_ = host_->user();
                        }
                    } else {
                        // Display user info nicely
                        if (user_info_.contains("login")) {
                            std::string login = user_info_["login"].get<std::string>();
                            ImGui::Text("User: %s", login.c_str());
                        }
                        
                        if (user_info_.contains("name") && !user_info_["name"].is_null()) {
                            std::string name = user_info_["name"].get<std::string>();
                            ImGui::Text("Name: %s", name.c_str());
                        }
                        
                        if (user_info_.contains("public_repos")) {
                            int public_repos = static_cast<int>(user_info_["public_repos"].get<double>());
                            ImGui::Text("Public Repositories: %d", public_repos);
                        }
                        
                        if (user_info_.contains("followers")) {
                            int followers = static_cast<int>(user_info_["followers"].get<double>());
                            ImGui::Text("Followers: %d", followers);
                        }
                        
                        if (user_info_.contains("following")) {
                            int following = static_cast<int>(user_info_["following"].get<double>());
                            ImGui::Text("Following: %d", following);
                        }
                    }
                    
                    ImGui::Separator();
                    
                    // Quick actions
                    ImGui::TextColored(colors[0], ICON_MD_FLASH_ON " Quick Actions");
                    
                    if (ImGui::Button(ICON_MD_FOLDER " My Repositories")) {
                        // Switch to repositories tab
                        ImGui::SetTabItemClosed("Repositories");
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_BUILD " CI/CD Dashboard")) {
                        // Could open the CI card or switch to it
                        latest_error_ = "CI/CD Dashboard - Use dedicated GitHub CI card";
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_OPEN_IN_BROWSER " GitHub.com")) {
                        host_->open_url("https://github.com");
                    }
                    
                } catch (std::exception const &e) {
                    ImGui::TextColored(colors[3], "Error: %s", e.what());
                }
            }
            
            void render_repositories_tab() {
                try {
                    ImGui::TextColored(colors[0], ICON_MD_FOLDER " My Repositories");
                    
                    // Repository filter
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputTextWithHint("##repo_filter", "Filter repositories...", 
                                            repo_filter_.data(), repo_filter_.capacity());
                    
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_MD_REFRESH " Refresh")) {
                        repos_.clear();
                        auto repos_json = host_->user_repos();
                        
                        if (repos_json.is_array()) {
                            for (const auto& repo_json : repos_json.get<std::vector<glz::json_t>>()) {
                                repos_.emplace_back(repo_json, host_);
                            }
                        }
                    }
                    
                    if (repos_.empty()) {
                        if (ImGui::Button("Load Repositories")) {
                            auto repos_json = host_->user_repos();
                            
                            if (repos_json.is_array()) {
                                for (const auto& repo_json : repos_json.get<std::vector<glz::json_t>>()) {
                                    repos_.emplace_back(repo_json, host_);
                                }
                            }
                        }
                    } else {
                        // Repository table
                        if (ImGui::BeginTable("repositories", 4, ImGuiTableFlags_Resizable | 
                                             ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                            
                            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("Language", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                            ImGui::TableSetupColumn("Stars", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                            ImGui::TableHeadersRow();
                            
                            for (auto& repo : repos_) {
                                std::string repo_name = repo.name();
                                
                                // Apply filter
                                if (!repo_filter_.empty() && 
                                    repo_name.find(repo_filter_) == std::string::npos) {
                                    continue;
                                }
                                
                                ImGui::TableNextRow();
                                
                                // Name column
                                ImGui::TableNextColumn();
                                ImGui::Text("%s", repo_name.c_str());
                                
                                // Language column
                                ImGui::TableNextColumn();
                                if (repo.json().contains("language") && !repo.json()["language"].is_null()) {
                                    std::string language = repo.json()["language"].get<std::string>();
                                    ImGui::Text("%s", language.c_str());
                                } else {
                                    ImGui::TextColored(colors[5], "—");
                                }
                                
                                // Stars column
                                ImGui::TableNextColumn();
                                if (repo.json().contains("stargazers_count")) {
                                    int stars = static_cast<int>(repo.json()["stargazers_count"].get<double>());
                                    ImGui::Text("%d", stars);
                                } else {
                                    ImGui::TextColored(colors[5], "—");
                                }
                                
                                // Actions column
                                ImGui::TableNextColumn();
                                ImGui::PushID(repo_name.c_str());
                                
                                if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " View##repo_view")) {
                                    if (repo.json().contains("html_url")) {
                                        std::string url = repo.json()["html_url"].get<std::string>();
                                        host_->open_url(url);
                                    }
                                }
                                
                                ImGui::SameLine();
                                if (ImGui::SmallButton(ICON_MD_BUILD " CI/CD##repo_cicd")) {
                                    latest_error_ = std::format("CI/CD for {} - Use GitHub CI card", repo_name);
                                }
                                
                                ImGui::PopID();
                            }
                            
                            ImGui::EndTable();
                        }
                    }
                    
                } catch (std::exception const &e) {
                    ImGui::TextColored(colors[3], "Error: %s", e.what());
                }
            }
            
            void render_profile_tab() {
                try {
                    ImGui::TextColored(colors[0], ICON_MD_ACCOUNT_CIRCLE " GitHub Profile");
                    
                    if (user_info_.empty()) {
                        if (ImGui::Button("Load Profile Data")) {
                            user_info_ = host_->user();
                        }
                    } else {
                        // Display detailed profile information
                        if (ImGui::BeginTable("profile", 2, ImGuiTableFlags_SizingFixedFit)) {
                            auto display_field = [&](const char* label, const char* key, bool is_url = false) {
                                if (user_info_.contains(key) && !user_info_[key].is_null()) {
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%s:", label);
                                    ImGui::TableNextColumn();
                                    
                                    std::string value = user_info_[key].get<std::string>();
                                    if (is_url && !value.empty()) {
                                        if (ImGui::SmallButton(value.c_str())) {
                                            host_->open_url(value);
                                        }
                                    } else {
                                        ImGui::Text("%s", value.c_str());
                                    }
                                }
                            };
                            
                            display_field("Username", "login");
                            display_field("Name", "name");
                            display_field("Company", "company");
                            display_field("Blog", "blog", true);
                            display_field("Location", "location");
                            display_field("Email", "email");
                            display_field("Bio", "bio");
                            display_field("Twitter", "twitter_username");
                            display_field("GitHub URL", "html_url", true);
                            
                            // Numeric fields
                            auto display_number = [&](const char* label, const char* key) {
                                if (user_info_.contains(key)) {
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%s:", label);
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%d", static_cast<int>(user_info_[key].get<double>()));
                                }
                            };
                            
                            display_number("Public Repos", "public_repos");
                            display_number("Public Gists", "public_gists");
                            display_number("Followers", "followers");
                            display_number("Following", "following");
                            
                            ImGui::EndTable();
                        }
                        
                        // Organizations
                        ImGui::Separator();
                        ImGui::TextColored(colors[0], ICON_MD_BUSINESS " Organizations");
                        
                        if (organizations_.empty()) {
                            if (ImGui::Button("Load Organizations")) {
                                organizations_ = host_->organizations();
                            }
                        } else {
                            if (organizations_.is_array()) {
                                for (const auto& org : organizations_.get<std::vector<glz::json_t>>()) {
                                    if (org.contains("login")) {
                                        std::string org_name = org["login"].get<std::string>();
                                        if (ImGui::SmallButton(org_name.c_str())) {
                                            if (org.contains("html_url")) {
                                                std::string url = org["html_url"].get<std::string>();
                                                host_->open_url(url);
                                            }
                                        }
                                        ImGui::SameLine();
                                    }
                                }
                            }
                        }
                    }
                    
                } catch (std::exception const &e) {
                    ImGui::TextColored(colors[3], "Error: %s", e.what());
                }
            }
            
            void render_settings_tab() {
                ImGui::TextColored(colors[0], ICON_MD_SETTINGS " GitHub Settings");
                
                // Token status
                if (login_host_->personal_token().empty()) {
                    ImGui::TextColored(colors[3], ICON_MD_WARNING " No GitHub token configured");
                } else {
                    ImGui::TextColored(colors[1], ICON_MD_CHECK_CIRCLE " GitHub token configured");
                    
                    // Masked token display
                    std::string token = login_host_->personal_token();
                    std::string masked_token = token.substr(0, 4) + "..." + token.substr(token.length() - 4);
                    ImGui::Text("Token: %s", masked_token.c_str());
                }
                
                ImGui::Separator();
                
                // Configuration actions
                if (ImGui::Button(ICON_MD_EDIT " Reconfigure Token")) {
                    config_mode_ = true;
                }
                
                ImGui::SameLine();
                if (ImGui::Button(ICON_MD_REFRESH " Test Connection")) {
                    try {
                        auto test_user = host_->user();
                        if (test_user.contains("login")) {
                            latest_error_ = "Connection successful!";
                        } else {
                            latest_error_ = "Connection failed - invalid response";
                        }
                    } catch (std::exception const &e) {
                        latest_error_ = std::format("Connection failed: {}", e.what());
                    }
                }
                
                ImGui::Separator();
                
                // Configuration info
                ImGui::Text("Configuration:");
                ImGui::Text("  Config Name: %s", config_name_.c_str());
                ImGui::Text("  Config File: %s", get_config_filename().c_str());
                
                // Clear data
                ImGui::Separator();
                if (ImGui::Button(ICON_MD_CLEAR " Clear Cache")) {
                    user_info_ = glz::json_t{};
                    organizations_ = glz::json_t{};
                    repos_.clear();
                    latest_error_ = "Cache cleared";
                }
            }
            
            std::string config_name_;
            std::shared_ptr<models::github::host> host_;
            std::shared_ptr<models::github::login_host> login_host_;
            std::unique_ptr<github::login_screen> login_screen_;
            bool config_mode_ = false;
            std::string selected_org_login_;
            std::vector<github::repo_screen> repos_;
            std::string repo_name_;
            std::string repo_filter_;
            std::string latest_error_;
            glz::json_t organizations_{};
            glz::json_t user_info_{};
            helpers::views::json_view json_view_;
            std::string debug_repos_json_;
    };
}
