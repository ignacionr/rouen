#pragma once

#include <memory>
#include <string>

#include <imgui.h>
#include <glaze/json.hpp>

#include "../../../models/github/host.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "../../../../external/IconsMaterialDesign.h"

namespace rouen::cards::github {
    struct repo_screen {
        repo_screen(glz::json_t repo, std::shared_ptr<models::github::host> host) 
            : repo_{std::move(repo)}, host_{host} {}

        std::string const &name() const {
            return repo_["name"].get_string();
        }

        std::string const &full_name() const {
            return repo_["full_name"].get_string();
        }

        void render() {
            auto const &repo_name{name()};
            ImGui::PushID(repo_name.c_str());
            
            if (ImGui::BeginTable(repo_name.c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                
                // Repository name with toggle for details
                ImGui::TextUnformatted(repo_name.data(), repo_name.data() + repo_name.size());
                if (ImGui::SameLine(); ImGui::SmallButton("Details")) {
                    show_details_ = !show_details_;
                }
                
                // Show JSON details if requested
                if (show_details_) {
                    json_view_.render(repo_);
                }
                
                ImGui::TableNextColumn();
                
                // Handle workflows
                if (ImGui::Button("Fetch Workflows")) {
                    workflows_ = host_->repo_workflows(full_name());
                    workflow_runs_.clear();
                }
                
                // Show JSON details if requested and workflows are loaded
                if (show_details_ && !workflows_.empty()) {
                    json_view_.render(workflows_);
                }
                
                // Render each workflow if workflows are loaded
                if (!workflows_.empty() && workflows_.contains("workflows")) {
                    for (auto const &workflow : workflows_["workflows"].get_array()) {
                        ImGui::PushID(workflow["name"].get_string().c_str());
                        
                        // Display workflow name
                        ImGui::TextUnformatted(workflow["name"].get_string().c_str());
                        
                        // Add button to fetch workflow runs
                        if (ImGui::SmallButton("Fetch Runs")) {
                            workflow_runs_[workflow["name"].get_string()] = 
                                host_->workflow_runs(workflow["url"].get_string());
                        }
                        
                        // Display workflow runs if available
                        auto it = workflow_runs_.find(workflow["name"].get_string());
                        if (it != workflow_runs_.end()) {
                            auto& runs = it->second;
                            
                            if (show_details_) {
                                json_view_.render(runs);
                            }
                            
                            if (ImGui::BeginTable("runs", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                                ImGui::TableSetupColumn("Status");
                                ImGui::TableSetupColumn("Title");
                                ImGui::TableSetupColumn("Started At");
                                ImGui::TableSetupColumn("Hash");
                                ImGui::TableSetupColumn("##actions");
                                ImGui::TableHeadersRow();
                                
                                for (auto const &run : runs["workflow_runs"].get_array()) {
                                    ImGui::PushID(static_cast<int>(run["id"].get_number()));
                                    ImGui::TableNextRow();
                                    
                                    // Status column
                                    ImGui::TableNextColumn();
                                    std::string conclusion = "pending";
                                    if (run.contains("conclusion") && run["conclusion"].is_string()) {
                                        conclusion = run["conclusion"].get_string();
                                    }
                                    
                                    // Color-coded status icons
                                    if (conclusion == "failure") {
                                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4{1.0f, 0.0f, 0.0f, 0.5f}));
                                        ImGui::TextUnformatted(ICON_MD_CANCEL);
                                    } else if (conclusion == "success") {
                                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4{0.0f, 1.0f, 0.0f, 0.5f}));
                                        ImGui::TextUnformatted(ICON_MD_CHECK_CIRCLE);
                                    } else {
                                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 1.0f, 0.25f}));
                                        ImGui::TextUnformatted(ICON_MD_RUN_CIRCLE);
                                    }
                                    
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(run["status"].get_string().c_str());
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(conclusion.c_str());
                                    
                                    // Title column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(run["display_title"].get_string().c_str());
                                    
                                    // Started At column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(run["run_started_at"].get_string().c_str());
                                    
                                    // Hash column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(run["head_sha"].get_string().c_str());
                                    
                                    // Actions column
                                    ImGui::TableNextColumn();
                                    if (ImGui::SmallButton(ICON_MD_WEB " Open...")) {
                                        host_->open_url(run["html_url"].get_string());
                                    }
                                    
                                    ImGui::PopID();
                                }
                                ImGui::EndTable();
                            }
                        }
                        
                        ImGui::PopID();
                    }
                }
                
                ImGui::EndTable();
            }
            
            // Repository actions
            if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " Open in Browser")) {
                host_->open_url(repo_["html_url"].get_string());
            }
            
            ImGui::PopID();
        }

    private:
        glz::json_t repo_;
        std::shared_ptr<models::github::host> host_;
        bool show_details_{false};
        glz::json_t workflows_{};
        std::unordered_map<std::string, glz::json_t> workflow_runs_{};
        helpers::views::json_view json_view_;
    };
}