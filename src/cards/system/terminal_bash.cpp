#include "terminal_bash.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/config_service.hpp"
#include <vector>

namespace rouen::cards {

TerminalBash::TerminalBash() = default;

TerminalBash::~TerminalBash() {
    terminate_bash_session();
}

namespace {
std::string ProcessCarriageReturns(const std::string& input) {
    if (input.find('\r') == std::string::npos) {
        return input;
    }
    
    size_t last_r = input.find_last_of('\r');
    if (last_r != std::string::npos) {
        if (last_r + 1 < input.size()) {
            return input.substr(last_r + 1);
        }
        size_t prev_r = input.find_last_of('\r', last_r - 1);
        if (prev_r != std::string::npos) {
            return input.substr(prev_r + 1, last_r - (prev_r + 1));
        }
        return input.substr(0, last_r);
    }
    return input;
}

std::string StripAnsiSequences(const std::string& input) {
    std::string stripped;
    stripped.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '\x1b') {
            stripped += input[i];
            continue;
        }

        if (i + 1 >= input.size()) {
            break;
        }

        const char next = input[i + 1];
        if (next == '[') {
            i += 2;
            while (i < input.size()) {
                const auto ch = static_cast<unsigned char>(input[i]);
                if (ch >= 0x40 && ch <= 0x7e) {
                    break;
                }
                ++i;
            }
            continue;
        }

