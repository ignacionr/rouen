#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <imgui/imgui.h>
#include "card.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <format>
#include "../helpers/texture_helper.hpp"
#include "../animation/slide.hpp"
#include "../models/git.hpp" // Include the git model

struct git: public card {
    std::string repo_status; // Store the git status result
    std::unique_ptr<rouen::models::git> git_model; // Git model for handling git operations
    
    git() {
        first_color = {1.0f, 0.341f, 0.133f, 1.0f}; // git Orange
        second_color = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
        
        // Create git model
        git_model = std::make_unique<rouen::models::git>();
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
        ImGui::BeginChild("GitStatus", ImVec2(0, size.y - 130), true);
        ImGui::TextWrapped("%s", repo_status.c_str());
        ImGui::EndChild();

        // Repository actions
        if (ImGui::Button("Refresh Status")) {
            updateRepoStatus();
        }
        
        // Add "Open in VS Code" button
        ImGui::SameLine();
        if (ImGui::Button("Open in VS Code")) {
            git_model->openInVSCode(selected_repo);
        }
    }

    bool render() override {
        // Create a window with non-collapsible flags (ImGuiWindowFlags_NoCollapse)
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize;
        bool is_open = true;
        if (ImGui::Begin(std::format("Git Repos##{}", static_cast<const void*>(this)).c_str(), &is_open, window_flags)) {        
            is_open &= run_focused_handlers();
            if (selected_repo.empty()) {
                render_index();
            } else {
                render_selected();
            }
        }
        ImGui::End();
        return is_open;
    }
    
    void render_index() {
        // Get repository data from the model
        const auto& repos = git_model->getRepos();
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
                // If a repository is selected, set it as the selected_repo
                select(repo_path);
            }
            
            ImGui::EndGroup();
        }
    }

    std::string selected_repo; 
};
