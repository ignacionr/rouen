#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <format>
#include <cstring>

#include "../../helpers/debug.hpp"
#include "terminal_output.hpp"
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

class TerminalCommands {
public:
    // Execute a command with optional LLM processing
    void execute_command(const std::string& command, bool use_llm, 
                        TerminalOutput& output, const std::string& cwd,
                        std::vector<std::string>& history, size_t& history_index,
                        bool& is_command_running, bool use_interactive_bash,
                        bool& show_sudo_prompt, std::string& sudo_command);

    // Check command output and update status
    void check_command_output(bool use_interactive_bash, bool& is_command_running, 
                            TerminalOutput& output);
    
    // Terminate current running process
    void terminate_current_process(bool& is_command_running);
    
    // Execute command without using interactive bash
    void execute_external_command(const std::string& command, 
                                const std::string& cwd,
                                bool& is_command_running,
                                TerminalOutput& output);
    
private:
    // Generate a shell command using LLM
    std::string generate_shell_command(const std::string& description, TerminalOutput& output);
    
    // Command pipe for non-interactive mode
    FILE* command_pipe = nullptr;
    std::mutex command_pipe_mutex;
    std::thread output_reader_thread;
    std::atomic<bool> should_stop_thread{false};
};

} // namespace rouen::cards
