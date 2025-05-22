#include "git_process_helper.hpp"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>

namespace rouen::models {
    std::string GitProcessHelper::executeCommandInDirectory(const std::string& dir, const std::string& command) {
        std::string full_cmd = "cd '" + dir + "' && " + command + " 2>&1";
        std::array<char, 128> buffer;
        std::string result;
        FILE* pipe = popen(full_cmd.c_str(), "r");
        if (!pipe) return "";
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        pclose(pipe);
        return result;
    }
}
