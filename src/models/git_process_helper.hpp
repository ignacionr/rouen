#pragma once
#include <string>

namespace rouen::models {
    class GitProcessHelper {
    public:
        static std::string executeCommandInDirectory(const std::string& dir, const std::string& command);
    };
}
