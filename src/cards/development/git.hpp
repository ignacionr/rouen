#pragma once

#include <format>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <memory>

#include "../../helpers/imgui_include.hpp"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../models/git.hpp" // Include the git model
#include "../../registrar.hpp"
#include "../../../external/IconsMaterialDesign.h"
#include "../../helpers/glaze_include.hpp"

struct git: public card {
    std::string repo_status; // Store the git status result
    std::unique_ptr<rouen::models::git> git_model; // Git model for handling git operations
    std::string target_repo; // Optional target repository path
    std::string last_commit_message; // AI-generated commit message used most recently
    
    git(std::string_view repo_path = "") : target_repo(repo_path) {
        colors[0] = {0.37f, 0.53f, 0.71f, 1.0f}; // Changed from orange to blue accent color (first_color)
        colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color (second_color)
        
        // Set card width
        width = 600.0f;
        
        // Create git model
        git_model = std::make_unique<rouen::models::git>();
        
        if (!target_repo.empty()) {
            // If a specific repository is provided, select it immediately
            name(std::format("Git: {}", std::filesystem::path(target_repo).filename().string()));
            selected_repo = target_repo;
            
            // Ensure the target repository is recognized by the git model
            // by adding it if it's not already there
            git_model->addRepository(target_repo);
        } else {
            name("Git Repos");
        }
    }

    std::string get_uri() const override {
        if (!target_repo.empty()) {
            return std::format("git:{}", target_repo);
        }
        return "git";
    }
    
