#include "git_process_helper.hpp"
#include "../helpers/process_helper.hpp"

namespace rouen::models {
    std::string GitProcessHelper::executeCommandInDirectory(const std::string& dir, const std::string& command) {
        return ProcessHelper::executeCommandInDirectory(dir, command + " 2>&1");
    }
}
