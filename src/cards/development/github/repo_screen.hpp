#pragma once

#include <memory>
#include <string>

#include "../../../helpers/imgui_include.hpp"
#include <glaze/json.hpp>

#include "../../../models/github/host.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "../../../../external/IconsMaterialDesign.h"

namespace rouen::cards::github {
    // Helper functions for safe JSON access
    namespace detail {
        inline void print_json(const glz::json_t& obj) {
            std::string out;
            auto err = glz::write_json(obj, out);
            if (err == 0)
                std::cerr << "[repo_screen] Offending object: " << out << '\n';
            else
                std::cerr << "[repo_screen] (Could not serialize object)\n";
        }
        inline std::string safe_get_string(const glz::json_t& obj, std::string_view key, std::string_view fallback = "<missing>") {
            if (obj.contains(key) && obj[key].is_string()) {
                return obj[key].get_string();
            } else {
                std::cerr << "[repo_screen] Missing or invalid string field: '" << key << "'\n";
                print_json(obj);
                return std::string(fallback);
            }
        }
        inline double safe_get_number(const glz::json_t& obj, std::string_view key, double fallback = 0.0) {
            if (obj.contains(key) && obj[key].is_number()) {
                return obj[key].get_number();
            } else {
                std::cerr << "[repo_screen] Missing or invalid number field: '" << key << "'\n";
                print_json(obj);
                return fallback;
            }
        }
    }

    struct repo_screen {
        repo_screen(glz::json_t repo, std::shared_ptr<models::github::host> host) {
            // If repo is an array, extract the first element if available
            if (repo.is_array() && !repo.get_array().empty()) {
                repo_ = std::move(repo.get_array().front());
            } else {
                repo_ = std::move(repo);
            }
            host_ = host;
        }

        std::string name() const {
            if (!repo_.is_object()) {
                std::cerr << "[repo_screen] Expected object but got " 
                          << (repo_.is_array() ? "array" : repo_.is_null() ? "null" : "other type") << "\n";
                return "<invalid>";
            }
            return detail::safe_get_string(repo_, "name");
        }

        std::string full_name() const {
            if (!repo_.is_object()) {
                std::cerr << "[repo_screen] Expected object but got " 
                          << (repo_.is_array() ? "array" : repo_.is_null() ? "null" : "other type") << "\n";
                return "<invalid>";
            }
            return detail::safe_get_string(repo_, "full_name");
        }

        void render() {
            auto repo_name = name();
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
                if (!workflows_.empty() && workflows_.contains("workflows") && workflows_["workflows"].is_array()) {
                    for (auto const &workflow : workflows_["workflows"].get_array()) {
                        auto workflow_name = detail::safe_get_string(workflow, "name");
                        ImGui::PushID(workflow_name.c_str());
                        // Display workflow name
                        ImGui::TextUnformatted(workflow_name.c_str());
                        
                        // Add button to fetch workflow runs
                        if (ImGui::SmallButton("Fetch Runs")) {
                            workflow_runs_[workflow_name] = 
                                host_->workflow_runs(detail::safe_get_string(workflow, "url"));
                        }
                        
                        // Display workflow runs if available
                        auto it = workflow_runs_.find(workflow_name);
                        if (it != workflow_runs_.end()) {
                            auto& runs = it->second;
                            
                            if (show_details_) {
                                json_view_.render(runs);
                            }
                            
                            if (runs.contains("workflow_runs") && runs["workflow_runs"].is_array() && ImGui::BeginTable("runs", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                                ImGui::TableSetupColumn("Status");
                                ImGui::TableSetupColumn("Title");
                                ImGui::TableSetupColumn("Started At");
                                ImGui::TableSetupColumn("Hash");
                                ImGui::TableSetupColumn("##actions");
                                ImGui::TableHeadersRow();
                                
                                for (auto const &run : runs["workflow_runs"].get_array()) {
                                    int run_id = static_cast<int>(detail::safe_get_number(run, "id"));
                                    ImGui::PushID(run_id);
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
                                    ImGui::TextUnformatted(detail::safe_get_string(run, "status").c_str());
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(conclusion.c_str());
                                    // Title column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(detail::safe_get_string(run, "display_title").c_str());
                                    // Started At column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(detail::safe_get_string(run, "run_started_at").c_str());
                                    // Hash column
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(detail::safe_get_string(run, "head_sha").c_str());
                                    // Actions column
                                    ImGui::TableNextColumn();
                                    if (ImGui::SmallButton(ICON_MD_WEB " Open...")) {
                                        host_->open_url(detail::safe_get_string(run, "html_url"));
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
                host_->open_url(detail::safe_get_string(repo_, "html_url"));
            }
            ImGui::PopID();
        }
        // Add a public accessor for the raw JSON
        const glz::json_t& json() const { return repo_; }
    private:
        glz::json_t repo_;
        std::shared_ptr<models::github::host> host_;
        bool show_details_{false};
        glz::json_t workflows_{};
        std::unordered_map<std::string, glz::json_t> workflow_runs_{};
        helpers::views::json_view json_view_;
    };
}
