#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
         * Add a repository to the model if it's not already present
         * 
         * @param repo_path Repository path string
         * @return true if repository was added or already exists, false if not a valid git repo
         */
        bool addRepository(const std::string& repo_path) {
            if (repo_path.empty()) {
                return false;
            }
            
            // Check if it's actually a git repository
            if (!std::filesystem::exists(std::filesystem::path(repo_path) / ".git")) {
                return false;
            }
            
            // If not already in the list, add it
            if (repos.find(repo_path) == repos.end()) {
                repos[repo_path] = GitRepoStatus::Unknown;
                
                // Also add to the sorted repo_paths list
                auto it = std::lower_bound(repo_paths.begin(), repo_paths.end(), repo_path);
                if (it == repo_paths.end() || *it != repo_path) {
                    repo_paths.insert(it, repo_path);
                }
            }
            
            // Update the status
            return updateRepoStatus(repo_path);
        }

        /**
         * Update the repository status
         * 
         * @param repo_path Repository path string
         * @param status_output Optional git status command output (if already available)
         * @return true if successful, false if failed
         */
        bool updateRepoStatus(const std::string& repo_path, const std::string& status_output = "") {
            if (repo_path.empty()) {
                return false;
            }
            
            // If repository is not in our list, try to add it first
            if (repos.find(repo_path) == repos.end()) {
                // Check if it's a valid git repository
                if (!std::filesystem::exists(std::filesystem::path(repo_path) / ".git")) {
                    return false;
                }
                // Add it to our repository list
                repos[repo_path] = GitRepoStatus::Unknown;
                
                // Also add to the sorted repo_paths list
                auto it = std::lower_bound(repo_paths.begin(), repo_paths.end(), repo_path);
                if (it == repo_paths.end() || *it != repo_path) {
                    repo_paths.insert(it, repo_path);
                }
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
            
            // Determine the status based on git status output (priority order matters)
            if (output.find("nothing to commit, working tree clean") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Clean;
            } else if (output.find("Unmerged paths") != std::string::npos || 
                    output.find("fix conflicts") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Conflict;
            } else if (output.find("HEAD detached") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Detached;
            } else if (output.find("Changes to be committed") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Staged;
            } else if (output.find("modified:") != std::string::npos || 
                     output.find("Changes not staged") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Modified;
            } else if (output.find("Untracked files") != std::string::npos) {
                repos[repo_path] = GitRepoStatus::Untracked;
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

        std::string gitPull(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return "";
            }

            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string result = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " pull");
            if (!result.empty()) {
                updateRepoStatus(repo_path);
            }
            return result;
        }

        std::string gitAddAll(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return "";
            }

            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string result = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " add -A");
            updateRepoStatus(repo_path);
            return result;
        }

        std::string getCachedDiff(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return "";
            }

            std::string git_path = CONFIG_SERVICE()->get_git_path();
            return GitProcessHelper::executeCommandInDirectory(
                repo_path,
                git_path + " diff --cached --stat && printf '\\n---DIFF---\\n' && " + git_path + " diff --cached"
            );
        }

        bool hasStagedChanges(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return false;
            }

            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string result = GitProcessHelper::executeCommandInDirectory(
                repo_path,
                git_path + " diff --cached --name-only"
            );
            return std::any_of(result.begin(), result.end(), [](unsigned char c) {
                return !std::isspace(c);
            });
        }

        std::string gitCommit(const std::string& repo_path, const std::string& message) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end() || message.empty()) {
                return "";
            }

            std::filesystem::path temp_file = std::filesystem::temp_directory_path() /
                std::format("rouen-git-commit-{}.txt", std::hash<std::string>{}(repo_path + message));

            {
                std::ofstream commit_message_file(temp_file);
                if (!commit_message_file.is_open()) {
                    return "Failed to create temporary commit message file.";
                }
                commit_message_file << message << '\n';
            }

            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string result = GitProcessHelper::executeCommandInDirectory(
                repo_path,
                std::format("{} commit -F '{}'", git_path, temp_file.string())
            );

            std::error_code ec;
            std::filesystem::remove(temp_file, ec);

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
        
        /**
         * Get the remote URL for the repository
         * 
         * @param repo_path Repository path
         * @return Remote URL as string
         */
        std::string getGitRemote(const std::string& repo_path) {
            if (repo_path.empty() || repos.find(repo_path) == repos.end()) {
                return "";
            }
            
            std::string git_path = CONFIG_SERVICE()->get_git_path();
            std::string remote_output = GitProcessHelper::executeCommandInDirectory(repo_path, git_path + " remote get-url origin");
            
            // Remove trailing newline if present
            if (!remote_output.empty() && remote_output.back() == '\n') {
                remote_output.pop_back();
            }
            
            return remote_output;
        }

    private:
        std::map<std::string, GitRepoStatus> repos;
        std::vector<std::string> repo_paths; // For maintaining sorted order
    };
}