    // Override to provide MCP functions
    std::vector<mcp_function> get_mcp_functions() const override {
        return {
            mcp_function(
                "get_repository_status",
                "Get status of git repositories. Returns the current state of git repositories. Status values: 'clean' (no changes), 'modified' (uncommitted changes), 'untracked' (contains untracked files), 'staged' (changes ready to commit), 'conflict' (merge conflicts), 'detached' (detached HEAD), 'unknown' (status unclear). If repo_path is provided, returns status for that specific repo, otherwise returns status for all repositories.",
                R"({"type":"object","properties":{"repo_path":{"type":"string","description":"Optional: specific repository path to check"}}})",
                [this](const std::string& params) { return get_repository_status_json(params); }
            ),
            mcp_function(
                "get_repositories_needing_push",
                "Get list of repositories that have commits ahead of their remote branches and need to be pushed.",
                R"({"type": "object", "properties": {}})",
                [this](const std::string&) { return get_repositories_needing_push_json(); }
            ),
            mcp_function(
                "get_modified_repositories", 
                "Get repositories with uncommitted changes (modified, staged, or untracked files).",
                R"({"type": "object", "properties": {}})",
                [this](const std::string&) { return get_modified_repositories_json(); }
            )
        };
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
        ImGui::BeginChild("GitStatus", ImVec2(0, ImGui::GetWindowHeight() - 180.0f), true);
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
        
        // Add "Open in Terminal" button
        ImGui::SameLine();
        if (ImGui::SmallButton("Terminal")) {
            "create_card"_sfn(std::format("terminal:{}", selected_repo));
        }
        
        // Add GitHub integration button
        ImGui::SameLine();
        if (ImGui::SmallButton("GitHub CI")) {
            // Try to determine if this is a GitHub repository
            std::string git_remote = git_model->getGitRemote(selected_repo);
            if (git_remote.find("github.com") != std::string::npos) {
                // Extract repo name from remote URL
                std::string repo_name = extract_github_repo_name(git_remote);
                if (!repo_name.empty()) {
                    // This could create a GitHub CI card or switch to existing one
                    "create_card"_sfn(std::format("github-ci:{}", repo_name));
                }
            }
        }
        
        ImGui::SameLine();
        if (ImGui::SmallButton("Push")) {
            prepend_action_result("Push", git_model->gitPush(selected_repo));
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Pull")) {
            prepend_action_result("Pull", git_model->gitPull(selected_repo));
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Commit All")) {
            commit_all_with_ai_message();
        }

        if (!last_commit_message.empty()) {
            ImGui::Separator();
            ImGui::Text("Last AI commit message:");
            ImGui::TextWrapped("%s", last_commit_message.c_str());
        }
        
        // GitHub status indicator (if this is a GitHub repo)
        render_github_status_indicator();
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
            if (repo_path.size() > 80) {
                repo_path_cstr += repo_path.size() - 80;
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
    
private:
    void prepend_action_result(const std::string& action_name, const std::string& command_output) {
        updateRepoStatus();

        std::string output = command_output;
        if (output.empty()) {
            output = std::format("{} completed.", action_name);
        }

        repo_status = std::format("{} result:\n{}\n\n{}", action_name, output, repo_status);
    }

    static std::string trim_copy(std::string value) {
        auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        return value;
    }

    std::string generate_ai_commit_message(const std::string& staged_context) {
        if (!rouen::helpers::LLMConfig::is_configured()) {
            return {};
        }

        auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance();
        if (!llm_instance) {
            return {};
        }

        auto settings = rouen::helpers::LLMConfig::get_current_config();
        auto fetcher = std::make_shared<http::fetch>();

        llm_instance->add_instructions(
            "You are an expert software engineer writing git commit messages. "
            "Return ONLY a commit message ready to pass to `git commit`. "
            "Use imperative mood, be specific, and keep the first line under 72 characters. "
            "If additional detail is helpful, include a blank line followed by concise bullet points. "
            "Do not wrap the message in quotes or markdown."
        );

        std::string prompt = std::format(
            "Repository: {}\n\n"
            "Write a git commit message for these staged changes.\n\n"
            "Staged context:\n{}\n",
            selected_repo,
            staged_context
        );

        auto response = llm_instance->sendMessage(
            prompt,
            [fetcher](const std::string& url, const std::string& data, auto header_client) {
                return fetcher->post(url, data, header_client);
            },
            "user",
            settings.model_name
        );

        if (response.choices.empty() || response.choices[0].message.content.empty()) {
            return {};
        }

        return trim_copy(response.choices[0].message.content);
    }

    void commit_all_with_ai_message() {
        last_commit_message.clear();

        std::string add_result = git_model->gitAddAll(selected_repo);
        std::string staged_context = git_model->getCachedDiff(selected_repo);

        if (!git_model->hasStagedChanges(selected_repo)) {
            prepend_action_result(
                "Commit All",
                add_result.empty() ? "No changes to commit after staging." : add_result + "\nNo changes to commit after staging."
            );
            return;
        }

        constexpr std::size_t max_context_length = 12000;
        if (staged_context.size() > max_context_length) {
            staged_context.resize(max_context_length);
            staged_context += "\n\n[truncated]";
        }

        std::string commit_message;
        try {
            commit_message = generate_ai_commit_message(staged_context);
        } catch (const std::exception& e) {
            prepend_action_result("Commit All", std::format("Failed to generate AI commit message: {}", e.what()));
            return;
        }

        commit_message = trim_copy(commit_message);
        if (commit_message.empty()) {
            prepend_action_result("Commit All", "AI did not return a usable commit message.");
            return;
        }

        last_commit_message = commit_message;
        std::string commit_result = git_model->gitCommit(selected_repo, commit_message);
        prepend_action_result(
            "Commit All",
            std::format("Generated commit message:\n{}\n\n{}", commit_message, commit_result)
        );
    }

    // Helper to extract GitHub repository name from remote URL
    std::string extract_github_repo_name(const std::string& remote_url) {
        // Handle various GitHub URL formats:
        // https://github.com/user/repo.git
        // git@github.com:user/repo.git
        // https://github.com/user/repo
        
        std::string repo_name;
        size_t github_pos = remote_url.find("github.com");
        if (github_pos == std::string::npos) {
            return "";
        }
        
        // Find the start of the repository path
        size_t path_start = remote_url.find('/', github_pos);
        if (path_start == std::string::npos) {
            path_start = remote_url.find(':', github_pos);
        }
        if (path_start == std::string::npos) {
            return "";
        }
        
        path_start++; // Skip the separator
        
        // Find the end (remove .git if present)
        std::string path = remote_url.substr(path_start);
        if (path.ends_with(".git")) {
            path = path.substr(0, path.length() - 4);
        }
        
        return path;
    }
    
    void render_github_status_indicator() {
        // Check if this repository has a GitHub remote
        std::string git_remote = git_model->getGitRemote(selected_repo);
        if (git_remote.find("github.com") == std::string::npos) {
            return; // Not a GitHub repository
        }
        
        ImGui::Separator();
        ImGui::TextColored(colors[0], ICON_MD_CLOUD " GitHub Integration");
        
        std::string repo_name = extract_github_repo_name(git_remote);
        if (!repo_name.empty()) {
            ImGui::Text("Repository: %s", repo_name.c_str());
            
            // Quick actions for GitHub integration
            if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " Open on GitHub")) {
                std::string github_url = std::format("https://github.com/{}", repo_name);
                [[maybe_unused]] int system_result = system(("open " + github_url).c_str()); // macOS specific - should use platform utils
            }
            
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_BUILD " CI/CD Status")) {
                std::string actions_url = std::format("https://github.com/{}/actions", repo_name);
                [[maybe_unused]] int system_result = system(("open " + actions_url).c_str()); // macOS specific - should use platform utils
            }
            
            // Status indicators (placeholder - would need GitHub API integration)
            ImGui::Text("CI Status: %s", "Click CI/CD Status to view");
            
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Could not parse repository name");
        }
    }

