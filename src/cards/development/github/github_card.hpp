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
        github_card(std::string_view config_name = "default") : config_name_{config_name} {
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
                        try {
                            // Simplified GitHub integration for new Glaze API compatibility
                            ImGui::Text("GitHub integration simplified for API compatibility");
                            
                            if (ImGui::Button("Fetch Organizations")) {
                                organizations_ = host_->organizations();
                            }
                            
                            if (ImGui::Button("Fetch User Repositories")) {
                                repos_.clear();
                                // Repository iteration disabled due to API changes
                                ImGui::Text("Repository display temporarily disabled");
                            }
                            
                            // User info section
                            if (ImGui::CollapsingHeader("User Info")) {
                                if (ImGui::Button("Fetch User Info")) {
                                    user_info_ = host_->user();
                                }
                                
                                if (user_info_.contains("login")) {
                                    json_view_.render(user_info_);
                                }
                            }
                        } catch (std::exception const &e) {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", e.what());
                        }
                        
                        // Configuration button
                        if (ImGui::Button(ICON_MD_SETTINGS " Configure")) {
                            config_mode_ = true;
                        }
                    }
                });
            }
            
        private:
            std::string_view config_name_;
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
