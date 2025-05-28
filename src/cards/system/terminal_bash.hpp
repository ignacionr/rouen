#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <format>
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#endif

#include "../../helpers/debug.hpp"
#include "terminal_output.hpp"

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

class TerminalBash {
public:
    TerminalBash();
    ~TerminalBash();
    
    // Initialize and terminate bash sessions
    void initialize_bash_session(const std::string& initial_dir, TerminalOutput& output, bool& is_command_running);
    void terminate_bash_session();
    
    // Send commands to bash
    void send_to_bash(const std::string& command);
    
    // Get current working directory from bash
    std::string update_cwd_from_bash();
    
    // Restart with sudo privileges
    void restart_with_sudo(const char* password, const std::string& prev_cwd, 
                         const std::string& sudo_command, TerminalOutput& output,
                         bool& is_command_running);
                         
    // Check if interactive bash is being used
    bool is_interactive() const { return use_interactive_bash; }

private:
    // Reader thread for bash output stream (stdout or stderr)
    void read_bash_stream(int pipe_fd, OutputType output_type, 
                         TerminalOutput& output, bool& is_command_running);
                         
    bool use_interactive_bash = false;
    
#ifndef _WIN32
    pid_t bash_pid = -1;
    int bash_stdin_fd = -1;
    int bash_stdout_fd = -1;
    int bash_stderr_fd = -1;
    std::thread bash_stdout_reader_thread;
    std::thread bash_stderr_reader_thread;
    std::atomic<bool> should_stop_threads{false};
#endif

    // Output reference (set during initialization)
    TerminalOutput* output_ptr = nullptr;
    bool* is_command_running_ptr = nullptr;
};

} // namespace rouen::cards
