#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <imgui/imgui.h>
#include "card.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <cstdio>
#include <map>
#include <format>
#include "../helpers/texture_helper.hpp"
#include "../helpers/process_helper.hpp"

// Enum class for git repository statuses
enum class GitRepoStatus {
    Unknown,
    Clean,
    Modified,
    Untracked,
    Staged,
    Conflict,
    Detached
};

struct git: public card {
    SDL_Texture* image_texture = nullptr; // Changed from GLuint to SDL_Texture*
    int image_width = 0;
    int image_height = 0;
    bool image_loaded = false;
    SDL_Renderer* renderer = nullptr; // Add renderer member
    std::string repo_status; // Store the git status result
    
    git(SDL_Renderer* renderer) : renderer(renderer) {
        first_color = {1.0f, 0.341f, 0.133f, 1.0f}; // git Orange
        second_color = {0.251f, 0.878f, 0.816f, 1.0f}; // Turquoise color
        
        // Load the image when the card is created
        image_texture = TextureHelper::loadTextureFromFile(renderer, "img/git.jpeg", image_width, image_height);
        image_loaded = (image_texture != nullptr);

        // Scan for git repositories
        scanForRepositories();
    }
    
    ~git() {
        // Clean up the texture when done
        if (image_texture) {
            SDL_DestroyTexture(image_texture); // Changed from glDeleteTextures
            image_texture = nullptr;
        }
    }
    
