#pragma once

#include <string>
#include <array>
#include <memory>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <algorithm>
#include <format>

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include "debug.hpp"
#include "platform_utils.hpp"

// Add process-specific logging macros
#define PROCESS_ERROR(message) LOG_COMPONENT("PROCESS", LOG_LEVEL_ERROR, message)
#define PROCESS_ERROR_FMT(fmt, ...) PROCESS_ERROR(debug::format_log(fmt, __VA_ARGS__))

namespace ProcessHelper {
    struct CommandResult {
        int exit_code{0};
        std::string output;
        bool timed_out{false};
    };

    /**
     * Expand leading tilde (~) in directory paths using HOME environment variable.
     */
    inline std::string expandTilde(std::string_view path) {
        if (path.empty()) return "";
        if (path == "~") {
            const char* home = std::getenv("HOME");
            return home ? std::string(home) : std::string(path);
        }
        if (path.starts_with("~/")) {
            const char* home = std::getenv("HOME");
            return home ? (std::string(home) + std::string(path.substr(1))) : std::string(path);
        }
        return std::string(path);
    }

    /**
     * Execute a command with timeout and output truncation protection.
     */
    inline CommandResult executeCommandWithTimeout(
        const std::string& command,
        const std::string& directory = "",
        std::chrono::seconds timeout = std::chrono::seconds(180),
        size_t max_output_bytes = 65536) {

#if !defined(_WIN32)
        std::string command_to_run = std::string(R"(export PATH="$HOME/.local/bin:$HOME/.nix-profile/bin:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin:/run/current-system/sw/bin:$PATH" && )") + command;

        std::string expanded_dir;
        if (!directory.empty()) {
            expanded_dir = expandTilde(directory);
        }

        int pipefd[2];
        if (pipe(pipefd) != 0) {
            PROCESS_ERROR_FMT("Error creating pipe for command: {}", command);
            return CommandResult{-1, "Failed to create pipe", false};
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            PROCESS_ERROR_FMT("Error forking process for command: {}", command);
            return CommandResult{-1, "Failed to fork process", false};
        }

        if (pid == 0) {
            // Child process
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            if (!expanded_dir.empty()) {
                if (chdir(expanded_dir.c_str()) != 0) {
                    // Ignore or continue
                }
            }

            setpgid(0, 0);

            execl("/bin/sh", "sh", "-c", command_to_run.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }

        // Parent process
        close(pipefd[1]);

        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        CommandResult result;
        std::string raw_output;
        raw_output.reserve(4096);
        bool truncated = false;
        size_t total_bytes_read = 0;

        auto start_time = std::chrono::steady_clock::now();
        std::array<char, 2048> buf;

        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
            if (elapsed >= timeout) {
                result.timed_out = true;
                kill(-pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                kill(-pid, SIGKILL);
                int status = 0;
                waitpid(pid, &status, 0);
                result.exit_code = -1;
                break;
            }

            struct pollfd pfd;
            pfd.fd = pipefd[0];
            pfd.events = POLLIN | POLLHUP;
            pfd.revents = 0;

            int poll_res = poll(&pfd, 1, 50);
            if (poll_res > 0) {
                ssize_t bytes = read(pipefd[0], buf.data(), buf.size());
                if (bytes > 0) {
                    total_bytes_read += static_cast<size_t>(bytes);
                    if (raw_output.size() < max_output_bytes) {
                        size_t to_append = std::min(static_cast<size_t>(bytes), max_output_bytes - raw_output.size());
                        raw_output.append(buf.data(), to_append);
                    } else {
                        truncated = true;
                    }
                } else if (bytes == 0) {
                    int status = 0;
                    waitpid(pid, &status, 0);
                    if (WIFEXITED(status)) {
                        result.exit_code = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        result.exit_code = 128 + WTERMSIG(status);
                    }
                    break;
                }
            } else if (poll_res == 0) {
                int status = 0;
                pid_t wait_res = waitpid(pid, &status, WNOHANG);
                if (wait_res == pid) {
                    while (true) {
                        ssize_t bytes = read(pipefd[0], buf.data(), buf.size());
                        if (bytes > 0) {
                            total_bytes_read += static_cast<size_t>(bytes);
                            if (raw_output.size() < max_output_bytes) {
                                size_t to_append = std::min(static_cast<size_t>(bytes), max_output_bytes - raw_output.size());
                                raw_output.append(buf.data(), to_append);
                            } else {
                                truncated = true;
                            }
                        } else {
                            break;
                        }
                    }
                    if (WIFEXITED(status)) {
                        result.exit_code = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        result.exit_code = 128 + WTERMSIG(status);
                    }
                    break;
                }
            }
        }

        close(pipefd[0]);

        if (truncated) {
            raw_output += std::format("\n[... {} bytes truncated ...]\n", total_bytes_read - max_output_bytes);
        }
        result.output = std::move(raw_output);
        return result;
#else
        CommandResult result;
        std::string full_cmd = command;
        if (!directory.empty()) {
            full_cmd = "cd /d \"" + expandTilde(directory) + "\" && " + command;
        }
        result.output = executeCommand(full_cmd);
        result.exit_code = 0;
        result.timed_out = false;
        return result;
#endif
    }
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
        std::string expandedDir = expandTilde(directory);
        std::string fullCommand = "cd \"" + expandedDir + "\" && " + command;
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
