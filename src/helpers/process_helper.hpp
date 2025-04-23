#pragma once

#include <string>
#include <array>
#include <memory>
#include <sstream>
#include <cstdio>
#include <iostream>
#include "debug.hpp"

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
        
        // Open a pipe to read the command output using the custom deleter
        std::unique_ptr<FILE, decltype(pipeDeleter)> pipe(popen(command.c_str(), "r"), pipeDeleter);
        if (!pipe) {
            PROCESS_ERROR_FMT("Error executing command: {}", command);
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
}