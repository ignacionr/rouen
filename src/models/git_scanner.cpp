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
        // Collect directories to scan
        std::vector<std::filesystem::path> scan_roots;
        
        // 1. Check if GIT_SCAN_ROOT or DEVELOPMENT_SRC_DIR is set in the environment
        const char* env_root = std::getenv("GIT_SCAN_ROOT");
        if (!env_root) {
            env_root = std::getenv("DEVELOPMENT_SRC_DIR");
        }
        if (env_root && *env_root) {
            std::filesystem::path p(env_root);
            if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                scan_roots.push_back(p);
            }
        }
        
        // 2. Add common user development directories
        const char* home_env = std::getenv("HOME");
        if (home_env && *home_env) {
            std::filesystem::path home(home_env);
            std::vector<std::filesystem::path> common_dirs = {
                home / "src",
                home / "Development",
                home / "Projects",
                home / "Developer"
            };
            for (const auto& d : common_dirs) {
                if (std::filesystem::exists(d) && std::filesystem::is_directory(d)) {
                    scan_roots.push_back(d);
                }
            }
        }
        
        // 3. Add current working directory (skip root directory)
        try {
            std::filesystem::path cwd = std::filesystem::current_path();
            if (cwd != "/" && cwd != "C:\\" && cwd != "c:\\") {
                scan_roots.push_back(cwd);
            }
        } catch (...) {} // NOLINT(bugprone-empty-catch)
        
        // Remove duplicates while keeping order
        std::vector<std::filesystem::path> unique_roots;
        for (const auto& root : scan_roots) {
            try {
                auto canonical_root = std::filesystem::weakly_canonical(root);
                if (std::find(unique_roots.begin(), unique_roots.end(), canonical_root) == unique_roots.end()) {
                    unique_roots.push_back(canonical_root);
                }
            } catch (...) {
                if (std::find(unique_roots.begin(), unique_roots.end(), root) == unique_roots.end()) {
                    unique_roots.push_back(root);
                }
            }
        }
        
        // Helper lambda for bounded recursive scanning
        auto scan_dir = [&](auto& self, const std::filesystem::path& dir, int depth) -> void {
            if (depth > 2) return; // Limit depth to 2 (e.g. root/group/repo)
            
            try {
                // If this directory is a git repository, add it and stop searching deeper
                if (std::filesystem::exists(dir / ".git")) {
                    repos[dir.string()] = GitRepoStatus::Unknown;
                    return;
                }
                
                // Otherwise search subdirectories
                for (const auto& entry : std::filesystem::directory_iterator(
                         dir, 
                         std::filesystem::directory_options::skip_permission_denied)) {
                    if (entry.is_directory()) {
                        // Skip hidden directories (like .git, .vscode, etc.) to optimize search
                        std::string name = entry.path().filename().string();
                        if (name.empty() || name[0] == '.') {
                            continue;
                        }
                        self(self, entry.path(), depth + 1);
                    }
                }
            } catch (...) { // NOLINT(bugprone-empty-catch)
                // Ignore permission or other access errors
            }
        };
        
        // Run the scan on all collected roots
        for (const auto& root : unique_roots) {
            scan_dir(scan_dir, root, 0);
        }
        
        // Create a sorted list of keys for display
        repo_paths.clear();
        for (const auto& [path, status] : repos) {
            repo_paths.push_back(path);
        }
        std::sort(repo_paths.begin(), repo_paths.end());
    }
}
