#include "git_scanner.hpp"
#include "git.hpp"
#include "git_process_helper.hpp"
#include "../helpers/debug.hpp"
#include "../helpers/config_service.hpp"
#include <filesystem>
#include <cstdlib>
#include <algorithm>

namespace rouen::models {
    void GitScanner::scanForRepositories(std::map<std::string, GitRepoStatus>& repos, std::vector<std::string>& repo_paths) {
        // For now, just scan the current working directory and immediate subdirectories
        // to avoid hanging on large directory trees
        try {
            std::filesystem::path cwd = std::filesystem::current_path();
            
            // Check if current directory is a git repo
            if (std::filesystem::exists(cwd / ".git")) {
                repos[cwd.string()] = GitRepoStatus::Unknown;
            }
            
            // Check immediate subdirectories for git repos (depth 1 only)
            for (const auto& entry : std::filesystem::directory_iterator(
                     cwd, 
                     std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_directory()) {
                    std::filesystem::path git_dir = entry.path() / ".git";
                    if (std::filesystem::exists(git_dir)) {
                        repos[entry.path().string()] = GitRepoStatus::Unknown;
                    }
                }
            }
            
        } catch (const std::exception& /* e */) {
            // If all else fails, just create an empty repository list
        }
        
        // Create a sorted list of keys for display
        repo_paths.clear();
        for (const auto& [path, status] : repos) {
            repo_paths.push_back(path);
        }
        std::sort(repo_paths.begin(), repo_paths.end());
    }
}
