#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <poll.h>
#include <errno.h>

// Required Linux/POSIX headers
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#include "imgui.h"

#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/api_keys.hpp"
#include "../../helpers/cppgpt.hpp"
#include "../../helpers/fetch.hpp"

// Define terminal-specific logging macros
#define TERM_ERROR(message) LOG_COMPONENT("TERM", LOG_LEVEL_ERROR, message)
#define TERM_WARN(message) LOG_COMPONENT("TERM", LOG_LEVEL_WARN, message)
#define TERM_INFO(message) LOG_COMPONENT("TERM", LOG_LEVEL_INFO, message)
#define TERM_DEBUG(message) LOG_COMPONENT("TERM", LOG_LEVEL_DEBUG, message)
#define TERM_TRACE(message) LOG_COMPONENT("TERM", LOG_LEVEL_TRACE, message)

// Format-enabled logging macros
#define TERM_ERROR_FMT(fmt, ...) TERM_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define TERM_WARN_FMT(fmt, ...) TERM_WARN(debug::format_log(fmt, __VA_ARGS__))
#define TERM_INFO_FMT(fmt, ...) TERM_INFO(debug::format_log(fmt, __VA_ARGS__))
#define TERM_DEBUG_FMT(fmt, ...) TERM_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define TERM_TRACE_FMT(fmt, ...) TERM_TRACE(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::cards {

