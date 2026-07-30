#include "terminal.hpp"
#include "../../registrar.hpp"
#include "../../fonts.hpp"
#include "cards/system/terminal_output.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <string>
#include <string_view>

namespace rouen::cards {

namespace {
std::string unescape_shell_path(const std::string& input) {
    std::string str = input;
    str.erase(0, str.find_first_not_of(" \t\n\r"));
    if (str.empty()) return "";
    str.erase(str.find_last_not_of(" \t\n\r") + 1);

    if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') || 
                            (str.front() == '\'' && str.back() == '\''))) {
        return str.substr(1, str.size() - 2);
    }

    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            result += str[++i];
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string resolve_terminal_file_path(const std::string& raw_input, const std::string& cwd) {
    std::string cleaned = unescape_shell_path(raw_input);
    if (cleaned.empty()) return "";

    if (cleaned == "~" || cleaned.starts_with("~/")) {
        const char* home = std::getenv("HOME");
        if (home) {
            cleaned = (cleaned == "~") ? std::string(home) : std::string(home) + cleaned.substr(1);
        }
    }

    std::filesystem::path path_obj(cleaned);
    if (!path_obj.is_absolute()) {
        path_obj = std::filesystem::path(cwd) / path_obj;
    }
    return path_obj.lexically_normal().string();
}
}

terminal::terminal(std::string_view initial_dir) {
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
    
    // Initialize working directory ($HOME if not specified)
    if (initial_dir.empty()) {
        const char* home = std::getenv("HOME");
        if (home && home[0] != '\0') {
            current_working_dir = home;
        } else {
            current_working_dir = std::filesystem::current_path().string();
        }
    } else {
        current_working_dir = std::string(initial_dir);
    }
    
    // Initialize the interactive bash session
    bash.initialize_bash_session(current_working_dir, output, is_command_running);
    
    // Add initial welcome message
    output.add_to_output(std::format("Interactive Bash Terminal initialized in {}", current_working_dir), OutputType::System);
    output.add_to_output("Type commands and press Enter to execute. Use Up/Down arrows for history.", OutputType::System);
    output.add_to_output("Press Ctrl+Enter to use Grok AI to convert natural language to Bash commands.", OutputType::System);
    output.add_to_output("", OutputType::Blank);
    
    // Add prompt
    output.add_prompt(current_working_dir);
}

terminal::~terminal() {
    // Stop all running processes and terminate the bash session
    bash.terminate_bash_session();
}

void terminal::test_stderr_output() {
    const std::string test_cmd = "bash -c 'echo This is stdout; echo This is stderr >&2; ls /nonexistent-directory; invalid_command'";
    output.add_to_output("Running stderr test command...", OutputType::System);
    commands.execute_command(test_cmd, false, output, current_working_dir, 
                         command_history, history_index, is_command_running,
                         bash.is_interactive(), show_sudo_prompt, sudo_command);
}

bool terminal::render() {
    return render_window([this]() {
        rouen::fonts::with_font fnt{rouen::fonts::FontType::Mono};
        // Calculate window dimensions
        const float window_width = ImGui::GetContentRegionAvail().x;
        const float footer_height = ImGui::GetFrameHeightWithSpacing() + (ImGui::GetStyle().ItemSpacing.y * 2);
        
        // Test for stderr - press F5 for a quick test
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            test_stderr_output();
        }
        
        // Render the output area (taking most of the space)
        if (ImGui::BeginChild("OutputScrollRegion", ImVec2(window_width, -footer_height), true)) {
            // Process any output from running commands
            commands.check_command_output(bash.is_interactive(), is_command_running, output);
            
            // Display output buffer
            output.display_buffer(colors.data(), should_auto_scroll);
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
    });
}

