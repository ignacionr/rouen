#pragma once

#include <memory>
#include <string>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "../../../models/github/host.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "../../../../external/IconsMaterialDesign.h"

namespace rouen::cards::github {
    struct repo_screen {
        repo_screen(nlohmann::json repo, std::shared_ptr<models::github::host> host) 
            : repo_{std::move(repo)}, host_{host} {}

        std::string const &name() const {
            return repo_.at("name").get_ref<const std::string &>();
        }

        std::string const &full_name() const {
            return repo_.at("full_name").get_ref<const std::string &>();
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
                    for (auto const &workflow : workflows_.at("workflows")) {
                        ImGui::PushID(workflow.at("name").get_ref<const std::string&>().c_str());
                        
                        // Display workflow name
                        ImGui::TextUnformatted(workflow.at("name").get_ref<const std::string&>().c_str());
                        
                        // Add button to fetch workflow runs
                        if (ImGui::SmallButton("Fetch Runs")) {
                            workflow_runs_[workflow.at("name").get<std::string>()] = 
                                host_->workflow_runs(workflow.at("url").get<std::string>());
                        }
                        
                        // Display workflow runs if available
                        auto it = workflow_runs_.find(workflow.at("name").get<std::string>());
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
                                
                                for (auto const &run : runs.at("workflow_runs")) {
                                    ImGui::PushID(run.at("id").get<int>());
                                    ImGui::TableNextRow();
                                    
                                    // Status column
                                    ImGui::TableNextColumn();
                                    auto const &conclusion_el {run.at("conclusion")};
                                    auto const &conclusion {conclusion_el.is_string() ? conclusion_el.get_ref<const std::string&>() : "pending"};
                                    
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
                                    ImGui::TextUnformatted(run.at("status").get_ref<const std::string&>().c_str());
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(conclusion.c_str());
                                    
                                    // Title column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(run.at("display_title").get_ref<const std::string&>().c_str());
                                    
                                    // Started At column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(run.at("run_started_at").get_ref<const std::string&>().c_str());
                                    
                                    // Hash column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(run.at("head_sha").get_ref<const std::string&>().c_str());
                                    
                                    // Actions column
                                    ImGui::TableNextColumn();
                                    if (ImGui::SmallButton(ICON_MD_WEB " Open...")) {
                                        host_->open_url(run.at("html_url").get<std::string>());
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
                host_->open_url(repo_.at("html_url").get<std::string>());
            }
            
            ImGui::PopID();
        }

    private:
        nlohmann::json repo_;
        std::shared_ptr<models::github::host> host_;
        bool show_details_{false};
        nlohmann::json workflows_{};
        std::unordered_map<std::string, nlohmann::json> workflow_runs_{};
        helpers::views::json_view json_view_;
    };
}