class terminal : public card {
public:
    terminal(std::string_view initial_dir = {}) {
        // Set up colors for the terminal card
        colors[0] = {0.15f, 0.15f, 0.2f, 1.0f};     // Primary color - dark blue
        colors[1] = {0.2f, 0.2f, 0.25f, 0.7f};      // Secondary color - slightly lighter
        
        // Additional colors for terminal elements
        get_color(2, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));   // Standard output text
        get_color(3, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));   // Success/prompt text
        get_color(4, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));   // Error text
        get_color(5, ImVec4(0.9f, 0.9f, 0.5f, 1.0f));   // Warning/special text
        get_color(6, ImVec4(0.6f, 0.6f, 0.8f, 1.0f));   // Command text
        get_color(7, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));   // Input background
        
        // Set window title and properties
        name("Terminal");
        width = 600.0f;                  // Default width
        requested_fps = 30;              // High refresh rate for smoother output
        
        // Initialize working directory
        if (initial_dir.empty()) {
            current_working_dir = std::filesystem::current_path().string();
        } else {
            current_working_dir = std::string(initial_dir);
        }
        
        // Initialize the interactive bash session
        initialize_bash_session();
        
        // Add initial welcome message
        add_to_output(std::format("Interactive Bash Terminal initialized in {}", current_working_dir), OutputType::System);
        add_to_output("Type commands and press Enter to execute. Use Up/Down arrows for history.", OutputType::System);
        add_to_output("Press Ctrl+Enter to use Grok AI to convert natural language to Bash commands.", OutputType::System);
        add_to_output("", OutputType::Blank);
        
        // Add prompt
        add_prompt();
    }
    
    ~terminal() override {
        // Stop all running processes and terminate the bash session
        terminate_bash_session();
    }
    
    // Execute a command to explicitly test stderr output
    void test_stderr_output() {
        const std::string test_cmd = "bash -c 'echo This is stdout; echo This is stderr >&2; ls /nonexistent-directory; invalid_command'";
        add_to_output("Running stderr test command...", OutputType::System);
        execute_command(test_cmd);
    }
    
    bool render() override {
        return render_window([this]() {
            // Calculate window dimensions
            const float window_width = ImGui::GetContentRegionAvail().x;
            const float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
            
            // Test for stderr - press F5 for a quick test
            if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
                test_stderr_output();
            }
            
            // Render the output area (taking most of the space)
            if (ImGui::BeginChild("OutputScrollRegion", ImVec2(window_width, -footer_height), true)) {
                // Process any output from running commands
                check_command_output();
                
                // Display output buffer
                std::lock_guard<std::mutex> lock(output_mutex);
                for (const auto& [text, type] : output_buffer) {
                    switch (type) {
                        case OutputType::Command:
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[6]);
                            break;
                        case OutputType::StdOut:
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
                            break;
                        case OutputType::StdErr:
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[4]);
                            break;
                        case OutputType::System:
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[5]);
                            break;
                        case OutputType::Prompt:
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[3]);
                            break;
                        case OutputType::Blank:
                            ImGui::Spacing();
                            continue;
                        default:
                            ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
                    }
                    
                    ImGui::TextWrapped("%s", text.c_str());
                    ImGui::PopStyleColor();
                }
                
                // Auto-scroll to the bottom if needed
                if (should_auto_scroll || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
                    ImGui::SetScrollHereY(1.0f);
                    should_auto_scroll = false;
                }
            }
            ImGui::EndChild();
            
            // Separator
            ImGui::Separator();
            
            // Check if we need to show the sudo password prompt
            if (show_sudo_prompt) {
                render_sudo_prompt(window_width);
            } else {
                // Regular command input area (at the bottom)
                render_command_input(window_width);
            }
            
            // Status indicator for running processes
            if (is_command_running) {
                ImGui::SameLine();
                ImGui::TextColored(colors[3], "%c", spinner_chars[(spinner_counter/5) % 4]);
                spinner_counter++;
            }
        });
    }
    
    // Render the sudo password prompt
    void render_sudo_prompt(float window_width) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, colors[7]);
        
        // Focus the password input on first display
        static bool first_display = true;
        if (first_display) {
            ImGui::SetKeyboardFocusHere();
            first_display = false;
        }
        
        // Password input (displayed as asterisks)
        static char password_buffer[128] = "";
        ImGui::SetNextItemWidth(window_width - 70);
        bool enter_pressed = ImGui::InputText("##SudoPassword", password_buffer, IM_ARRAYSIZE(password_buffer), 
                                            ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                                            nullptr, nullptr);
        
        ImGui::SameLine();
        
        // Submit button
        if (ImGui::Button("Submit", ImVec2(60, 0)) || enter_pressed) {
            if (password_buffer[0] != '\0') {
                // Attempt to restart the bash session with sudo
                restart_with_sudo(password_buffer);
                
                // Clear password for security
                memset(password_buffer, 0, sizeof(password_buffer));
                
                // Hide the prompt and reset for next time
                show_sudo_prompt = false;
                first_display = true;
            }
        }
        
        ImGui::PopStyleColor();
    }
    
    // Render the regular command input
    void render_command_input(float window_width) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, colors[7]);
        
        // Focus the input box when requested
        if (focus_input) {
            ImGui::SetKeyboardFocusHere();
            focus_input = false;
        }
        
        // Process keyboard shortcuts for history navigation
        bool enter_pressed = false;
        
        // Input field - Using standard char array
        static char input_buffer[1024] = "";
        
        // Copy current input_text to input_buffer if not empty
        if (!input_text.empty() && input_buffer[0] == '\0') {
            strncpy(input_buffer, input_text.c_str(), sizeof(input_buffer) - 1);
            input_buffer[sizeof(input_buffer) - 1] = '\0';
            input_text.clear();
        }
        
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            if (navigate_history(true, input_buffer, sizeof(input_buffer))) {
                // History navigation updated the input buffer
                focus_input = true;
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            if (navigate_history(false, input_buffer, sizeof(input_buffer))) {
                // History navigation updated the input buffer
                focus_input = true;
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            enter_pressed = true;
        }
        
        ImGui::SetNextItemWidth(window_width);
        if (ImGui::InputText("##CommandInput", input_buffer, IM_ARRAYSIZE(input_buffer), 
                          ImGuiInputTextFlags_EnterReturnsTrue,
                          nullptr, nullptr)) {
            enter_pressed = true;
        }
        
        // Process the command if enter was pressed
        if (enter_pressed && input_buffer[0] != '\0') {
            execute_command(input_buffer, ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl));
            input_buffer[0] = '\0';  // Clear the input
            focus_input = true;
        }
        
        ImGui::PopStyleColor();
    }
    
    std::string get_uri() const override {
        if (current_working_dir.empty() || current_working_dir == std::filesystem::current_path().string()) {
            return "terminal";
        }
        return std::format("terminal:{}", current_working_dir);
    }
    
