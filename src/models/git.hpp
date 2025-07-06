#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "../helpers/debug.hpp"
#include "../helpers/config_service.hpp"
#include "git_process_helper.hpp"
#include "git_scanner.hpp"

namespace rouen::models {
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

    struct git {
        git() {
            GitScanner::scanForRepositories(repos, repo_paths);
        }

        /**
         * Update the repository status
         * 
         * @param repo_path Repository path string
         * @param status_output Optional git status command output (if already available)
         * @return true if successful, false if failed
         */
        bool updateRepoStatus(const std::string& repo_path, const std::string& status_output = "") {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return false;
            }
            
            std::string output = status_output;
            if (output.empty()) {
                // If no status output provided, get it now
                std::string git_path = CONFIG_SERVICE()->get_git_path();
                output = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " status");
                if (output.empty()) {
                    return false;
                }
            }
            
            // Determine the status based on git status output
            if (output.find("nothing to commit, working tree clean") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Clean;
            } else if (output.find("Changes to be committed") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Staged;
            } else if (output.find("Untracked files") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Untracked;
            } else if (output.find("modified:") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Modified;
            } else if (output.find("Unmerged paths") != std::string::npos || 
                    output.find("fix conflicts") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Conflict;
            } else if (output.find("HEAD detached") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Detached;
            } else {
                repos[repo_path] = GitRepoStatus::Unknown;
            }
            return true;
        }

        /**
         * Execute git status command for the given repository
         * 
         * @param repo_path Repository path
         * @return Status output as string
         */
        std::string getGitStatus(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return "";
            }
            
            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string status = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " status");
            if (!status.empty()) {
                updateRepoStatus(repo_path, status);
            }
            return status;
        }

        /**
         * Open the repository in VS Code
         * 
         * @param repo_path Repository path
         * @return true if command was executed
         */
        bool openInVSCode(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return false;
            }
            
            std::string vscode_path = CONFIG_SERVICE()->get_vscode_path();
            std::string command = std::format("\"{}\" \"{}\"", vscode_path, repo_path);
            [[maybe_unused]] int system_result = system(command.c_str());
            return true;
        }

        /**
         * Execute git push for the given repository
         * 
         * @param repo_path Repository path
         * @return Push output as string
         */
        std::string gitPush(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return "";
            }
            
            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string result = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " push");
            // Update status after push
            if (!result.empty()) {
                updateRepoStatus(repo_path);
            }
            return result;
        }

        // Getters
        const std::map<std::string, GitRepoStatus>& getRepos() const { 
            return repos; 
        }
        
        const std::vector<std::string>& getRepoPaths() const { 
            return repo_paths; 
        }
        
        GitRepoStatus getRepoStatus(const std::string& repo_path) const {
            auto it = repos.find(repo_path);
            if (it != repos.end()) {
                return it->second;
            }
            return GitRepoStatus::Unknown;
        }
        
        /**
         * Check if the current branch is ahead of its remote tracking branch
         * 
         * @param repo_path Repository path
         * @return true if branch is ahead, false otherwise
         */
        bool isBranchAhead(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return false;
            }
            
            // Get git status with porcelain format for easier parsing
            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string status = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " status -sb");
            if (status.empty()) {
                return false;
            }
            
            // Look for "[ahead" in the first line, which indicates branch is ahead
            size_t newline = status.find('\n');
            std::string firstLine = newline != std::string::npos ? status.substr(0, newline) : status;
            return firstLine.find("[ahead") != std::string::npos;
        }

    private:
        std::map<std::string, GitRepoStatus> repos;
        std::vector<std::string> repo_paths; // For maintaining sorted order
    };
}
