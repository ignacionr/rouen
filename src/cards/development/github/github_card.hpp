#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>

#include <imgui.h>
#include <glaze/json.hpp>

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
                        auto result = config.dump();
                        if (result) {
                            file << *result;
                            file.close();
                        }
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
                            login_host_->set_personal_token(config["token"].get_string());
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
                            // Display organizations dropdown
                            if (ImGui::Button("Fetch Organizations")) {
                                organizations_ = host_->organizations();
                            }
                            
                            if (!organizations_.empty()) {
                                if (ImGui::BeginCombo("Organizations", selected_org_login_.c_str())) {
                                    try {
                                        // List all organizations
                                        for (auto const &org : organizations_.get_array()) {
                                            if (ImGui::Selectable(org["login"].get_string().c_str())) {
                                                selected_org_login_ = org["login"].get_string();
                                                repos_.clear();
                                                auto repos = host_->org_repos(selected_org_login_);
                                                for (auto& repo_json : repos.get_array()) {
                                                    repos_.emplace_back(repo_json, host_);
                                                }
                                            }
                                        }
                                        
                                        // All user repositories option
                                        auto const all_option {"All Repositories"};
                                        if (ImGui::Selectable(all_option)) {
                                            selected_org_login_ = all_option;
                                            repos_.clear();
                                            auto repos = host_->user_repos();
                                            for (auto& repo_json : repos.get_array()) {
                                                repos_.emplace_back(repo_json, host_);
                                            }
                                        }
                                        
                                        latest_error_.clear();
                                    } catch(std::exception const &e) {
                                        latest_error_ = e.what();
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            
                            // Repositories section
                            if (ImGui::CollapsingHeader("Repositories", ImGuiTreeNodeFlags_DefaultOpen)) {
                                // Repository filter
                                if (repo_filter_.reserve(256); ImGui::InputText("Filter", repo_filter_.data(), repo_filter_.capacity())) {
                                    repo_filter_ = repo_filter_.data();
                                }
                                
                                // Display repositories
                                ImGui::BeginChild("ReposList", ImVec2(0, 400), true);
                                for (auto &repo : repos_) {
                                    if (repo_filter_.empty() || repo.full_name().find(repo_filter_) != std::string::npos) {
                                        repo.render();
                                        ImGui::Separator();
                                    }
                                }
                                ImGui::EndChild();
                            }
                            
                            // Add repository section
                            if (ImGui::CollapsingHeader("Add Repository")) {
                                if (repo_name_.reserve(256); ImGui::InputText("Full Name (owner/repo)", repo_name_.data(), repo_name_.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                                    repo_name_ = repo_name_.data();
                                    try {
                                        repos_.emplace_back(host_->find_repo(repo_name_), host_);
                                        latest_error_.clear();
                                        repo_name_.clear();
                                    } catch (std::exception const &e) {
                                        latest_error_ = e.what();
                                    }
                                }
                                
                                // Display error message if any
                                if (!latest_error_.empty()) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", latest_error_.c_str());
                                }
                            }
                            
                            // User info section
                            if (ImGui::CollapsingHeader("User Info")) {
                                if (ImGui::Button("Fetch User Info")) {
                                    user_info_ = host_->user();
                                }
                                
                                if (!user_info_.empty()) {
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
    };
}