private:
    enum class OutputType {
        StdOut,     // Standard output (white)
        StdErr,     // Error output (red)
        Command,    // Commands entered by user (blue)
        System,     // System messages (yellow)
        Prompt,     // Command prompt (green)
        Blank       // Empty line
    };
    
    void add_to_output(const std::string& text, OutputType type) {
        std::lock_guard<std::mutex> lock(output_mutex);
        output_buffer.emplace_back(text, type);
        
        // Keep buffer from growing too large (max 500 lines)
        if (output_buffer.size() > 500) {
            output_buffer.pop_front();
        }
        
        should_auto_scroll = true;
    }
    
    // Add multiple output lines with a single lock
    void add_multiple_outputs(const std::vector<std::pair<std::string, OutputType>>& entries) {
        std::lock_guard<std::mutex> lock(output_mutex);
        for (const auto& [text, type] : entries) {
            output_buffer.emplace_back(text, type);
        }
        
        // Keep buffer from growing too large (max 500 lines)
        while (output_buffer.size() > 500) {
            output_buffer.pop_front();
        }
        
        should_auto_scroll = true;
    }
    
    // Clear the terminal and add new content
    void clear_terminal() {
        std::lock_guard<std::mutex> lock(output_mutex);
        output_buffer.clear();
        output_buffer.emplace_back("Terminal cleared.", OutputType::System);
        output_buffer.emplace_back("", OutputType::Blank);
        
        // Get current working directory from bash
        update_cwd_from_bash();
        
        // Add prompt with updated working directory
        output_buffer.emplace_back(std::format("{}$ ", current_working_dir), OutputType::Prompt);
        should_auto_scroll = true;
    }
    
    void add_prompt() {
        // Add the directory prompt (like "user@host:~/dir$")
        std::string prompt = std::format("{}$ ", current_working_dir);
        add_to_output(prompt, OutputType::Prompt);
    }
    
    // Initialize interactive bash session
    void initialize_bash_session() {
        // Terminate any existing session
        terminate_bash_session();
        
        // Create pipes for communication with bash
        int stdin_pipe[2];    // For writing to bash's stdin
        int stdout_pipe[2];   // For reading from bash's stdout
        int stderr_pipe[2];   // For reading from bash's stderr
        
#ifdef _WIN32
        TERM_ERROR("Interactive bash sessions are not supported on Windows. Falling back to command-by-command mode.");
        use_interactive_bash = false;
        return;
#else
        // Create pipes
        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            TERM_ERROR("Failed to create pipes for bash session");
            use_interactive_bash = false;
            return;
        }
        
        // Fork a child process for bash
        bash_pid = fork();
        
        if (bash_pid == -1) {
            // Fork failed
            TERM_ERROR("Failed to fork process for bash session");
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            use_interactive_bash = false;
            return;
        } else if (bash_pid == 0) {
            // Child process (bash)
            
            // Redirect stdin/stdout/stderr
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            
            // Close unused pipe ends
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            
            // Execute bash with interactive but non-login options
            execl("/bin/bash", "bash", "--noediting", "--noprofile", "--norc", "+m", 
                  "-c", "exec bash --norc +m", NULL);
            
            // If execl returns, there was an error
            perror("execl");
            exit(1);
        } else {
            // Parent process
            
            // Close unused pipe ends
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            
            // Store pipe file descriptors
            bash_stdin_fd = stdin_pipe[1];
            bash_stdout_fd = stdout_pipe[0];
            bash_stderr_fd = stderr_pipe[0];
            
            // Set pipes to non-blocking mode
            int flags = fcntl(bash_stdin_fd, F_GETFL, 0);
            fcntl(bash_stdin_fd, F_SETFL, flags | O_NONBLOCK);
            
            flags = fcntl(bash_stdout_fd, F_GETFL, 0);
            fcntl(bash_stdout_fd, F_SETFL, flags | O_NONBLOCK);
            
            flags = fcntl(bash_stderr_fd, F_GETFL, 0);
            fcntl(bash_stderr_fd, F_SETFL, flags | O_NONBLOCK);
            
            // Start reader threads for bash output
            bash_stdout_reader_thread = std::jthread([this](std::stop_token stoken) {
                read_bash_stream(stoken, bash_stdout_fd, OutputType::StdOut);
            });
            
            bash_stderr_reader_thread = std::jthread([this](std::stop_token stoken) {
                read_bash_stream(stoken, bash_stderr_fd, OutputType::StdErr);
            });
            
            // Customize bash environment - use PS1 that doesn't have job control messages
            send_to_bash("export PS1=\"ROUEN_PROMPT|\"");
            send_to_bash("export TERM=dumb");
            
            // Disable history expansion to avoid problems with '!' character
            send_to_bash("set +H");
            
            // Change to initial directory
            send_to_bash(std::format("cd \"{}\"", current_working_dir));
            
            use_interactive_bash = true;
            TERM_INFO("Interactive bash session started successfully");
        }