        if (next == ']') {
            i += 2;
            while (i < input.size()) {
                if (input[i] == '\a') {
                    break;
                }
                if (input[i] == '\x1b' && i + 1 < input.size() && input[i + 1] == '\\') {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        ++i;
    }

    return stripped;
}
}

void TerminalBash::initialize_bash_session(const std::string& initial_dir, TerminalOutput& output, std::atomic<bool>& is_command_running) {
    // Store the references
    output_ptr = &output;
    is_command_running_ptr = &is_command_running;
    
    // Set initial cwd
    set_cwd(initial_dir);
    
    // Terminate any existing session
    terminate_bash_session();
    
#ifdef _WIN32
    TERM_ERROR("Interactive bash sessions are not supported on Windows. Falling back to command-by-command mode.");
    use_interactive_bash = false;
    (void)initial_dir; // Suppress unused variable warning
    return;
#else
    // Set up terminal attributes (disable echo to prevent double commands)
    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    
    // Get standard attributes from stdin
    if (tcgetattr(STDIN_FILENO, &tio) != 0) {
        // Fallback default init if stdin is not a tty
        tio.c_iflag = TTYDEF_IFLAG;
        tio.c_oflag = TTYDEF_OFLAG;
        tio.c_cflag = TTYDEF_CFLAG;
        tio.c_lflag = TTYDEF_LFLAG;
    }
    tio.c_lflag &= ~static_cast<tcflag_t>(ECHO); // Turn off local echoing of input commands
    
    // Set terminal window size parameters
    struct winsize ws;
    ws.ws_row = 24;
    ws.ws_col = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    std::string bash_path = CONFIG_SERVICE()->get_bash_path();
    
    // Fork a child process with a pseudo-terminal (PTY) master-slave pair
    bash_pid = forkpty(&bash_master_fd, nullptr, &tio, &ws);
    
    if (bash_pid == -1) {
        TERM_ERROR("Failed to fork PTY for bash session");
        use_interactive_bash = false;
        return;
    } if (bash_pid == 0) {
        // Child process (executes inside the PTY slave)
        setenv("TERM", "xterm-256color", 1);
        
        // Execute bash in interactive mode
        execl(bash_path.c_str(), "bash", "-i", nullptr);
        
        perror("execl");
        exit(1);
    } else {
        // Parent process
        
        // Set master PTY fd to non-blocking mode
        int flags = fcntl(bash_master_fd, F_GETFL, 0);
        [[maybe_unused]] int fcntl_result = fcntl(bash_master_fd, F_SETFL, flags | O_NONBLOCK);
        
        // Reset stop flag before starting thread
        should_stop_threads = false;
        
        // Start single reader thread for both stdout/stderr coming from PTY master
        bash_stdout_reader_thread = std::thread([this, &output]() {
            read_bash_stream(bash_master_fd, OutputType::StdOut, output, *is_command_running_ptr);
        });
        
        // Customize bash environment - use PS1 that has the working directory (\w) followed by a newline for prompt tracking
        send_to_bash(R"(export PS1="ROUEN_PROMPT|\w|\n")", true);
        send_to_bash("set +H", true);
        
        // Ensure Nix and standard paths are loaded
        send_to_bash("export PATH=\"$HOME/.local/bin:$PATH:/opt/homebrew/bin:/usr/local/bin\"", true);
        send_to_bash("export NIXPKGS_ALLOW_UNFREE=1", true);
        send_to_bash(R"(if [ -e '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh' ]; then . '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh'; elif [ -e '/etc/profile.d/nix.sh' ]; then . '/etc/profile.d/nix.sh'; elif [ -e "$HOME/.nix-profile/etc/profile.d/nix.sh" ]; then . "$HOME/.nix-profile/etc/profile.d/nix.sh"; fi)", true);
        
        // Change to initial directory
        send_to_bash(std::format("cd \"{}\"", initial_dir), true);
        
        use_interactive_bash = true;
        TERM_INFO("Interactive PTY bash session started successfully");
    }
#endif
}

void TerminalBash::terminate_bash_session() {
#ifndef _WIN32
    if (bash_pid > 0) {
        // Signal thread to stop
        should_stop_threads = true;
        
        // Stop reader thread
        if (bash_stdout_reader_thread.joinable()) {
            bash_stdout_reader_thread.join();
        }
        
        // Close PTY master connection
        if (bash_master_fd >= 0) {
            [[maybe_unused]] auto write_result = write(bash_master_fd, "exit\n", 5);
            close(bash_master_fd);
            bash_master_fd = -1;
        }
        
        // Give bash a moment to exit cleanly
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Kill the process if it's still running
        int status{0};
        pid_t result = waitpid(bash_pid, &status, WNOHANG);
        if (result == 0) {
            kill(bash_pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

void TerminalBash::send_to_bash(const std::string& command, bool raw) const {
#ifndef _WIN32
    if (bash_master_fd >= 0) {
        std::string cmd_with_nl = command + "\n";
        [[maybe_unused]] auto write_result1 = write(bash_master_fd, cmd_with_nl.c_str(), cmd_with_nl.length());
        
        if (!raw) {
            std::string end_marker = "echo ROUEN_CMD_DONE\n";
            [[maybe_unused]] auto write_result2 = write(bash_master_fd, end_marker.c_str(), end_marker.length());
        }
    }
#else
    (void)command;
    (void)raw;
#endif
}

std::string TerminalBash::get_cwd() {
    std::lock_guard<std::mutex> lock(cwd_mutex);
    return current_working_dir;
}

void TerminalBash::set_cwd(const std::string& cwd) {
    std::lock_guard<std::mutex> lock(cwd_mutex);
    current_working_dir = cwd;
}

void TerminalBash::read_bash_stream(int pipe_fd, OutputType output_type, 
                                    TerminalOutput& output, std::atomic<bool>& is_command_running) {
#ifdef _WIN32
    (void)pipe_fd;
    (void)output_type;
    (void)output;
    (void)is_command_running;
#else
    if (pipe_fd < 0) return;
    
    char buffer[4096];
    std::string accumulated_output;
    bool command_running = false;
    
    struct pollfd pfd;
    pfd.fd = pipe_fd;
    pfd.events = POLLIN;
    
    while (!should_stop_threads.load()) {
        int poll_result = poll(&pfd, 1, 10);
        
        if (poll_result > 0 && (pfd.revents & POLLIN)) {
            ssize_t bytes_read = read(pipe_fd, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                accumulated_output += buffer;
                
                size_t pos = 0;
                size_t end_line{0};
                
                while ((end_line = accumulated_output.find('\n', pos)) != std::string::npos) {
                    std::string line = accumulated_output.substr(pos, end_line - pos);
                    pos = end_line + 1;
                    
                    if (line.find("cannot set terminal process group") != std::string::npos ||
                        line.find("no job control in shell") != std::string::npos) {
                        continue;
                    }
                    
                    // Collapse any carriage returns (like progress update bars)
                    std::string clean_line = ProcessCarriageReturns(line);
                    
                    const std::string control_stripped_line = StripAnsiSequences(clean_line);

                    if (control_stripped_line.starts_with("ROUEN_PROMPT|")) {
                        is_command_running.store(false);
                        command_running = false;
                        
                        std::string parsed_cwd;
                        size_t first_pipe = control_stripped_line.find('|');
                        size_t second_pipe = control_stripped_line.find('|', first_pipe + 1);
                        if (first_pipe != std::string::npos && second_pipe != std::string::npos) {
                            parsed_cwd = control_stripped_line.substr(first_pipe + 1, second_pipe - first_pipe - 1);
                        }
                        
                        if (!parsed_cwd.empty()) {
                            if (parsed_cwd == "~") {
                                const char* home = getenv("HOME");
                                if (home) parsed_cwd = home;
                            } else if (parsed_cwd.starts_with("~/")) {
                                const char* home = getenv("HOME");
                                if (home) parsed_cwd = std::string(home) + parsed_cwd.substr(1);
                            }
                            set_cwd(parsed_cwd);
                        }
                        
                        output.add_to_output("", OutputType::Blank);
                        output.add_prompt(get_cwd());
                        continue;
                    }
                    
                    if (output_type == OutputType::StdOut) {
                        if (control_stripped_line == "ROUEN_CMD_DONE") {
                            is_command_running.store(false);
                            command_running = false;
                            continue;
                        } if (!command_running && 
                                  (clean_line.empty() || clean_line.find("bash") != std::string::npos || 
                                   clean_line.find("TERM=") != std::string::npos)) {
                            continue;
                        }
                    }
                    
                    command_running = true;
                    output.add_to_output(clean_line, output_type);
                }
                
                accumulated_output.erase(0, pos);
                output.set_partial_line(ProcessCarriageReturns(accumulated_output), output_type);
                
            } else if (bytes_read == 0) {
                TERM_WARN("PTY master connection closed EOF");
                break;
            } else if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }                     TERM_ERROR_FMT("Error reading from PTY: {}", strerror(errno));
                    break;
               
            }
        } else if (poll_result < 0) {
            if (errno != EINTR) {
                TERM_ERROR_FMT("Poll error on PTY master: {}", strerror(errno));
                break;
            }
        }
    }
    
    TERM_INFO("PTY reader thread exiting");
#endif
}

void TerminalBash::restart_with_sudo(const char* password, const std::string& prev_cwd,
                                     const std::string& sudo_cmd, TerminalOutput& output,
                                     std::atomic<bool>& is_command_running) {
#ifndef _WIN32
    output.add_to_output("Starting sudo PTY session...", OutputType::System);
    terminate_bash_session();
    
    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    if (tcgetattr(STDIN_FILENO, &tio) != 0) {
        tio.c_iflag = TTYDEF_IFLAG;
        tio.c_oflag = TTYDEF_OFLAG;
        tio.c_cflag = TTYDEF_CFLAG;
        tio.c_lflag = TTYDEF_LFLAG;
    }
    tio.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    
    struct winsize ws;
    ws.ws_row = 24;
    ws.ws_col = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    std::string sudo_path = CONFIG_SERVICE()->get_sudo_path();
    std::string bash_path = CONFIG_SERVICE()->get_bash_path();
    
    bash_pid = forkpty(&bash_master_fd, nullptr, &tio, &ws);
    
    if (bash_pid == -1) {
        TERM_ERROR("Failed to fork PTY for sudo bash session");
        output.add_to_output("Failed to start sudo session: PTY error", OutputType::StdErr);
        initialize_bash_session(prev_cwd, output, is_command_running);
        return;
    } if (bash_pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execl(sudo_path.c_str(), "sudo", "-S", bash_path.c_str(), "--norc", "-i", nullptr);
        perror("execl");
        exit(1);
    } else {
        int flags = fcntl(bash_master_fd, F_GETFL, 0);
        [[maybe_unused]] int fcntl_result = fcntl(bash_master_fd, F_SETFL, flags | O_NONBLOCK);
        
        should_stop_threads = false;
        bash_stdout_reader_thread = std::thread([this, &output]() {
            read_bash_stream(bash_master_fd, OutputType::StdOut, output, *is_command_running_ptr);
        });
        
        std::string pass_str = std::string(password) + "\n";
        [[maybe_unused]] auto write_result = write(bash_master_fd, pass_str.c_str(), pass_str.length());
        
        send_to_bash(R"(export PS1="ROUEN_PROMPT|\w|\n")", true);
        send_to_bash("set +H", true);
        send_to_bash("export PATH=\"$HOME/.local/bin:$PATH:/opt/homebrew/bin:/usr/local/bin\"", true);
        send_to_bash("export NIXPKGS_ALLOW_UNFREE=1", true);
        send_to_bash(R"(if [ -e '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh' ]; then . '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh'; elif [ -e '/etc/profile.d/nix.sh' ]; then . '/etc/profile.d/nix.sh'; elif [ -e "$HOME/.nix-profile/etc/profile.d/nix.sh" ]; then . "$HOME/.nix-profile/etc/profile.d/nix.sh"; fi)", true);
        
        send_to_bash(std::format("cd \"{}\"", prev_cwd), true);
        
        if (!sudo_cmd.empty()) {
            output.add_to_output("", OutputType::Blank);
            output.add_to_output(sudo_cmd, OutputType::Command);
            is_command_running_ptr->store(true);
            send_to_bash(sudo_cmd);
        }
        
        use_interactive_bash = true;
        output.add_to_output("", OutputType::Blank);
        output.add_to_output("You are now in a sudo session. Be careful with privileged commands.", OutputType::System);
    }
#else
    // Windows doesn't support sudo - just show an error
    (void)password;
    (void)sudo_cmd;
    (void)is_command_running;
    
    output.add_to_output("Sudo is not supported on Windows.", OutputType::StdErr);
    
    // Add a blank line and prompt
    output.add_to_output("", OutputType::Blank);
    output.add_prompt(prev_cwd);
#endif
}

void TerminalBash::send_sigint() {
#ifndef _WIN32
    if (bash_pid > 0) {
        // Send SIGINT to the process group so foreground jobs (like 'top') receive it
        kill(-bash_pid, SIGINT);
        
        // Also send SIGINT directly to direct child processes of the bash session
        // (This acts as a robust backup if process group propagation is hindered)
        std::string cmd = std::format("pgrep -P {}", bash_pid);
        FILE* pipe = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
        if (pipe) {
            char buffer[128];
            std::vector<pid_t> child_pids;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                try {
                    pid_t pid = std::stoi(buffer);
                    if (pid > 0) {
                        child_pids.push_back(pid);
                    }
                } catch (...) {
                    (void)0;
                }
            }
            pclose(pipe);
            
            for (pid_t pid : child_pids) {
                kill(pid, SIGINT);
                TERM_INFO_FMT("Sent SIGINT directly to child process {}", pid);
            }
        }
        
        TERM_INFO("Sent SIGINT (Ctrl+C) to bash process group and child processes");
    }
#endif
}

void TerminalBash::send_sigkill() {
#ifndef _WIN32
    if (bash_pid > 0) {
        // Query child PIDs of bash_pid using pgrep -P
        std::string cmd = std::format("pgrep -P {}", bash_pid);
        FILE* pipe = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
        if (pipe) {
            char buffer[128];
            std::vector<pid_t> child_pids;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                try {
                    pid_t pid = std::stoi(buffer);
                    if (pid > 0) {
                        child_pids.push_back(pid);
                    }
                } catch (...) {
                    (void)0;
                }
            }
            pclose(pipe);
            
            for (pid_t pid : child_pids) {
                kill(pid, SIGKILL);
                TERM_INFO_FMT("Sent SIGKILL to child process {}", pid);
            }
        }
    }
#endif
}

} // namespace rouen::cards