    /**
     * Scan the filesystem for Git repositories
     * Searches under the user's HOME directory for .git folders
     * and populates the repos map with the parent directories and their statuses
     */
    void scanForRepositories() {
        const char* home = std::getenv("HOME");
        if (home) {
            try {
                std::filesystem::path homeDir(home);
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     homeDir, 
                     std::filesystem::directory_options::skip_permission_denied)) {
                    if (entry.is_directory() && entry.path().filename() == ".git") {
                        std::string repo_path = entry.path().parent_path().string();
                        repos[repo_path] = GitRepoStatus::Unknown; // Set initial status
                        
                        // Query the git status immediately
                        std::string status_output = ProcessHelper::executeCommandInDirectory(repo_path, "git status");
                        if (!status_output.empty()) {
                            // Determine the status based on git status output
                            if (status_output.find("nothing to commit, working tree clean") != std::string::npos) {
                                repos[repo_path] = GitRepoStatus::Clean;
                            } else if (status_output.find("Changes to be committed") != std::string::npos) {
                                repos[repo_path] = GitRepoStatus::Staged;
                            } else if (status_output.find("Untracked files") != std::string::npos) {
                                repos[repo_path] = GitRepoStatus::Untracked;
                            } else if (status_output.find("modified:") != std::string::npos) {
                                repos[repo_path] = GitRepoStatus::Modified;
                            } else if (status_output.find("Unmerged paths") != std::string::npos || 
                                      status_output.find("fix conflicts") != std::string::npos) {
                                repos[repo_path] = GitRepoStatus::Conflict;
                            } else if (status_output.find("HEAD detached") != std::string::npos) {
                                repos[repo_path] = GitRepoStatus::Detached;
                            }
                        }
                    }
                }
                
                // Create a sorted list of keys for display
                repo_paths.clear();
                for (const auto& [path, status] : repos) {
                    repo_paths.push_back(path);
                }
                std::sort(repo_paths.begin(), repo_paths.end());
                
            } catch (const std::exception& e) {
                std::cerr << "Error scanning for repositories: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Error: HOME environment variable not set" << std::endl;
        }
    }
    
    /**
     * Select a repository and get its current git status
     * 
     * @param repo_path Repository path string
     * @return true if successful, false if failed
     */
    bool select(const std::string& repo_path) {
        if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
            return false;
        }
        
        selected_repo = repo_path;
        return updateRepoStatus();
    }
    
    /**
     * Update the repository status by running git status command
     * and determine the GitRepoStatus enum value
     * 
     * @return true if successful, false if failed
     */
    bool updateRepoStatus() {
        if (selected_repo.empty()) {
            return false;
        }
        
        // Use the ProcessHelper to execute git status in the repository directory
        repo_status = ProcessHelper::executeCommandInDirectory(selected_repo, "git status");
        
        if (!repo_status.empty()) {
            // Determine the status based on git status output
            if (repo_status.find("nothing to commit, working tree clean") != std::string::npos) {
                repos[selected_repo] = GitRepoStatus::Clean;
            } else if (repo_status.find("Changes to be committed") != std::string::npos) {
                repos[selected_repo] = GitRepoStatus::Staged;
            } else if (repo_status.find("Untracked files") != std::string::npos) {
                repos[selected_repo] = GitRepoStatus::Untracked;
            } else if (repo_status.find("modified:") != std::string::npos) {
                repos[selected_repo] = GitRepoStatus::Modified;
            } else if (repo_status.find("Unmerged paths") != std::string::npos || 
                       repo_status.find("fix conflicts") != std::string::npos) {
                repos[selected_repo] = GitRepoStatus::Conflict;
            } else if (repo_status.find("HEAD detached") != std::string::npos) {
                repos[selected_repo] = GitRepoStatus::Detached;
            } else {
                repos[selected_repo] = GitRepoStatus::Unknown;
            }
            return true;
        }
        
        return false;
    }
    
    // Helper function to scale and offset SVG coordinates
    inline ImVec2 ScalePoint(const ImVec2& p, const ImVec2& offset, float scaleX, float scaleY, float svgMax) {
        return ImVec2(offset.x + (p.x / svgMax) * scaleX, offset.y + (p.y / svgMax) * scaleY);
    }

    void render_selected() {
        // Back button
        if (ImGui::Button("Back to Repository List")) {
            selected_repo.clear();
            repo_status.clear();
        }
        
        ImGui::Text("Repository: %s", selected_repo.c_str());
                    
        // Display the git status
        ImGui::Separator();
        ImGui::TextWrapped("Git Status:");
        ImGui::BeginChild("GitStatus", ImVec2(0, 0), true);
        ImGui::TextWrapped("%s", repo_status.c_str());
        ImGui::EndChild();

        // Repository actions
        if (ImGui::Button("Refresh Status")) {
            updateRepoStatus();
        }
        
        // Add "Open in VS Code" button
        ImGui::SameLine();
        if (ImGui::Button("Open in VS Code")) {
            // Use system and std::format to construct and execute the command
            std::string command = std::format("code \"{}\"", selected_repo);
            system(command.c_str());
        }
    }

    void render() override {
        // Create a window with non-collapsible flags (ImGuiWindowFlags_NoCollapse)
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse;
        ImGui::Begin("Git Repos", nullptr, window_flags);
        
        if (selected_repo.empty()) {
            render_index();
        } else {
            render_selected();
        }
        ImGui::End();
    }
    
    void render_index() {
                
        // save the current position
        ImVec2 current_pos = ImGui::GetCursorPos();
        // Display the image if loaded
        if (image_loaded) {
            // Calculate display size (you can adjust this)
            float display_width = std::min(static_cast<float>(image_width), ImGui::GetWindowWidth() - 20.0f);
            float aspect_ratio = static_cast<float>(image_height) / image_width;
            float display_height = display_width * aspect_ratio;
            
            // Display the image
            ImGui::Image(
                (void*)(intptr_t)image_texture, 
                ImVec2(display_width, display_height)
            );
        }
        // Set the cursor position back to the original
        ImGui::SetCursorPos(current_pos);

        // Display the list of repositories with their statuses as colored dots
        for (const auto& repo_path : repo_paths) {
            GitRepoStatus status = repos[repo_path];
            
            // Set status color
            ImColor dotColor;
            switch (status) {
                case GitRepoStatus::Clean:
                    dotColor = ImColor(0, 255, 0, 255); // Green
                    break;
                case GitRepoStatus::Modified:
                    dotColor = ImColor(255, 165, 0, 255); // Orange
                    break;
                case GitRepoStatus::Untracked:
                    dotColor = ImColor(200, 200, 0, 255); // Yellow
                    break;
                case GitRepoStatus::Staged:
                    dotColor = ImColor(0, 200, 255, 255); // Blue
                    break;
                case GitRepoStatus::Conflict:
                    dotColor = ImColor(255, 0, 0, 255); // Red
                    break;
                case GitRepoStatus::Detached:
                    dotColor = ImColor(128, 0, 128, 255); // Purple
                    break;
                default:
                    dotColor = ImColor(255, 255, 255, 255); // White
                    break;
            }
            
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
            if (ImGui::Selectable(repo_path.c_str(), false, 0, ImVec2(0, 0))) {
                // If a repository is selected, set it as the selected_repo
                select(repo_path);
            }
            
            ImGui::EndGroup();
        }
    }

    std::map<std::string, GitRepoStatus> repos; // Changed from vector to map of path -> status
    std::vector<std::string> repo_paths; // For maintaining sorted order
    std::string selected_repo; 
};
