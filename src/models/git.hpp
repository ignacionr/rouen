#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "../helpers/process_helper.hpp"

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
            scanForRepositories();
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
                                updateRepoStatus(repo_path, status_output);
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
                output = ProcessHelper::executeCommandInDirectory(repo_path, "git status");
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
            
            std::string status = ProcessHelper::executeCommandInDirectory(repo_path, "git status");
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
            
            std::string command = std::format("code \"{}\"", repo_path);
            system(command.c_str());
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
            
            std::string result = ProcessHelper::executeCommandInDirectory(repo_path, "git push");
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
            std::string status = ProcessHelper::executeCommandInDirectory(repo_path, "git status -sb");
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