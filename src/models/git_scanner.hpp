#pragma once
#include <string>
#include <vector>
#include <map>

namespace rouen::models {
    // Forward declaration to avoid circular dependency
    enum class GitRepoStatus;
    
    class GitScanner {
    public:
        static void scanForRepositories(std::map<std::string, GitRepoStatus>& repos, std::vector<std::string>& repo_paths);
    };
}
