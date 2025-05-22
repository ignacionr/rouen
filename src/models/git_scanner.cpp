#include "git_scanner.hpp"
#include "git.hpp"
#include "git_process_helper.hpp"
#include "../helpers/debug.hpp"
#include <filesystem>
#include <cstdlib>
#include <algorithm>

namespace rouen::models {
    void GitScanner::scanForRepositories(std::map<std::string, GitRepoStatus>& repos, std::vector<std::string>& repo_paths) {
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
                        std::string status_output = GitProcessHelper::executeCommandInDirectory(repo_path, "git status");
                        if (!status_output.empty()) {
                            // Status will be updated by the git class
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
                // Error scanning repositories
            }
        }
    }
}
