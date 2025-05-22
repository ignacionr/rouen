#include "terminal_commands.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards {

void TerminalCommands::execute_command(const std::string& command, bool use_llm, 
                                     TerminalOutput& output, const std::string& cwd,
                                     std::vector<std::string>& history, size_t& history_index,
                                     bool& is_command_running, bool use_interactive_bash,
                                     bool& show_sudo_prompt, std::string& sudo_command) {
    // If use_llm is true, generate a shell command using Grok
    std::string cmd_to_execute = command;
    
    if (use_llm) {
        std::string generated_cmd = generate_shell_command(command, output);
        if (!generated_cmd.empty()) {
            cmd_to_execute = generated_cmd;
        } else {
            // If command generation failed, fallback to original command
            output.add_to_output("Failed to generate command with Grok. Using original command.", OutputType::StdErr);
        }
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
        is_command_running = true;
    } else {
        // For non-interactive mode or if interactive mode is not available
        // Use the traditional command execution
        execute_external_command(cmd_to_execute, cwd, is_command_running, output);
    }
}

std::string TerminalCommands::generate_shell_command(const std::string& description, TerminalOutput& output) {
    // Get Grok API key using our centralized API key manager
    std::string api_key = rouen::helpers::ApiKeys::get_grok_api_key();
    if (api_key.empty()) {
        output.add_to_output("Error: GROK_API_KEY environment variable is not set.", OutputType::StdErr);
        return "";
    }
    
    try {
        // Add a loader to indicate processing
        output.add_to_output("Generating shell command with Grok AI...", OutputType::System);
        
        // Initialize Grok client
        ignacionr::cppgpt gpt(api_key, ignacionr::cppgpt::grok_base);
        
        // Add system instructions for the AI
        gpt.add_instructions(
            "You are a Linux shell command generator. Convert the user's natural language request into "
            "the most appropriate bash command. Respond ONLY with the exact command, without any explanations, "
            "backticks, markdown formatting or additional text. Only provide a bash command that can be executed "
            "directly in a Linux terminal. Ensure the command is safe and efficient."
        );
        
        // Send the request to Grok
        http::fetch fetcher;
        auto response = gpt.sendMessage(
            description,
            [&fetcher](const std::string& url, const std::string& data, auto header_client) {
                return fetcher.post(url, data, header_client);
            },
            "user",
            "grok-2-latest"
        );
        
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
                                              bool& is_command_running,
                                              TerminalOutput& output) {
    // First terminate any running process
    terminate_current_process(is_command_running);
    
    // Set command as running
    is_command_running = true;
    
    // Create full command with the working directory
    // Append 2>&1 to redirect stderr to stdout so we capture both
    std::string full_command = std::format("cd \"{}\" && {} 2>&1", cwd, command);
    
    // Launch process
    TERM_INFO_FMT("Executing command: {}", full_command);
    
    {
        // Lock the command_pipe_mutex to ensure thread safety
        std::lock_guard<std::mutex> lock(command_pipe_mutex);
        
#ifdef _WIN32
        command_pipe = _popen(full_command.c_str(), "r");
#else
        command_pipe = popen(full_command.c_str(), "r");
#endif
        
        if (!command_pipe) {
            output.add_to_output("Failed to execute command.", OutputType::StdErr);
            is_command_running = false;
            output.add_to_output("", OutputType::Blank);
            output.add_prompt(cwd);
            return;
        }
    }
    
    // Start background thread to read the output
    output_reader_thread = std::jthread([this, &output](std::stop_token stoken) {
        char buffer[4096];
        while (!stoken.stop_requested()) {
            // Safely access the command_pipe with proper synchronization
            FILE* pipe_to_read = nullptr;
            {
                std::lock_guard<std::mutex> lock(command_pipe_mutex);
                if (command_pipe == nullptr) {
                    break; // Pipe has been closed
                }
                pipe_to_read = command_pipe;
            }
            
            if (fgets(buffer, sizeof(buffer), pipe_to_read) != nullptr) {
                // Remove trailing newline if present
                size_t len = strlen(buffer);
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

void TerminalCommands::check_command_output(bool use_interactive_bash, bool& is_command_running, 
                                          TerminalOutput& output) {
    // For interactive bash mode, the command status is handled in the reader thread
    if (use_interactive_bash) return;
    
    // Check if command has finished in non-interactive mode
    if (is_command_running) {
        bool should_close = false;
        
        {
            std::lock_guard<std::mutex> lock(command_pipe_mutex);
            if (command_pipe != nullptr) {
                // Don't close the pipe here, just check if we should
                should_close = true;
            }
        }
        
        if (should_close) {
            // We need to close the pipe in a thread-safe manner
            FILE* pipe_to_close = nullptr;
            {
                std::lock_guard<std::mutex> lock(command_pipe_mutex);
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
                is_command_running = false;
                
                // Add blank line and prompt after command completes
                output.add_to_output("", OutputType::Blank);
                output.add_prompt(""); // The prompt will contain the working directory
                
                // Terminate the reader thread
                if (output_reader_thread.joinable()) {
                    output_reader_thread.request_stop();
                }
            }
        }
    }
}

void TerminalCommands::terminate_current_process(bool& is_command_running) {
    if (is_command_running) {
        // Ask the thread to stop
        if (output_reader_thread.joinable()) {
            output_reader_thread.request_stop();
            
            // Give the thread a moment to notice the stop request
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Close the pipe in a thread-safe manner
        FILE* pipe_to_close = nullptr;
        {
            std::lock_guard<std::mutex> lock(command_pipe_mutex);
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
        
        is_command_running = false;
        
        // Wait for the thread to finish
        if (output_reader_thread.joinable()) {
            output_reader_thread.join();
        }
    }
}

} // namespace rouen::cards
