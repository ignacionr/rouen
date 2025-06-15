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
            glz::write_json(obj, out);
            std::cerr << "[repo_screen] Offending object: " << out << '\n';
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
                if (show_details_ && workflows_.contains("workflows")) {
                    json_view_.render(workflows_);
                }
                
                // Render each workflow if workflows are loaded
                if (workflows_.contains("workflows")) {
                    try {
                        // For the new Glaze API, we'll attempt to iterate through workflows
                        // This is a simplified approach since the array methods have changed
                        std::string workflows_str;
                        glz::write_json(workflows_["workflows"], workflows_str);
                        ImGui::Text("Workflows: %s", workflows_str.c_str());
                    } catch (const std::exception& e) {
                        ImGui::Text("Error displaying workflows: %s", e.what());
                    }
                }
                
                // Repository actions
                if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " Open in Browser")) {
                    host_->open_url(detail::safe_get_string(repo_, "html_url"));
                }
            }
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
