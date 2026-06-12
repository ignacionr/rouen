#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <format>
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/debug.hpp"

namespace rouen::cards {

// Terminal output types
enum class OutputType {
    StdOut,     // Standard output (white)
    StdErr,     // Error output (red)
    Command,    // Commands entered by user (blue)
    System,     // System messages (yellow)
    Prompt,     // Command prompt (green)
    Blank       // Empty line
};

class TerminalOutput {
public:
    void add_to_output(const std::string& text, OutputType type);
    void add_multiple_outputs(const std::vector<std::pair<std::string, OutputType>>& entries);
    void clear_terminal(const std::string& cwd);
    void add_prompt(const std::string& working_dir);
    void set_partial_line(const std::string& text, OutputType type);
    
    // Display output buffer in the UI
    void display_buffer(const ImVec4* colors, bool& auto_scroll);
    
    using OutputEntry = std::pair<std::string, OutputType>;
    
private:
    std::deque<OutputEntry> output_buffer;
    std::string stdout_partial;
    std::string stderr_partial;
    std::mutex output_mutex;
    bool should_auto_scroll = false;
};

} // namespace rouen::cards