#endif
    }
    
    // Terminate bash session
    void terminate_bash_session() {
#ifndef _WIN32
        if (bash_pid > 0) {
            // Stop reader threads
            if (bash_stdout_reader_thread.joinable()) {
                bash_stdout_reader_thread.request_stop();
                bash_stdout_reader_thread.join();
            }
            
            if (bash_stderr_reader_thread.joinable()) {
                bash_stderr_reader_thread.request_stop();
                bash_stderr_reader_thread.join();
            }
            
            // Send exit command to bash
            if (bash_stdin_fd >= 0) {
                // Send the exit command to bash
                write(bash_stdin_fd, "exit\n", 5);
                close(bash_stdin_fd);
                bash_stdin_fd = -1;
            }
            
            // Close stdout pipe
            if (bash_stdout_fd >= 0) {
                close(bash_stdout_fd);
                bash_stdout_fd = -1;
            }
            
            // Close stderr pipe
            if (bash_stderr_fd >= 0) {
                close(bash_stderr_fd);
                bash_stderr_fd = -1;
            }
            
            // Give bash a moment to exit cleanly
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Kill the process if it's still running
            int status;
            pid_t result = waitpid(bash_pid, &status, WNOHANG);
            if (result == 0) {
                // Process is still running, kill it
                kill(bash_pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                // Force kill if still running
                result = waitpid(bash_pid, &status, WNOHANG);
                if (result == 0) {
                    kill(bash_pid, SIGKILL);
                }
            }
            
            bash_pid = -1;
            TERM_INFO("Bash session terminated");
        }
#endif
    }
    
    // Send command to interactive bash session
    void send_to_bash(const std::string& command) {
#ifndef _WIN32
        if (bash_stdin_fd >= 0) {
            // Simply append a newline to the command and send it directly
            std::string cmd_with_nl = command + "\n";
            
            // Write command to bash's stdin
            write(bash_stdin_fd, cmd_with_nl.c_str(), cmd_with_nl.length());
            
            // Send a separate echo command to mark the end of output
            // Use a unique string that's unlikely to appear in normal output
            std::string end_marker = "echo ROUEN_CMD_DONE\n";
            write(bash_stdin_fd, end_marker.c_str(), end_marker.length());
        }
#endif
    }
    
    // Reader thread for bash output stream (stdout or stderr)
    void read_bash_stream(std::stop_token stoken, int pipe_fd, OutputType output_type) {
#ifndef _WIN32
        if (pipe_fd < 0) return;
        
        char buffer[4096];
        std::string accumulated_output;
        bool command_running = false;
        
        // Set up poll structure to check for data
        struct pollfd pfd;
        pfd.fd = pipe_fd;
        pfd.events = POLLIN;
        
        while (!stoken.stop_requested()) {
            // Poll with a short timeout
            int poll_result = poll(&pfd, 1, 10); // 10ms timeout
            
            if (poll_result > 0 && (pfd.revents & POLLIN)) {
                // Data is available to read
                ssize_t bytes_read = read(pipe_fd, buffer, sizeof(buffer) - 1);
                
                if (bytes_read > 0) {
                    // Null-terminate the buffer
                    buffer[bytes_read] = '\0';
                    
                    // Add to accumulated output
                    accumulated_output += buffer;
                    
                    // Process accumulated output line by line
                    size_t pos = 0;
                    size_t end_line;
                    
                    while ((end_line = accumulated_output.find('\n', pos)) != std::string::npos) {
                        // Extract line
                        std::string line = accumulated_output.substr(pos, end_line - pos);
                        pos = end_line + 1;
                        
                        // Filter out job control warning messages
                        if (line.find("cannot set terminal process group") != std::string::npos ||
                            line.find("no job control in shell") != std::string::npos) {
                            // Skip these bash startup warning messages
                            continue;
                        }
                        
                        // Special handling for stdout stream
                        if (output_type == OutputType::StdOut) {
                            // Check for special markers
                            if (line.starts_with("ROUEN_PROMPT|")) {
                                // Bash prompt - indicates command has finished
                                is_command_running = false;
                                command_running = false;
                                
                                // Update current working directory
                                update_cwd_from_bash();
                                
                                // Add prompt to output
                                add_to_output("", OutputType::Blank);
                                add_prompt();
                                continue;
                            } else if (line == "ROUEN_CMD_DONE") {
                                // End marker for command output
                                is_command_running = false;
                                command_running = false;
                                continue;
                            } else if (!command_running && 
                                      (line.empty() || line.find("bash") != std::string::npos || 
                                       line.find("TERM=") != std::string::npos)) {
                                // Ignore initial bash startup messages
                                continue;
                            }
                        }
                        
                        // Regular output line - stdout or stderr
                        command_running = true;
                        add_to_output(line, output_type);
                    }
                    
                    // Keep any remaining partial line
                    accumulated_output.erase(0, pos);
                    
                } else if (bytes_read == 0) {
                    // EOF - bash has closed the pipe
                    TERM_WARN_FMT("Bash {} stream closed unexpectedly", output_type == OutputType::StdOut ? "stdout" : "stderr");
                    break;
                } else if (bytes_read < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // No data available, continue to next poll
                        continue;
                    } else {
                        // Error
                        TERM_ERROR_FMT("Error reading from bash {}: {}", 
                            output_type == OutputType::StdOut ? "stdout" : "stderr", 
                            strerror(errno));
                        break;
                    }
                }
            } else if (poll_result < 0) {
                // Poll error
                if (errno != EINTR) {
                    TERM_ERROR_FMT("Poll error on bash {}: {}", 
                        output_type == OutputType::StdOut ? "stdout" : "stderr", 
                        strerror(errno));
                    break;
                }
            }
            // Poll timeout or no data - just continue
        }
        
        TERM_INFO_FMT("Bash {} reader thread exiting", output_type == OutputType::StdOut ? "stdout" : "stderr");
#endif
    }
    
    // Get current working directory from bash
    void update_cwd_from_bash() {
#ifndef _WIN32
        if (!use_interactive_bash || bash_stdin_fd < 0) return;
        
        // Create a temporary file for bash to write the pwd to
        char temp_filename[] = "/tmp/rouen_pwd_XXXXXX";
        int temp_fd = mkstemp(temp_filename);
        
        if (temp_fd != -1) {
            close(temp_fd);
            
            // Send command to write pwd to the temporary file
            send_to_bash(std::format("pwd > \"{}\"", temp_filename));
            
            // Wait for the command to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Read the file
            std::ifstream pwd_file(temp_filename);
            if (pwd_file) {
                std::string pwd;
                std::getline(pwd_file, pwd);
                
                if (!pwd.empty()) {
                    current_working_dir = pwd;
                }
            }
            
            // Remove the temporary file
            std::remove(temp_filename);
        }
#endif
    }
    
    // Execute command with sudo if needed
    void execute_command(const std::string& command, bool use_llm = false) {
        // If use_llm is true, generate a shell command using Grok
        std::string cmd_to_execute = command;
        
        if (use_llm) {
            std::string generated_cmd = generate_shell_command(command);
            if (!generated_cmd.empty()) {
                // Show what command was generated
                add_to_output(std::format("Grok generated command: {}", generated_cmd), OutputType::System);
                cmd_to_execute = generated_cmd;
            } else {
                // If command generation failed, fallback to original command
                add_to_output("Failed to generate command with Grok. Using original command.", OutputType::StdErr);
            }
        }
        
        // Check if command needs sudo privileges
        if (cmd_to_execute.starts_with("sudo ")) {
            // Remember the command without sudo for later execution
            sudo_command = cmd_to_execute.substr(5);
            
            // Inform the user about the sudo prompt
            add_to_output("This command requires sudo privileges. Please enter your password:", OutputType::System);
            
            // Show the sudo password prompt
            show_sudo_prompt = true;
            return;
        }
        
        // Add command to output buffer and history
        add_to_output(cmd_to_execute, OutputType::Command);
        
        // Add to command history
        command_history.push_back(cmd_to_execute);
        if (command_history.size() > 50) {  // Limit history size
            command_history.erase(command_history.begin());
        }
        history_index = command_history.size();
        
        // Handle special built-in commands first
        if (cmd_to_execute == "clear" || cmd_to_execute == "cls") {
            clear_terminal();
            return;
        }
        
        // For interactive bash mode
        if (use_interactive_bash) {
            // Set command as running
            is_command_running = true;
            
            // Send command to bash
            send_to_bash(cmd_to_execute);
        } else {
            // For non-interactive mode or if interactive mode is not available
            // Use the traditional command execution
            execute_external_command(cmd_to_execute);
        }
    }
    
    // Use Grok to generate a shell command from a natural language description
    std::string generate_shell_command(const std::string& description) {
        // Get Grok API key using our centralized API key manager
        std::string api_key = rouen::helpers::ApiKeys::get_grok_api_key();
        if (api_key.empty()) {
            add_to_output("Error: GROK_API_KEY environment variable is not set.", OutputType::StdErr);
            return "";
        }
        
        try {
            // Add a loader to indicate processing
            add_to_output("Generating shell command with Grok AI...", OutputType::System);
            
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
            add_to_output(std::format("Error generating command: {}", e.what()), OutputType::StdErr);
            return "";
        }
    }
    
    void execute_external_command(const std::string& command) {
        // First terminate any running process
        terminate_current_process();
        
        // Set command as running
        is_command_running = true;
        
        // Create full command with the working directory
        // Append 2>&1 to redirect stderr to stdout so we capture both
        std::string full_command = std::format("cd \"{}\" && {} 2>&1", current_working_dir, command);
        
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
                add_to_output("Failed to execute command.", OutputType::StdErr);
                is_command_running = false;
                add_to_output("", OutputType::Blank);
                add_prompt();
                return;
            }
        }
        
        // Start background thread to read the output
        output_reader_thread = std::jthread([this](std::stop_token stoken) {
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
                    
                    add_to_output(buffer, OutputType::StdOut);
                } else {
                    // End of output or error
                    break;
                }
            }
        });
    }
    
    void check_command_output() {
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
                    add_to_output("", OutputType::Blank);
                    add_prompt();
                    
                    // Terminate the reader thread
                    if (output_reader_thread.joinable()) {
                        output_reader_thread.request_stop();
                    }
                }
            }
        }
    }
    
    void terminate_current_process() {
        if (is_command_running && !use_interactive_bash) {
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
    
    bool navigate_history(bool go_back, char* buffer, size_t buffer_size) {
        if (command_history.empty()) return false;
        
        if (go_back) {  // Up arrow - go back in history
            if (history_index > 0) {
                history_index--;
                strncpy(buffer, command_history[history_index].c_str(), buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                return true;
            }
        } else {        // Down arrow - go forward in history
            if (history_index < command_history.size() - 1) {
                history_index++;
                strncpy(buffer, command_history[history_index].c_str(), buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                return true;
            } else if (history_index == command_history.size() - 1) {
                // At the newest command, clear the input
                history_index = command_history.size();
                buffer[0] = '\0';
                return true;
            }
        }
        return false;
    }
    
    // Member variables
    std::string input_text;  // Stores the current input text (for history navigation)
    std::vector<std::string> command_history;
    size_t history_index = 0;
    bool focus_input = true;
    bool should_auto_scroll = false;
    
    // Sudo functionality
    bool show_sudo_prompt = false;
    std::string sudo_command;  // Stores the command to run after sudo authentication
    
    using OutputEntry = std::pair<std::string, OutputType>;
    std::deque<OutputEntry> output_buffer;
    std::mutex output_mutex;
    std::mutex command_pipe_mutex; // Mutex for synchronizing access to command_pipe
    
    std::string current_working_dir;
    FILE* command_pipe = nullptr;
    std::jthread output_reader_thread;
    bool is_command_running = false;
    
    // Interactive bash session variables
    bool use_interactive_bash = false;
#ifndef _WIN32
    pid_t bash_pid = -1;
    int bash_stdin_fd = -1;
    int bash_stdout_fd = -1;
    int bash_stderr_fd = -1;
    std::jthread bash_stdout_reader_thread;
    std::jthread bash_stderr_reader_thread;
#endif
    
    // Animation for running process
    int spinner_counter = 0;
    const char spinner_chars[4] = {'|', '/', '-', '\\'};
    
    // Restart bash session with sudo privileges
    void restart_with_sudo(const char* password) {
#ifndef _WIN32
        // Prepare a message to inform the user that we're starting a sudo session
        add_to_output("Starting sudo session...", OutputType::System);
        
        // Remember the current working directory
        std::string previous_cwd = current_working_dir;
        
        // Terminate current bash session
        terminate_bash_session();
        
        // Create pipes for the new bash session
        int stdin_pipe[2];    // For writing to bash's stdin
        int stdout_pipe[2];   // For reading from bash's stdout
        int stderr_pipe[2];   // For reading from bash's stderr
        
        // Create pipes
        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            TERM_ERROR("Failed to create pipes for sudo bash session");
            add_to_output("Failed to start sudo session: pipe creation error", OutputType::StdErr);
            
            // Restart a regular bash session
            initialize_bash_session();
            add_to_output("", OutputType::Blank);
            add_prompt();
            return;
        }
        
        // Fork a child process for sudo
        bash_pid = fork();
        
        if (bash_pid == -1) {
            // Fork failed
            TERM_ERROR("Failed to fork process for sudo bash session");
            add_to_output("Failed to start sudo session: fork error", OutputType::StdErr);
            
            // Close pipes
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            
            // Restart a regular bash session
            initialize_bash_session();
            add_to_output("", OutputType::Blank);
            add_prompt();
            return;
        } else if (bash_pid == 0) {
            // Child process
            
            // Redirect stdin/stdout/stderr
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            
            // Close unused pipe ends
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            
            // Execute sudo to start a privileged bash session
            execl("/usr/bin/sudo", "sudo", "-S", "bash", "--noediting", "--noprofile", "--norc", "+m", NULL);
            
            // If execl returns, there was an error
            perror("execl");
            exit(1);
        } else {
            // Parent process
            
            // Close unused pipe ends
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            
            // Store pipe file descriptors
            bash_stdin_fd = stdin_pipe[1];
            bash_stdout_fd = stdout_pipe[0];
            bash_stderr_fd = stderr_pipe[0];
            
            // Set pipes to non-blocking mode
            int flags = fcntl(bash_stdin_fd, F_GETFL, 0);
            fcntl(bash_stdin_fd, F_SETFL, flags | O_NONBLOCK);
            
            flags = fcntl(bash_stdout_fd, F_GETFL, 0);
            fcntl(bash_stdout_fd, F_SETFL, flags | O_NONBLOCK);
            
            flags = fcntl(bash_stderr_fd, F_GETFL, 0);
            fcntl(bash_stderr_fd, F_SETFL, flags | O_NONBLOCK);
            
            // Start reader threads for bash output
            bash_stdout_reader_thread = std::jthread([this](std::stop_token stoken) {
                read_bash_stream(stoken, bash_stdout_fd, OutputType::StdOut);
            });
            
            bash_stderr_reader_thread = std::jthread([this](std::stop_token stoken) {
                read_bash_stream(stoken, bash_stderr_fd, OutputType::StdErr);
            });
            
            // Send the password to sudo (the -S flag makes sudo read the password from stdin)
            std::string pwd_with_nl = std::string(password) + "\n";
            write(bash_stdin_fd, pwd_with_nl.c_str(), pwd_with_nl.length());
            
            // Wait a moment for sudo to process the password
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Set up the environment for the sudo session
            send_to_bash("export PS1=\"ROUEN_PROMPT|\"");
            send_to_bash("export TERM=dumb");
            send_to_bash("set +H");
            
            // Change to the previous working directory
            send_to_bash(std::format("cd \"{}\"", previous_cwd));
            
            // Check if we have a command to run
            if (!sudo_command.empty()) {
                // Execute the sudo command
                add_to_output("", OutputType::Blank);
                add_to_output(sudo_command, OutputType::Command);
                
                // Add to command history (without 'sudo')
                command_history.push_back(sudo_command);
                if (command_history.size() > 50) {  // Limit history size
                    command_history.erase(command_history.begin());
                }
                history_index = command_history.size();
                
                // Execute the command
                is_command_running = true;
                send_to_bash(sudo_command);
                
                // Clear the stored sudo command
                sudo_command.clear();
            }
            
            use_interactive_bash = true;
            TERM_INFO("Sudo bash session started successfully");
            
            // Add a note about being in a sudo session
            add_to_output("", OutputType::Blank);
            add_to_output("You are now in a sudo session. Be careful with privileged commands.", OutputType::System);
        }
#else
        // Windows doesn't support sudo - just show an error
        add_to_output("Sudo is not supported on Windows.", OutputType::StdErr);
        
        // Clear the stored sudo command
        sudo_command.clear();
        
        // Add a blank line and prompt
        add_to_output("", OutputType::Blank);
        add_prompt();
#endif
    }
};

} // namespace rouen::cards