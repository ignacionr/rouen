#include "terminal_bash.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards {

TerminalBash::TerminalBash() = default;

TerminalBash::~TerminalBash() {
    terminate_bash_session();
}

void TerminalBash::initialize_bash_session(const std::string& initial_dir, TerminalOutput& output, bool& is_command_running) {
    // Store the references
    output_ptr = &output;
    is_command_running_ptr = &is_command_running;
    
    // Terminate any existing session
    terminate_bash_session();
    
#ifdef _WIN32
    TERM_ERROR("Interactive bash sessions are not supported on Windows. Falling back to command-by-command mode.");
    use_interactive_bash = false;
    return;
#else
    // Create pipes for communication with bash
    int stdin_pipe[2];    // For writing to bash's stdin
    int stdout_pipe[2];   // For reading from bash's stdout
    int stderr_pipe[2];   // For reading from bash's stderr
    
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
        
        // Reset stop flag before starting threads
        should_stop_threads = false;
        
        // Start reader threads for bash output
        bash_stdout_reader_thread = std::thread([this, &output]() {
            read_bash_stream(bash_stdout_fd, OutputType::StdOut, output, *is_command_running_ptr);
        });
        
        bash_stderr_reader_thread = std::thread([this, &output]() {
            read_bash_stream(bash_stderr_fd, OutputType::StdErr, output, *is_command_running_ptr);
        });
        
        // Customize bash environment - use PS1 that doesn't have job control messages
        send_to_bash("export PS1=\"ROUEN_PROMPT|\"");
        send_to_bash("export TERM=dumb");
        
        // Disable history expansion to avoid problems with '!' character
        send_to_bash("set +H");
        
        // Change to initial directory
        send_to_bash(std::format("cd \"{}\"", initial_dir));
        
        use_interactive_bash = true;
        TERM_INFO("Interactive bash session started successfully");
    }
#endif
}

void TerminalBash::terminate_bash_session() {
#ifndef _WIN32
    if (bash_pid > 0) {
        // Signal threads to stop
        should_stop_threads = true;
        
        // Stop reader threads
        if (bash_stdout_reader_thread.joinable()) {
            bash_stdout_reader_thread.join();
        }
        
        if (bash_stderr_reader_thread.joinable()) {
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

void TerminalBash::send_to_bash(const std::string& command) {
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

std::string TerminalBash::update_cwd_from_bash() {
    std::string current_working_dir;
    
#ifndef _WIN32
    if (!use_interactive_bash || bash_stdin_fd < 0) return current_working_dir;
    
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

    return current_working_dir;
}

void TerminalBash::read_bash_stream(int pipe_fd, OutputType output_type, 
                                    TerminalOutput& output, bool& is_command_running) {
#ifndef _WIN32
    if (pipe_fd < 0) return;
    
    char buffer[4096];
    std::string accumulated_output;
    bool command_running = false;
    
    // Set up poll structure to check for data
    struct pollfd pfd;
    pfd.fd = pipe_fd;
    pfd.events = POLLIN;
    
    while (!should_stop_threads.load()) {
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
                            
                            // Update current working directory and get the result
                            std::string new_cwd = update_cwd_from_bash();
                            
                            // Add prompt to output
                            output.add_to_output("", OutputType::Blank);
                            output.add_prompt(new_cwd);
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
                    output.add_to_output(line, output_type);
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

void TerminalBash::restart_with_sudo(const char* password, const std::string& prev_cwd,
                                     const std::string& sudo_cmd, TerminalOutput& output,
                                     bool& is_command_running) {
#ifndef _WIN32
    // Prepare a message to inform the user that we're starting a sudo session
    output.add_to_output("Starting sudo session...", OutputType::System);
    
    // Terminate current bash session
    terminate_bash_session();
    
    // Create pipes for the new bash session
    int stdin_pipe[2];    // For writing to bash's stdin
    int stdout_pipe[2];   // For reading from bash's stdout
    int stderr_pipe[2];   // For reading from bash's stderr
    
    // Create pipes
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        TERM_ERROR("Failed to create pipes for sudo bash session");
        output.add_to_output("Failed to start sudo session: pipe creation error", OutputType::StdErr);
        
        // Restart a regular bash session
        initialize_bash_session(prev_cwd, output, is_command_running);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(prev_cwd);
        return;
    }
    
    // Fork a child process for sudo
    bash_pid = fork();
    
    if (bash_pid == -1) {
        // Fork failed
        TERM_ERROR("Failed to fork process for sudo bash session");
        output.add_to_output("Failed to start sudo session: fork error", OutputType::StdErr);
        
        // Close pipes
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        
        // Restart a regular bash session
        initialize_bash_session(prev_cwd, output, is_command_running);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(prev_cwd);
        return;
    } else if (bash_pid == 0) {
        // Child process (sudo bash)
        
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
        
        // Execute sudo bash with -S to read password from stdin
        execl("/usr/bin/sudo", "sudo", "-S", "bash", "--norc", "+m", NULL);
        
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
        
        // Write password to sudo's stdin
        std::string pass_str = std::string(password) + "\n";
        write(bash_stdin_fd, pass_str.c_str(), pass_str.length());
        
        // Reset stop flag before starting threads
        should_stop_threads = false;
        
        // Start reader threads for bash output
        bash_stdout_reader_thread = std::thread([this, &output]() {
            read_bash_stream(bash_stdout_fd, OutputType::StdOut, output, *is_command_running_ptr);
        });
        
        bash_stderr_reader_thread = std::thread([this, &output]() {
            read_bash_stream(bash_stderr_fd, OutputType::StdErr, output, *is_command_running_ptr);
        });
        
        // Set up the environment for the sudo session
        send_to_bash("export PS1=\"ROUEN_PROMPT|\"");
        send_to_bash("export TERM=dumb");
        send_to_bash("set +H");
        
        // Change to the previous working directory
        send_to_bash(std::format("cd \"{}\"", prev_cwd));
        
        // Check if we have a command to run
        if (!sudo_cmd.empty()) {
            // Execute the sudo command
            output.add_to_output("", OutputType::Blank);
            output.add_to_output(sudo_cmd, OutputType::Command);
            
            // Execute the command
            *is_command_running_ptr = true;
            send_to_bash(sudo_cmd);
        }
        
        use_interactive_bash = true;
        TERM_INFO("Sudo bash session started successfully");
        
        // Add a note about being in a sudo session
        output.add_to_output("", OutputType::Blank);
        output.add_to_output("You are now in a sudo session. Be careful with privileged commands.", OutputType::System);
    }
#else
    // Windows doesn't support sudo - just show an error
    output.add_to_output("Sudo is not supported on Windows.", OutputType::StdErr);
    
    // Add a blank line and prompt
    output.add_to_output("", OutputType::Blank);
    output.add_prompt(prev_cwd);
#endif
}

} // namespace rouen::cards
