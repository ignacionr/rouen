#pragma once

#include <string>
#include <array>
#include <memory>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "debug.hpp"
#include "platform_utils.hpp"

// Add process-specific logging macros
#define PROCESS_ERROR(message) LOG_COMPONENT("PROCESS", LOG_LEVEL_ERROR, message)
#define PROCESS_ERROR_FMT(fmt, ...) PROCESS_ERROR(debug::format_log(fmt, __VA_ARGS__))

namespace ProcessHelper {
    /**
     * Execute a command and return its output as a string
     * 
     * @param command The command to execute
     * @return The command output as a string, empty string if failed
     */
    inline std::string executeCommand(const std::string& command) {
        // Create a custom deleter to avoid attributes warning
        auto pipeDeleter = [](FILE* pipe) {
            if (pipe) {
                pclose(pipe);
            }
        };
        
        std::string command_to_run = command;
        if constexpr (!rouen::platform::is_windows) {
            command_to_run = std::string(R"(export PATH="$HOME/.local/bin:$HOME/.nix-profile/bin:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin:/run/current-system/sw/bin:$PATH" && )") + command;
        }

        // Open a pipe to read the command output using the custom deleter
        std::unique_ptr<FILE, decltype(pipeDeleter)> pipe(popen(command_to_run.c_str(), "r"), pipeDeleter);
        if (!pipe) {
            PROCESS_ERROR_FMT("Error executing command: {}", command_to_run);
            return "";
        }
        
        // Read the output
        std::array<char, 128> buffer;
        std::stringstream output;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            output << buffer.data();
        }
        
        return output.str();
    }
    
    /**
     * Execute a command in a specific directory and return its output
     * 
     * @param directory The directory to execute the command in
     * @param command The command to execute
     * @return The command output as a string, empty string if failed
     */
    inline std::string executeCommandInDirectory(const std::string& directory, const std::string& command) {
        std::string fullCommand = "cd \"" + directory + "\" && " + command;
        return executeCommand(fullCommand);
    }

    /**
     * Check if a given yt-dlp executable supports the --remote-components option.
     * Caches the result to avoid invoking the process repeatedly.
     */
    inline bool ytdlp_supports_remote_components(const std::string& ytdlp_path) {
        static std::mutex mutex;
        static std::unordered_map<std::string, bool> cache;
        
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(ytdlp_path);
        if (it != cache.end()) {
            return it->second;
        }
        
        // Execute a quick check command (redirecting stderr to stdout)
        std::string test_cmd = std::format("\"{}\" --help 2>&1", ytdlp_path);
        std::string help_output = executeCommand(test_cmd);
        bool supported = (help_output.find("--remote-components") != std::string::npos);
        cache[ytdlp_path] = supported;
        return supported;
    }
}
