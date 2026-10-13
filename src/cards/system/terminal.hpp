#pragma once

#include <array>
#include <chrono>
#include <atomic>
#include <format>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/ui_context.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
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
    bool render(rouen::ui::ui_context& ui) override;
    std::string get_uri() const override;
    
    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }

    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::string& get_last_command() const noexcept { return last_command_; }

    [[nodiscard]] const std::string& get_shell_path() const noexcept { return shell_path_; }
    void set_shell_path(std::string_view path) { shell_path_ = path; }

    [[nodiscard]] int get_last_exit_code() const noexcept { return last_exit_code_; }
    void set_last_exit_code(int code) noexcept { last_exit_code_ = code; }

    [[nodiscard]] const std::string& get_current_working_dir() const noexcept { return current_working_dir; }
    void set_current_working_dir(std::string_view dir) { current_working_dir = dir; }

    [[nodiscard]] std::string get_recent_output() const;
    
private:
    void execute_command_line(std::string cmd);
    void render_sudo_prompt(float window_width);
    void render_command_input(float window_width);
    void test_stderr_output();
    bool navigate_history(bool go_back, char* buffer, size_t buffer_size);
    bool handle_slash_command(const std::string& cmd);
    
    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // State synchronization
    std::string shell_path_{"/bin/zsh"};
    int last_exit_code_{0};
    std::string last_action_{};
    std::string last_command_{};

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
    std::atomic<bool> is_command_running{false};
    
    // Animation for running process
    int spinner_counter = 0;
    const char spinner_chars[4] = {'|', '/', '-', '\\'};
};

} // namespace rouen::cards
