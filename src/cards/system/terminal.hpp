#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui/imgui.h>

// Remove imgui_stdlib dependency
// #include <imgui/misc/cpp/imgui_stdlib.h>

#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"

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
        
        // Add initial welcome message
        add_to_output(std::format("Terminal initialized in {}", current_working_dir), OutputType::System);
        add_to_output("Type commands and press Enter to execute. Use Up/Down arrows for history.", OutputType::System);
        add_to_output("", OutputType::Blank);
        
        // Add prompt
        add_prompt();
    }
    
    ~terminal() override {
        // Stop all running processes
        terminate_current_process();
    }
    
    bool render() override {
        return render_window([this]() {
            // Calculate window dimensions
            const float window_width = ImGui::GetContentRegionAvail().x;
            const float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
            
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
            
            // Command input area (at the bottom)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, colors[7]);
            
            // Focus the input box on start
            if (focus_input) {
                ImGui::SetKeyboardFocusHere();
                focus_input = false;
            }
            
            // Process keyboard shortcuts for history navigation
            bool enter_pressed = false;
            
            // Input field - Using standard char array instead of std::string to avoid imgui_stdlib dependency
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
                execute_command(input_buffer);
                input_buffer[0] = '\0';  // Clear the input
                focus_input = true;
            }
            
            ImGui::PopStyleColor();
            
            // Status indicator for running processes
            if (is_command_running) {
                ImGui::SameLine();
                ImGui::TextColored(colors[3], "%c", spinner_chars[(spinner_counter/5) % 4]);
                spinner_counter++;
            }
        });
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
        output_buffer.emplace_back(std::format("{}$ ", current_working_dir), OutputType::Prompt);
        should_auto_scroll = true;
    }
    
    void add_prompt() {
        // Add the directory prompt (like "user@host:~/dir$")
        std::string prompt = std::format("{}$ ", current_working_dir);
        add_to_output(prompt, OutputType::Prompt);
    }
    
    void execute_command(const std::string& command) {
        // Add command to output buffer and history
        add_to_output(command, OutputType::Command);
        
        // Add to command history
        command_history.push_back(command);
        if (command_history.size() > 50) {  // Limit history size
            command_history.erase(command_history.begin());
        }
        history_index = command_history.size();
        
        // Check for built-in commands
        if (command == "clear" || command == "cls") {
            // Use our new clear_terminal function to avoid mutex deadlock
            clear_terminal();
            return;
        } else if (command.substr(0, 3) == "cd ") {
            // Change directory
            std::string new_dir = command.substr(3);
            
            // Handle relative and absolute paths
            std::filesystem::path target_path;
            if (new_dir.empty() || new_dir == "~") {
                // Home directory
                target_path = std::getenv("HOME") ? std::getenv("HOME") : "/";
            } else if (new_dir[0] == '/') {
                // Absolute path
                target_path = new_dir;
            } else {
                // Relative path
                target_path = std::filesystem::path(current_working_dir) / new_dir;
            }
            
            // Check if directory exists
            std::error_code ec;
            if (std::filesystem::is_directory(target_path, ec)) {
                current_working_dir = std::filesystem::canonical(target_path).string();
                add_to_output(std::format("Directory changed to: {}", current_working_dir), OutputType::StdOut);
            } else {
                add_to_output(std::format("Error: '{}' is not a valid directory", new_dir), OutputType::StdErr);
            }
            
            add_to_output("", OutputType::Blank);
            add_prompt();
            return;
        }
        
        // Handle other commands - execute in a separate process
        execute_external_command(command);
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
        // Check if command has finished
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
    
    using OutputEntry = std::pair<std::string, OutputType>;
    std::deque<OutputEntry> output_buffer;
    std::mutex output_mutex;
    std::mutex command_pipe_mutex; // Mutex for synchronizing access to command_pipe
    
    std::string current_working_dir;
    FILE* command_pipe = nullptr;
    std::jthread output_reader_thread;
    bool is_command_running = false;
    
    // Animation for running process
    int spinner_counter = 0;
    const char spinner_chars[4] = {'|', '/', '-', '\\'};
};

} // namespace rouen::cards