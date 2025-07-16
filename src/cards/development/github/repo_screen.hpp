#pragma once

#include <memory>
#include <string>

#include "../../../helpers/imgui_include.hpp"
#include "../../../helpers/glaze_include.hpp"

#include "../../../models/github/host.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "../../../../external/IconsMaterialDesign.h"

namespace rouen::cards::github {
    // Helper functions for safe JSON access
    namespace detail {
        inline void print_json(const glz::json_t& obj) {
            std::string out;
            auto result = glz::write_json(obj, out);
            if (!result) {
                std::cerr << "[repo_screen] Offending object: " << out << '\n';
            } else {
                std::cerr << "[repo_screen] Error serializing JSON object\n";
            }
        }
        inline std::string safe_get_string(const glz::json_t& obj, std::string_view key, std::string_view fallback = "<missing>") {
            try {
                if (obj.contains(key)) {
                    return obj[key].get<std::string>();
                }
            } catch (const std::exception&) {
                std::cerr << "[repo_screen] Missing or invalid string field: '" << key << "'\n";
                print_json(obj);
            }
            return std::string(fallback);
        }
        inline double safe_get_number(const glz::json_t& obj, std::string_view key, double fallback = 0.0) {
            try {
                if (obj.contains(key)) {
                    return obj[key].get<double>();
                }
            } catch (const std::exception&) {
                std::cerr << "[repo_screen] Missing or invalid number field: '" << key << "'\n";
                print_json(obj);
            }
            return fallback;
        }
    }

    struct repo_screen {
        repo_screen(glz::json_t repo, std::shared_ptr<models::github::host> host) {
            // Simple assignment - no type checking needed with new API
            repo_ = std::move(repo);
            host_ = host;
        }

        std::string name() const {
            // For the new Glaze API, we'll use a simpler approach
            try {
                if (repo_.contains("name")) {
                    return repo_["name"].get<std::string>();
                }
            } catch (const std::exception&) {
                // Fallback if access fails
            }
            return "<invalid>";
        }

        std::string full_name() const {
            // For the new Glaze API, we'll use a simpler approach
            try {
                if (repo_.contains("full_name")) {
                    return repo_["full_name"].get<std::string>();
                }
            } catch (const std::exception&) {
                // Fallback if access fails
            }
            return "<invalid>";
        }

        void render() {
            auto repo_name = name();
            ImGui::PushID(repo_name.c_str());
            
            if (ImGui::BeginTable(repo_name.c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                
                // Repository name with enhanced info
                ImGui::TextUnformatted(repo_name.data(), repo_name.data() + repo_name.size());
                
                // Repository description
                if (repo_.contains("description") && !repo_["description"].is_null()) {
                    std::string description = repo_["description"].get<std::string>();
                    if (!description.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", description.c_str());
                    }
                }
                
                // Language and stats
                ImGui::BeginGroup();
                if (repo_.contains("language") && !repo_["language"].is_null()) {
                    std::string language = repo_["language"].get<std::string>();
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.9f, 1.0f), ICON_MD_CODE " %s", language.c_str());
                    ImGui::SameLine();
                }
                
                if (repo_.contains("stargazers_count")) {
                    int stars = static_cast<int>(repo_["stargazers_count"].get<double>());
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), ICON_MD_STAR " %d", stars);
                    ImGui::SameLine();
                }
                