private:
    // MCP helper functions for JSON responses
    std::string get_repository_status_json(const std::string& params) const {
        try {
            std::string requested_repo;
            
            // Parse params if provided (simple JSON parsing for now)
            if (!params.empty() && params.find("repo_path") != std::string::npos) {
                // Simple extraction - this could be improved with proper JSON parsing
                auto start = params.find("\"repo_path\"");
                if (start != std::string::npos) {
                    start = params.find(":", start);
                    if (start != std::string::npos) {
                        start = params.find("\"", start);
                        if (start != std::string::npos) {
                            start++;
                            auto end = params.find("\"", start);
                            if (end != std::string::npos) {
                                requested_repo = params.substr(start, end - start);
                            }
                        }
                    }
                }
            }
            
            std::string result = "{\"success\":true,\"repositories\":[";
            bool first = true;
            const auto& repos = git_model->getRepos();
            
            if (!requested_repo.empty()) {
                // Return status for specific repository
                auto repo_it = repos.find(requested_repo);
                if (repo_it != repos.end()) {
                    // Refresh the status before returning it
                    git_model->getGitStatus(requested_repo);
                    // Get the updated status
                    auto updated_repos = git_model->getRepos();
                    auto updated_it = updated_repos.find(requested_repo);
                    if (updated_it != updated_repos.end()) {
                        result += "{\"path\":\"" + requested_repo + "\",";
                        result += "\"status\":\"" + git_status_to_string(updated_it->second) + "\",";
                        result += "\"ahead\":" + std::string(git_model->isBranchAhead(requested_repo) ? "true" : "false") + "}";
                    }
                } else {
                    return "{\"success\":false,\"error\":\"Repository not found: " + requested_repo + "\"}";
                }
            } else {
                // Return status for all repositories
                for (const auto& [path, status] : repos) {
                    // Refresh each repository's status
                    git_model->getGitStatus(path);
                }
                // Get the updated statuses
                const auto& updated_repos = git_model->getRepos();
                for (const auto& [path, status] : updated_repos) {
                    if (!first) result += ",";
                    result += "{\"path\":\"" + path + "\",";
                    result += "\"status\":\"" + git_status_to_string(status) + "\",";
                    result += "\"ahead\":" + std::string(git_model->isBranchAhead(path) ? "true" : "false") + "}";
                    first = false;
                }
            }
            
            result += "]}";
            return result;
        } catch (const std::exception& e) {
            return "{\"success\":false,\"error\":\"Error getting repository status: " + std::string(e.what()) + "\"}";
        }
    }
    
    std::string get_repositories_needing_push_json() const {
        try {
            std::string result = "{\"success\":true,\"repositories\":[";
            bool first = true;
            const auto& repos = git_model->getRepos();
            
            for (const auto& [path, status] : repos) {
                if (git_model->isBranchAhead(path)) {
                    if (!first) result += ",";
                    result += "{\"path\":\"" + path + "\",";
                    result += "\"status\":\"" + git_status_to_string(status) + "\"}";
                    first = false;
                }
            }
            
            result += "]}";
            return result;
        } catch (const std::exception& e) {
            return "{\"success\":false,\"error\":\"Error getting repositories needing push: " + std::string(e.what()) + "\"}";
        }
    }
    
    std::string get_modified_repositories_json() const {
        try {
            std::string result = "{\"success\":true,\"repositories\":[";
            bool first = true;
            const auto& repos = git_model->getRepos();
            
            for (const auto& [path, status] : repos) {
                // Include repos with any changes (not clean)
                if (status != rouen::models::GitRepoStatus::Clean && 
                    status != rouen::models::GitRepoStatus::Unknown) {
                    if (!first) result += ",";
                    result += "{\"path\":\"" + path + "\",";
                    result += "\"status\":\"" + git_status_to_string(status) + "\",";
                    result += "\"ahead\":" + std::string(git_model->isBranchAhead(path) ? "true" : "false") + "}";
                    first = false;
                }
            }
            
            result += "]}";
            return result;
        } catch (const std::exception& e) {
            return "{\"success\":false,\"error\":\"Error getting modified repositories: " + std::string(e.what()) + "\"}";
        }
    }
    
    static std::string git_status_to_string(rouen::models::GitRepoStatus status) {
        switch (status) {
            case rouen::models::GitRepoStatus::Clean: return "clean";
            case rouen::models::GitRepoStatus::Modified: return "modified";
            case rouen::models::GitRepoStatus::Untracked: return "untracked";
            case rouen::models::GitRepoStatus::Staged: return "staged";
            case rouen::models::GitRepoStatus::Conflict: return "conflict";
            case rouen::models::GitRepoStatus::Detached: return "detached";
            case rouen::models::GitRepoStatus::Unknown: return "unknown";
            default: return "unknown";
        }
    }
};
