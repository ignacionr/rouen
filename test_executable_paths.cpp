#include "src/helpers/config_service.hpp"
#include <iostream>

int main() {
    auto config = rouen::helpers::ConfigService::instance();
    
    std::cout << "Testing executable path configuration:\n";
    std::cout << "MPV Path: " << config->get_mpv_path() << "\n";
    std::cout << "CMake Path: " << config->get_cmake_path() << "\n";
    std::cout << "Git Path: " << config->get_git_path() << "\n";
    std::cout << "Say Path: " << config->get_say_path() << "\n";
    std::cout << "Bash Path: " << config->get_bash_path() << "\n";
    std::cout << "Sudo Path: " << config->get_sudo_path() << "\n";
    std::cout << "VS Code Path: " << config->get_vscode_path() << "\n";
    std::cout << "Ping Path: " << config->get_ping_path() << "\n";
    
    return 0;
}
