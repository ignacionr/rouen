#pragma once

#include <format>
#include <cmath>
#include <string>
#include <vector>

#include "../../helpers/imgui_include.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include "../interface/card.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../models/git.hpp" // Include the git model
#include "../../registrar.hpp"

struct git: public card {
    std::string repo_status; // Store the git status result
    std::unique_ptr<rouen::models::git> git_model; // Git model for handling git operations
    
    git() {
        colors[0] = {0.37f, 0.53f, 0.71f, 1.0f}; // Changed from orange to blue accent color (first_color)
        colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color (second_color)
        
        // Create git model
        git_model = std::make_unique<rouen::models::git>();
        name("Git Repos");
    }

    std::string get_uri() const override {
        return "git";
    }
    
    /**
     * Select a repository and get its current git status
     * 
     * @param repo_path Repository path string
     * @return true if successful, false if failed
     */
    bool select(const std::string& repo_path) {
        if (repo_path.empty() || git_model->getRepos().find(repo_path) == git_model->getRepos().end()) {
            return false;
        }

        this->selected_repo = repo_path;
        this->updateRepoStatus();
        
        return true;
    }
    
    /**
     * Go back to the repository list with a slide animation
     */
    void back_to_list() {
        selected_repo.clear();
    }
    
    /**
     * Update the repository status by using the git model
     * 
     * @return true if successful, false if failed
     */
    bool updateRepoStatus() {
        if (selected_repo.empty()) {
            return false;
        }
        
        // Use the git model to get the status
        repo_status = git_model->getGitStatus(selected_repo);
        return !repo_status.empty();
    }
    
    // Helper function to scale and offset SVG coordinates
    inline ImVec2 ScalePoint(const ImVec2& p, const ImVec2& offset, float scaleX, float scaleY, float svgMax) {
        return ImVec2(offset.x + (p.x / svgMax) * scaleX, offset.y + (p.y / svgMax) * scaleY);
    }

    // Helper function to get status color using a static map
    static ImColor getStatusColor(rouen::models::GitRepoStatus status) {
        static const std::map<rouen::models::GitRepoStatus, ImColor> statusColorMap = {
            {rouen::models::GitRepoStatus::Clean,     ImColor(0, 255, 0, 255)},     // Green
            {rouen::models::GitRepoStatus::Modified,  ImColor(255, 165, 0, 255)},   // Orange
            {rouen::models::GitRepoStatus::Untracked, ImColor(200, 200, 0, 255)},   // Yellow
            {rouen::models::GitRepoStatus::Staged,    ImColor(0, 200, 255, 255)},   // Blue
            {rouen::models::GitRepoStatus::Conflict,  ImColor(255, 0, 0, 255)},     // Red
            {rouen::models::GitRepoStatus::Detached,  ImColor(128, 0, 128, 255)}    // Purple
        };
        
        auto it = statusColorMap.find(status);
        return it != statusColorMap.end() ? it->second : ImColor(255, 255, 255, 255); // White as default
    }

    void render_selected() {
        // Back button
        if (ImGui::Button("Back to Repository List")) {
            back_to_list();
            return;
        }
        
        ImGui::Text("Repository: %s", selected_repo.c_str());
                    
        // Display the git status
        ImGui::Separator();
        ImGui::BeginChild("GitStatus", ImVec2(0, ImGui::GetWindowHeight() - 130.0f), true);
        ImGui::TextWrapped("%s", repo_status.c_str());
        ImGui::EndChild();

        // Repository actions using SmallButton
        // Refresh Status button
        if (ImGui::SmallButton("Refresh")) {
            updateRepoStatus();
        }
        
        // Add "Open in VS Code" button
        ImGui::SameLine();
        if (ImGui::SmallButton("VS Code")) {
            git_model->openInVSCode(selected_repo);
        }
        
        // Add "Push" button only when the branch is ahead
        bool is_ahead = git_model->isBranchAhead(selected_repo);
        if (is_ahead) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Push")) {
                // Store push result
                std::string push_result = git_model->gitPush(selected_repo);
                // Refresh status after push
                updateRepoStatus();
                // Prepend push result to status display
                if (!push_result.empty()) {
                    repo_status = push_result + "\n\n" + repo_status;
                }
            }
        }
    }

    bool render() override {
        return render_window([this]() {
            if (selected_repo.empty()) {
                render_index();
            } else {
                render_selected();
            }
        });
    }
    
    void render_index() {
        // Get repository data from the model
        // const auto& repos = git_model->getRepos(); // Commented out unused variable
        const auto& repo_paths = git_model->getRepoPaths();

        // Display the list of repositories with their statuses as colored dots
        for (const auto& repo_path : repo_paths) {
            rouen::models::GitRepoStatus status = git_model->getRepoStatus(repo_path);
            
            // Get status color using the constexpr function
            ImColor dotColor = getStatusColor(status);
            
            // Begin horizontal layout
            ImGui::BeginGroup();
            
            // Draw status dot
            float dotRadius = 4.0f;
            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImVec2 dotPos(cursorPos.x + dotRadius + 4.0f, cursorPos.y + ImGui::GetTextLineHeight() / 2.0f);
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 absoluteDotPos(windowPos.x + dotPos.x, windowPos.y + dotPos.y);
            ImGui::GetWindowDrawList()->AddCircleFilled(
                absoluteDotPos, 
                dotRadius, 
                dotColor, 
                10);
            
            // Add padding after dot
            ImGui::SetCursorPosX(cursorPos.x + 2 * dotRadius + 8.0f);
            
            // Display repository path
            const char *repo_path_cstr = repo_path.c_str();
            if (repo_path.size() > 38) {
                repo_path_cstr += repo_path.size() - 38;
            }
            if (ImGui::Selectable(repo_path_cstr, false, 0, ImVec2(0, 0))) {
                // if the ctrl key is pressed, open the repository as a file system card
                if (ImGui::GetIO().KeyCtrl) {
                    // Open the repository as a file system card
                    "create_card"_sfn(std::format("dir:{}", repo_path));
                } else {
                    // If a repository is selected, set it as the selected_repo
                    select(repo_path);
                }
            }
            
            ImGui::EndGroup();
        }
    }

    std::string selected_repo; 
};