void terminal::render_sudo_prompt(float window_width) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colors[7]);
    
    // Focus the password input on first display
    static bool first_display = true;
    if (first_display) {
        ImGui::SetKeyboardFocusHere();
        first_display = false;
    }
    
    // Password input (displayed as asterisks)
    static char password_buffer[128] = "";
    float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    ImGui::SetNextItemWidth(window_width - (70.0f * dpi_scale));
    bool enter_pressed = ImGui::InputText("##SudoPassword", password_buffer, static_cast<int>(sizeof(password_buffer)), 
                                        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                                        nullptr, nullptr);
    
    ImGui::SameLine();
    
    // Submit button
    if (ImGui::Button("Submit", ImVec2(60.0f * dpi_scale, 0)) || enter_pressed) {
        if (password_buffer[0] != '\0') {
            // Attempt to restart the bash session with sudo
            bash.restart_with_sudo(password_buffer, current_working_dir, 
                                 sudo_command, output, is_command_running);
            
            // Clear password for security
            memset(password_buffer, 0, sizeof(password_buffer));
            
            // Hide the prompt and reset for next time
            show_sudo_prompt = false;
            first_display = true;
            
            // If we had a sudo command to run, add it to command history (without 'sudo')
            if (!sudo_command.empty()) {
                // Add to command history
                command_history.push_back(sudo_command);
                if (command_history.size() > 50) {  // Limit history size
                    command_history.erase(command_history.begin());
                }
                history_index = command_history.size();
                
                // Clear the stored sudo command
                sudo_command.clear();
            }
        }
    }
    
    ImGui::PopStyleColor();
}

void terminal::render_command_input(float window_width) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colors[7]);
    
    // Focus the input box when requested
    if (focus_input) {
        ImGui::SetKeyboardFocusHere();
        focus_input = false;
    }
    
    // Input field - Using standard char array
    static char input_buffer[1024] = "";
    // Copy current input_text to input_buffer if not empty
    if (!input_text.empty() && input_buffer[0] == '\0') {
        strncpy(input_buffer, input_text.c_str(), sizeof(input_buffer) - 1);
        input_buffer[sizeof(input_buffer) - 1] = '\0';
        input_text.clear();
    }

    bool enter_pressed = false;
    bool input_active = false;
    float const dpi_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    float input_width = window_width;
    // If buttons are visible, make room for them
    if (is_command_running.load()) {
        input_width -= 160.0f * dpi_scale; // Reserve space for spinner + Ctrl+C + Kill buttons + spacing
    }
    ImGui::SetNextItemWidth(input_width);
    if (ImGui::InputText("##CommandInput", input_buffer, static_cast<int>(sizeof(input_buffer)), 
                      ImGuiInputTextFlags_EnterReturnsTrue,
                      nullptr, nullptr)) {
        enter_pressed = true;
    }
    input_active = ImGui::IsItemActive();

    // If command is running, show spinner and control buttons to the right of the input
    if (is_command_running.load()) {
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "%c", spinner_chars[(spinner_counter/5) % 4]);
        spinner_counter++;
        ImGui::SameLine();
        if (ImGui::SmallButton("\u23CE Ctrl+C")) {
            bash.send_sigint();
            output.add_to_output("Sent Ctrl+C (SIGINT) to running process.", OutputType::System);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("\u23F9 Kill")) {
            bash.send_sigkill();
            output.add_to_output("Sent Kill (SIGKILL) to child processes.", OutputType::System);
        }
    }

    // Only process shortcuts if the input field is focused
    if (input_active) {
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
    }
    
    // Process the command if enter was pressed
    if (enter_pressed && input_buffer[0] != '\0') {
        bool ctrl_down = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        std::string user_cmd = input_buffer;
        // Always update current_working_dir from bash before handling shortcuts, but only if no command is running
        if (bash.is_interactive() && !is_command_running.load()) {
            std::string new_cwd = bash.get_cwd();
            if (!new_cwd.empty()) current_working_dir = new_cwd;
        }
        if (handle_slash_command(user_cmd)) {
            // Handled as slash command
        }
        // Check for 'ed <filename>' shortcut with Ctrl+Enter
        else if (ctrl_down && user_cmd.starts_with("ed ") && user_cmd.size() > 3) {
            std::string resolved_path = resolve_terminal_file_path(user_cmd.substr(3), current_working_dir);
            if (!resolved_path.empty()) {
                "edit"_sfn(resolved_path);
                output.add_to_output(std::format("Editing file: {}", resolved_path), OutputType::System);
            }
            command_history.push_back(user_cmd);
            if (command_history.size() > 50) command_history.erase(command_history.begin());
            history_index = command_history.size();
        } else if (is_command_running.load()) {
            // A process is running in the foreground. Send the raw input to its stdin.
            output.add_to_output(user_cmd, OutputType::Command);
            if (bash.is_interactive()) {
                bash.send_to_bash(user_cmd, true); // raw = true
            }
        } else {
            bool use_llm = ctrl_down;
            std::string actual_command;
            commands.execute_command(input_buffer, use_llm, output, current_working_dir, 
                                 command_history, history_index, is_command_running, 
                                 bash.is_interactive(), show_sudo_prompt, sudo_command, &actual_command);
            if (bash.is_interactive() && !show_sudo_prompt && !actual_command.empty()) {
                // Normal commands should emit the completion marker so the UI can
                // reliably clear the running state when the foreground job exits.
                bash.send_to_bash(actual_command, false);
            }
        }
        input_buffer[0] = '\0';  // Clear the input
        focus_input = true;
    }
    
    ImGui::PopStyleColor();
}

