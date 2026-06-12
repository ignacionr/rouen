#pragma once

#include <array>
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "terminal_output.hpp"
#include "terminal_bash.hpp"
#include "terminal_commands.hpp"

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
    terminal(std::string_view initial_dir = {});
    ~terminal() override;
    
    bool render() override;
    std::string get_uri() const override;
    
private:
    void render_sudo_prompt(float window_width);
    void render_command_input(float window_width);
    void test_stderr_output();
    bool navigate_history(bool go_back, char* buffer, size_t buffer_size);
    bool handle_slash_command(const std::string& cmd);
    
    std::string input_text;  // Stores the current input text (for history navigation)
    std::vector<std::string> command_history;
    size_t history_index = 0;
    bool focus_input = true;
    bool should_auto_scroll = false;
    
    // Sudo functionality
    bool show_sudo_prompt = false;
    std::string sudo_command;  // Stores the command to run after sudo authentication
    
    // Terminal components
    TerminalOutput output;
    TerminalBash bash;
    TerminalCommands commands;
    
    std::string current_working_dir;
    bool is_command_running = false;
    
    // Animation for running process
    int spinner_counter = 0;
    const char spinner_chars[4] = {'|', '/', '-', '\\'};
};

} // namespace rouen::cards