                if (repo_.contains("forks_count")) {
                    int forks = static_cast<int>(repo_["forks_count"].get<double>());
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ICON_MD_CALL_SPLIT " %d", forks);
                }
                ImGui::EndGroup();
                
                // Toggle for details
                if (ImGui::SameLine(); ImGui::SmallButton("Details")) {
                    show_details_ = !show_details_;
                }
                
                // Show JSON details if requested
                if (show_details_) {
                    json_view_.render(repo_);
                }
                
                ImGui::TableNextColumn();
                
                // Enhanced workflow section
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 0.9f, 1.0f), ICON_MD_BUILD " CI/CD");
                
                // Workflow status indicator
                if (!workflows_.empty()) {
                    render_workflow_status_summary();
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No workflows loaded");
                }
                
                // Workflow actions
                if (ImGui::Button(ICON_MD_REFRESH " Load Workflows")) {
                    workflows_ = host_->repo_workflows(full_name());
                    workflow_runs_.clear();
                }
                
                ImGui::SameLine();
                if (ImGui::Button(ICON_MD_TIMELINE " View All Runs")) {
                    show_all_workflows_ = !show_all_workflows_;
                    if (show_all_workflows_) {
                        load_all_workflow_runs();
                    }
                }
                
                // Show workflows if loaded
                if (!workflows_.empty() && show_all_workflows_) {
                    render_workflows_detailed();
                }
                
                // Repository actions
                ImGui::Separator();
                if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " Open Repository")) {
                    host_->open_url(detail::safe_get_string(repo_, "html_url"));
                }
                
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_BUILD " Actions")) {
                    std::string actions_url = detail::safe_get_string(repo_, "html_url") + "/actions";
                    host_->open_url(actions_url);
                }
                
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_SETTINGS " Settings")) {
                    std::string settings_url = detail::safe_get_string(repo_, "html_url") + "/settings";
                    host_->open_url(settings_url);
                }
            }
            
            ImGui::EndTable();
            ImGui::PopID();
        }
        // Add a public accessor for the raw JSON
        const glz::json_t& json() const { return repo_; }
        
    private:
        void render_workflow_status_summary() {
            if (!workflows_.contains("workflows") || !workflows_["workflows"].is_array()) {
                return;
            }
            
            try {
                auto workflows_array = workflows_["workflows"].get<std::vector<glz::json_t>>();
                int total_workflows = static_cast<int>(workflows_array.size());
                
                ImGui::Text("Workflows: %d", total_workflows);
                
                // Quick status indicators (simplified for now)
                for (const auto& workflow : workflows_array) {
                    if (workflow.contains("state")) {
                        std::string state = workflow["state"].get<std::string>();
                        ImVec4 color = (state == "active") ? 
                            ImVec4(0.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                        
                        ImGui::SameLine();
                        ImGui::TextColored(color, "●");
                    }
                }
            } catch (const std::exception&) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Error parsing workflows");
            }
        }
        
        void render_workflows_detailed() {
            if (!workflows_.contains("workflows")) {
                return;
            }
            
            ImGui::Separator();
            ImGui::Text("Workflows Detail:");
            
            if (ImGui::BeginChild("workflows_detail", ImVec2(0, 200), true)) {
                try {
                    if (workflows_["workflows"].is_array()) {
                        auto workflows_array = workflows_["workflows"].get<std::vector<glz::json_t>>();
                        
                        for (const auto& workflow : workflows_array) {
                            if (workflow.contains("name") && workflow.contains("state")) {
                                std::string name = workflow["name"].get<std::string>();
                                std::string state = workflow["state"].get<std::string>();
                                
                                ImVec4 state_color = (state == "active") ? 
                                    ImVec4(0.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                                
                                ImGui::TextColored(state_color, ICON_MD_SETTINGS " %s", name.c_str());
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%s)", state.c_str());
                                
                                // Show workflow runs if available
                                if (workflow.contains("id")) {
                                    std::string workflow_id = std::to_string(static_cast<int64_t>(workflow["id"].get<double>()));
                                    
                                    ImGui::SameLine();
                                    ImGui::PushID(workflow_id.c_str());
                                    if (ImGui::SmallButton("Runs")) {
                                        load_workflow_runs(workflow_id);
                                    }
                                    ImGui::PopID();
                                    
                                    // Display runs if available
                                    if (workflow_runs_.contains(workflow_id)) {
                                        render_workflow_runs(workflow_id);
                                    }
                                }
                                
                                ImGui::Separator();
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    ImGui::Text("Error displaying workflows: %s", e.what());
                }
            }
            ImGui::EndChild();
        }
        
        void load_all_workflow_runs() {
            if (!workflows_.contains("workflows")) {
                return;
            }
            
            try {
                if (workflows_["workflows"].is_array()) {
                    auto workflows_array = workflows_["workflows"].get<std::vector<glz::json_t>>();
                    
                    for (const auto& workflow : workflows_array) {
                        if (workflow.contains("id")) {
                            std::string workflow_id = std::to_string(static_cast<int64_t>(workflow["id"].get<double>()));
                            load_workflow_runs(workflow_id);
                        }
                    }
                }
            } catch (const std::exception&) {
                // Handle error silently
            }
        }
        
        void load_workflow_runs(const std::string& workflow_id) {
            try {
                std::string runs_url = std::format(
                    "https://api.github.com/repos/{}/actions/workflows/{}/runs?per_page=5",
                    full_name(), workflow_id);
                
                auto runs_json = host_->fetch(runs_url);
                workflow_runs_[workflow_id] = runs_json;
            } catch (const std::exception&) {
                // Handle error silently for now
            }
        }
        
        void render_workflow_runs(const std::string& workflow_id) {
            auto runs_it = workflow_runs_.find(workflow_id);
            if (runs_it == workflow_runs_.end()) {
                return;
            }
            
            const auto& runs_data = runs_it->second;
            if (!runs_data.contains("workflow_runs") || !runs_data["workflow_runs"].is_array()) {
                return;
            }
            
            ImGui::Indent();
            
            try {
                auto runs_array = runs_data["workflow_runs"].get<std::vector<glz::json_t>>();
                
                for (const auto& run : runs_array) {
                    if (run.contains("status") && run.contains("conclusion") && run.contains("run_number")) {
                        std::string status = run["status"].get<std::string>();
                        int run_number = static_cast<int>(run["run_number"].get<double>());
                        
                        ImVec4 status_color;
                        const char* status_icon;
                        
                        if (status == "completed") {
                            if (run.contains("conclusion") && !run["conclusion"].is_null()) {
                                std::string conclusion = run["conclusion"].get<std::string>();
                                if (conclusion == "success") {
                                    status_color = ImVec4(0.0f, 0.8f, 0.2f, 1.0f);
                                    status_icon = ICON_MD_CHECK_CIRCLE;
                                } else if (conclusion == "failure") {
                                    status_color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
                                    status_icon = ICON_MD_ERROR;
                                } else {
                                    status_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                                    status_icon = ICON_MD_HELP;
                                }
                            } else {
                                status_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                                status_icon = ICON_MD_HELP;
                            }
                        } else if (status == "in_progress") {
                            status_color = ImVec4(0.2f, 0.6f, 0.9f, 1.0f);
                            status_icon = ICON_MD_SYNC;
                        } else {
                            status_color = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
                            status_icon = ICON_MD_SCHEDULE;
                        }
                        
                        ImGui::TextColored(status_color, "%s #%d", status_icon, run_number);
                        
                        if (run.contains("html_url")) {
                            ImGui::SameLine();
                            ImGui::PushID((workflow_id + std::to_string(run_number)).c_str());
                            if (ImGui::SmallButton("View")) {
                                std::string url = run["html_url"].get<std::string>();
                                host_->open_url(url);
                            }
                            ImGui::PopID();
                        }
                    }
                }
            } catch (const std::exception&) {
                ImGui::Text("Error displaying runs");
            }
            
            ImGui::Unindent();
        }
        
    private:
        glz::json_t repo_;
        std::shared_ptr<models::github::host> host_;
        bool show_details_{false};
        bool show_all_workflows_{false};
        glz::json_t workflows_{};
        std::unordered_map<std::string, glz::json_t> workflow_runs_{};
        helpers::views::json_view json_view_;
    };
}