bool terminal::navigate_history(bool go_back, char* buffer, size_t buffer_size) {
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
        } if (history_index == command_history.size() - 1) {
            // At the newest command, clear the input
            history_index = command_history.size();
            buffer[0] = '\0';
            return true;
        }
    }
    return false;
}

bool terminal::handle_slash_command(const std::string& cmd) {
    if (!cmd.starts_with("/")) {
        return false;
    }

    // Ensure working directory is up to date from interactive bash session
    if (bash.is_interactive()) {
        std::string new_cwd = bash.get_cwd();
        if (!new_cwd.empty()) {
            current_working_dir = new_cwd;
        }
    }

    // Output the command typed by the user to the terminal output log so they see it
    output.add_to_output(cmd, OutputType::Command);

    // Also add it to history so they can navigate back to it (standard shell behavior for slash commands)
    command_history.push_back(cmd);
    if (command_history.size() > 50) {
        command_history.erase(command_history.begin());
    }
    history_index = command_history.size();

    // Parse the slash command
    std::string clean_cmd = cmd;
    // Remove leading/trailing spaces
    clean_cmd.erase(0, clean_cmd.find_first_not_of(" \t\n\r"));
    clean_cmd.erase(clean_cmd.find_last_not_of(" \t\n\r") + 1);

    if (clean_cmd == "/help") {
        output.add_to_output("Available slash commands:", OutputType::System);
        output.add_to_output("  /help           - Show this help message", OutputType::System);
        output.add_to_output("  /clear          - Clear terminal display buffer", OutputType::System);
        output.add_to_output("  /clear-history  - Clear terminal command history", OutputType::System);
        output.add_to_output("  /reset          - Restart interactive bash session", OutputType::System);
        output.add_to_output("  /info           - Show terminal card status and environment info", OutputType::System);
        output.add_to_output("  /edit <file>    - Open file in Rouen editor", OutputType::System);
        output.add_to_output("  /copy           - Copy all terminal contents to system clipboard", OutputType::System);
        output.add_to_output("  /agy <prompt>   - Run Antigravity CLI print command using Nix", OutputType::System);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(current_working_dir);
    }
    else if (clean_cmd == "/clear" || clean_cmd == "/cls") {
        output.clear_terminal(current_working_dir);
    }
    else if (clean_cmd == "/clear-history" || clean_cmd == "/clear_history" || clean_cmd == "/history -c") {
        command_history.clear();
        history_index = 0;
        output.add_to_output("Terminal command history cleared.", OutputType::System);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(current_working_dir);
    }
    else if (clean_cmd == "/reset") {
        output.add_to_output("Restarting interactive bash session...", OutputType::System);
        bash.terminate_bash_session();
        bash.initialize_bash_session(current_working_dir, output, is_command_running);
        output.add_to_output("Bash session restarted.", OutputType::System);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(current_working_dir);
    }
    else if (clean_cmd == "/info") {
        output.add_to_output("Terminal Card Info:", OutputType::System);
        output.add_to_output(std::format("  Working Dir: {}", current_working_dir), OutputType::System);
        output.add_to_output(std::format("  History Size: {} / 50", command_history.size()), OutputType::System);
        output.add_to_output(std::format("  Interactive: {}", bash.is_interactive() ? "Yes" : "No"), OutputType::System);
        output.add_to_output(std::format("  Process Running: {}", is_command_running.load() ? "Yes" : "No"), OutputType::System);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(current_working_dir);
    }
    else if (clean_cmd == "/edit" || clean_cmd.starts_with("/edit ") || clean_cmd.starts_with("/edit\t")) {
        size_t space_pos = clean_cmd.find_first_of(" \t");
        std::string raw_arg = (space_pos != std::string::npos) ? clean_cmd.substr(space_pos + 1) : "";
        std::string resolved_path = resolve_terminal_file_path(raw_arg, current_working_dir);

        if (resolved_path.empty()) {
            output.add_to_output("Error: /edit requires a filename (e.g., /edit main.cpp)", OutputType::StdErr);
            output.add_to_output("", OutputType::Blank);
            output.add_prompt(current_working_dir);
        } else {
            "edit"_sfn(resolved_path);
            output.add_to_output(std::format("Editing file: {}", resolved_path), OutputType::System);
            output.add_to_output("", OutputType::Blank);
            output.add_prompt(current_working_dir);
        }
    }
    else if (clean_cmd == "/copy" || clean_cmd == "/copy-all" || clean_cmd == "/copyall" || clean_cmd == "/clipboard") {
        std::string full_text = output.get_all_text();
        ImGui::SetClipboardText(full_text.c_str());
        output.add_to_output("Copied all terminal contents to clipboard.", OutputType::System);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(current_working_dir);
    }
    else if (clean_cmd.starts_with("/agy ")) {
        std::string prompt = clean_cmd.substr(5);
        // Clean prompt
        prompt.erase(0, prompt.find_first_not_of(" \t\n\r"));
        prompt.erase(prompt.find_last_not_of(" \t\n\r") + 1);
        
        if (prompt.empty()) {
            output.add_to_output("Error: /agy requires a prompt (e.g., /agy list files)", OutputType::StdErr);
            output.add_to_output("", OutputType::Blank);
            output.add_prompt(current_working_dir);
        } else {
            // Escape special shell characters in the prompt
            std::string escaped_prompt;
            for (char c : prompt) {
                if (c == '"' || c == '\\' || c == '`' || c == '$') {
                    escaped_prompt += '\\';
                }
                escaped_prompt += c;
            }
            
            const char* home_env = std::getenv("HOME");
            std::string home_dir = home_env ? home_env : "";
            std::string agy_path = home_dir.empty() ? "" : (std::filesystem::path(home_dir) / ".local" / "bin" / "agy").string();
            std::string src_path = home_dir.empty() ? "" : (std::filesystem::path(home_dir) / "src").string();
            
            std::string agy_cmd = std::format(
                R"((if command -v agy >/dev/null 2>&1; then agy --add-dir "$PWD" --prompt "{0}" < /dev/null; elif [ -n "{1}" ] && [ -f "{1}" ]; then "{1}" --add-dir "$PWD" --prompt "{0}" < /dev/null; elif [ -n "{2}" ] && [ -d "{2}" ] && [ -f "{2}/flake.nix" ]; then NIXPKGS_ALLOW_UNFREE=1 nix shell "{2}" -c agy --add-dir "$PWD" --prompt "{0}" < /dev/null; else NIXPKGS_ALLOW_UNFREE=1 nix shell . -c agy --add-dir "$PWD" --prompt "{0}" < /dev/null; fi))",
                escaped_prompt, agy_path, src_path);
            
            // Execute the command in the bash session
            is_command_running.store(true);
            if (bash.is_interactive()) {
                bash.send_to_bash(agy_cmd);
            } else {
                commands.execute_command(agy_cmd, false, output, current_working_dir,
                                         command_history, history_index, is_command_running,
                                         bash.is_interactive(), show_sudo_prompt, sudo_command);
            }
        }
    }
    else {
        output.add_to_output(std::format("Unknown slash command: {}. Type /help for a list of commands.", clean_cmd), OutputType::StdErr);
        output.add_to_output("", OutputType::Blank);
        output.add_prompt(current_working_dir);
    }

    return true;
}

std::string terminal::get_uri() const {
    const char* home = std::getenv("HOME");
    std::string home_dir = (home && home[0] != '\0') ? home : std::filesystem::current_path().string();
    if (current_working_dir.empty() || current_working_dir == home_dir) {
        return "terminal";
    }
    return std::format("terminal:{}", current_working_dir);
}

} // namespace rouen::cards
