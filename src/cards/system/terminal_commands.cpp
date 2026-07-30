#include "terminal_commands.hpp"
#include "cards/system/terminal_output.hpp"
#include "fetch.hpp"
#include "llm_host.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

namespace rouen::cards {

void TerminalCommands::execute_command(const std::string& command, bool use_llm, 
                                     TerminalOutput& output, const std::string& cwd,
                                     std::vector<std::string>& history, size_t& history_index,
                                     std::atomic<bool>& is_command_running, bool use_interactive_bash,
                                     bool& show_sudo_prompt, std::string& sudo_command,
                                     std::string* out_actual_command) {
    // If use_llm is true, generate a shell command using unified LLM
    std::string cmd_to_execute = command;
    
    if (use_llm) {
        std::string const generated_cmd = generate_shell_command(command, output);
        if (!generated_cmd.empty()) {
            cmd_to_execute = generated_cmd;
        } else {
            // If command generation failed, fallback to original command
            output.add_to_output("Failed to generate command with AI. Using original command.", OutputType::StdErr);
        }
    }
    if (out_actual_command) {
        *out_actual_command = cmd_to_execute;
    }
    // Check if command needs sudo privileges
    if (cmd_to_execute.starts_with("sudo ")) {
        // Remember the command without sudo for later execution
        sudo_command = cmd_to_execute.substr(5);
        
        // Inform the user about the sudo prompt
        output.add_to_output("This command requires sudo privileges. Please enter your password:", OutputType::System);
        
        // Show the sudo password prompt
        show_sudo_prompt = true;
        return;
    }
    
    // Add command to output buffer and history
    output.add_to_output(cmd_to_execute, OutputType::Command);
    
    // Add to command history
    history.push_back(cmd_to_execute);
    if (history.size() > 50) {  // Limit history size
        history.erase(history.begin());
    }
    history_index = history.size();
    
    // Handle special built-in commands first
    if (cmd_to_execute == "clear" || cmd_to_execute == "cls") {
        output.clear_terminal(cwd);
        return;
    }
    
    // For interactive bash mode
    if (use_interactive_bash) {
        // Set command as running
        is_command_running.store(true);
    } else {
        // For non-interactive mode or if interactive mode is not available
        // Use the traditional command execution
        execute_external_command(cmd_to_execute, cwd, is_command_running, output);
    }
}

std::string TerminalCommands::generate_shell_command(const std::string& description, TerminalOutput& output) {
    // Get global LLM configuration
    if (!rouen::helpers::LLMConfig::is_configured()) {
        auto settings = rouen::helpers::LLMConfig::get_current_config();
        std::string env_name = rouen::helpers::LLMConfig::get_api_key_env_name(settings.provider);
        output.add_to_output(std::format("Error: Global LLM is not configured. Please set the environment variable: {}", env_name), OutputType::StdErr);
        return "";
    }

    auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance();
    if (!llm_instance) {
        output.add_to_output("Error: Failed to create LLM instance.", OutputType::StdErr);
        return "";
    }
    
    auto settings = rouen::helpers::LLMConfig::get_current_config();
    
    try {
        // Add a loader to indicate processing
        output.add_to_output(std::format("Generating shell command with {} AI...", rouen::helpers::LLMConfig::provider_to_string(settings.provider)), OutputType::System);
        
        // Add system instructions for the AI
        llm_instance->add_instructions(
            "You are a Linux shell command generator. Convert the user's natural language request into "
            "the most appropriate bash command. Respond ONLY with the exact command, without any explanations, "
            "backticks, markdown formatting or additional text. Only provide a bash command that can be executed "
            "directly in a Linux terminal. Ensure the command is safe and efficient."
        );
        
        // Send the request to the configured LLM
        http::fetch fetcher;
        auto response = llm_instance->sendMessage(
            description,
            [&fetcher](const std::string& url, const std::string& data, auto header_client) {
                return fetcher.post(url, data, header_client);
            },
            "user",
            settings.model_name
        );
        
        if (response.choices.empty() || response.choices[0].message.content.empty()) {
            return "";
        }
        
        // Extract the command from the response
        std::string command = response.choices[0].message.content;
        
        // Clean up the command (remove quotes, backticks, etc.)
        command = command.substr(command.find_first_not_of(" \t\n`"));
        command = command.substr(0, command.find_last_not_of(" \t\n`") + 1);
        
        return command;
    } catch (const std::exception& e) {
        output.add_to_output(std::format("Error generating command: {}", e.what()), OutputType::StdErr);
        return "";
    }
}

void TerminalCommands::execute_external_command(const std::string& command, 
                                              const std::string& cwd,
                                              std::atomic<bool>& is_command_running,
                                              TerminalOutput& output) {
    // First terminate any running process
    terminate_current_process(is_command_running);
    
    // Set command as running
    is_command_running.store(true);
    
    // Create full command with the working directory
    // Append 2>&1 to redirect stderr to stdout so we capture both
    std::string full_command = std::format("cd \"{}\" && {} 2>&1", cwd, command);
    
    // Launch process
    TERM_INFO_FMT("Executing command: {}", full_command);
    
    {
        // Lock the command_pipe_mutex to ensure thread safety
        std::lock_guard<std::mutex> const lock(command_pipe_mutex);
        
#ifdef _WIN32
        command_pipe = _popen(full_command.c_str(), "r");
#else
        command_pipe = popen(full_command.c_str(), "r"); // NOLINT(cert-env33-c)
#endif
        
        if (!command_pipe) {
            output.add_to_output("Failed to execute command.", OutputType::StdErr);
            is_command_running.store(false);
            output.add_to_output("", OutputType::Blank);
            output.add_prompt(cwd);
            return;
        }
    }
    
    // Reset stop flag and start background thread to read the output
    should_stop_thread = false;
    output_reader_thread = std::thread([this, &output]() {
        char buffer[4096];
        while (!should_stop_thread.load()) {
            // Safely access the command_pipe with proper synchronization
            FILE* pipe_to_read = nullptr;
            {
                std::lock_guard<std::mutex> const lock(command_pipe_mutex);
                if (command_pipe == nullptr) {
                    break; // Pipe has been closed
                }
                pipe_to_read = command_pipe;
            }
            
            if (fgets(buffer, sizeof(buffer), pipe_to_read) != nullptr) {
                // Remove trailing newline if present
                size_t const len = strlen(buffer);
                if (len > 0 && buffer[len-1] == '\n') {
                    buffer[len-1] = '\0';
                }
                
                output.add_to_output(buffer, OutputType::StdOut);
            } else {
                // End of output or error
                break;
            }
        }
    });
}

void TerminalCommands::check_command_output(bool use_interactive_bash, std::atomic<bool>& is_command_running, 
                                          TerminalOutput& output) {
    // For interactive bash mode, the command status is handled in the reader thread
    if (use_interactive_bash) return;
    
    // Check if command has finished in non-interactive mode
    if (is_command_running.load()) {
        bool should_close = false;
        
        {
            std::lock_guard<std::mutex> const lock(command_pipe_mutex);
            if (command_pipe != nullptr) {
                // Don't close the pipe here, just check if we should
                should_close = true;
            }
        }
        
        if (should_close) {
            // We need to close the pipe in a thread-safe manner
            FILE* pipe_to_close = nullptr;
            {
                std::lock_guard<std::mutex> const lock(command_pipe_mutex);
                pipe_to_close = command_pipe;
                command_pipe = nullptr; // Prevent other threads from using it
            }
            
            if (pipe_to_close) {
#ifdef _WIN32
                _pclose(pipe_to_close);
#else
                pclose(pipe_to_close);
#endif
                // Command has finished
                is_command_running.store(false);
                
                // Add blank line and prompt after command completes
                output.add_to_output("", OutputType::Blank);
                output.add_prompt(""); // The prompt will contain the working directory
                
                // Terminate the reader thread
                if (output_reader_thread.joinable()) {
                    should_stop_thread = true;
                }
            }
        }
    }
}

void TerminalCommands::terminate_current_process(std::atomic<bool>& is_command_running) {
    if (is_command_running.load()) {
        // Ask the thread to stop
        if (output_reader_thread.joinable()) {
            should_stop_thread = true;
            
            // Give the thread a moment to notice the stop request
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Close the pipe in a thread-safe manner
        FILE* pipe_to_close = nullptr;
        {
            std::lock_guard<std::mutex> const lock(command_pipe_mutex);
            pipe_to_close = command_pipe;
            command_pipe = nullptr; // Prevent other threads from using it
        }
        
        // Close the pipe
        if (pipe_to_close) {
#ifdef _WIN32
            _pclose(pipe_to_close);
#else
            pclose(pipe_to_close);
#endif
        }
        
        is_command_running.store(false);
        
        // Wait for the thread to finish
        if (output_reader_thread.joinable()) {
            output_reader_thread.join();
        }
    }
}

} // namespace rouen::cards
