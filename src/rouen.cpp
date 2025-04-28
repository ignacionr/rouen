// 1. Standard includes in alphabetic order
#include <iostream>
#include <mutex>
#include <thread>

// 2. Libraries used in the project, in alphabetic order
#include "imgui.h"
#include <SDL.h>  // Changed from SDL2/SDL.h to SDL.h for macOS compatibility

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "helpers/debug.hpp"
#include "helpers/deferred_operations.hpp" // For deferred operations
#include "helpers/notify_service.hpp"
#include "helpers/process_helper.hpp" // Added this include for ProcessHelper
#include "main_wnd.hpp"
#include "registrar.hpp"

int main() {
    notify_service notify; // Initialize the notify service
    
    // Register the run_command function - non-blocking with incremental output
    registrar::add<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>(
        "run_command", 
        std::make_shared<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>(
            [](std::string const& cmd, std::shared_ptr<std::function<void(std::string)>> callback) {
                // Launch the command in a background thread to avoid freezing the UI
                std::thread([cmd, callback]() {
                    // Create a pipe to the command
                    FILE* pipe = popen(cmd.c_str(), "r");
                    if (!pipe) {
                        if (callback) {
                            (*callback)("Error: Failed to execute command");
                        }
                        return;
                    }
                    
                    // Buffer for reading output
                    std::array<char, 128> buffer;
                    std::string current_output;
                    bool has_output = false;
                    
                    // Read output incrementally
                    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                        has_output = true;
                        current_output += buffer.data();
                        
                        // Send the current output to the callback
                        if (callback) {
                            (*callback)(current_output);
                        }
                    }
                    
                    // Get the exit status of the command
                    int status = pclose(pipe);
                    
                    // If there was no output but the command completed, provide a default message
                    if (!has_output) {
                        if (WIFEXITED(status)) {
                            int exit_status = WEXITSTATUS(status);
                            if (exit_status == 0) {
                                current_output = "Command completed successfully with no output.";
                            } else {
                                current_output = std::format("Command failed with exit code: {}", exit_status);
                            }
                        } else {
                            current_output = "Command terminated abnormally.";
                        }
                        
                        // Send the final status message to the callback
                        if (callback) {
                            (*callback)(current_output);
                        }
                    } else {
                        // For commands with output, append the exit status
                        std::string status_message;
                        if (WIFEXITED(status)) {
                            int exit_status = WEXITSTATUS(status);
                            status_message = std::format("\n\nProcess exited with code: {}", exit_status);
                        } else if (WIFSIGNALED(status)) {
                            int term_signal = WTERMSIG(status);
                            status_message = std::format("\n\nProcess terminated by signal: {}", term_signal);
                        } else {
                            status_message = "\n\nProcess completed.";
                        }
                        
                        current_output += status_message;
                        
                        // Send the final output with status to the callback
                        if (callback) {
                            (*callback)(current_output);
                        }
                    }
                    
                    // Add a small marker to indicate process completion
                    if (callback) {
                        // Send a specially marked message that the card can detect to know the process is definitely complete
                        (*callback)(current_output + "\n<PROCESS_COMPLETED>");
                    }
                }).detach(); // Detach the thread so it runs independently
            }
        )
    );
    
    // Create and initialize the main window
    main_wnd window;
    if (!window.initialize()) {
        SYS_ERROR("Failed to initialize window");
        return -1;
    }
    
    // Run the main loop
    window.run();

    return 0;
}
