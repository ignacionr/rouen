#include "terminal.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

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
    
    // Initialize working directory
    if (initial_dir.empty()) {
        current_working_dir = std::filesystem::current_path().string();
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
        // Calculate window dimensions
        const float window_width = ImGui::GetContentRegionAvail().x;
        const float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
        
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
    ImGui::SetNextItemWidth(window_width - 70);
    bool enter_pressed = ImGui::InputText("##SudoPassword", password_buffer, static_cast<int>(sizeof(password_buffer)), 
                                        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                                        nullptr, nullptr);
    
    ImGui::SameLine();
    
    // Submit button
    if (ImGui::Button("Submit", ImVec2(60, 0)) || enter_pressed) {
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
    float input_width = window_width;
    // If Ctrl+C button is visible, make room for it
    if (is_command_running) {
        input_width -= 80.0f; // Reserve space for spinner + button + spacing
    }
    ImGui::SetNextItemWidth(input_width);
    if (ImGui::InputText("##CommandInput", input_buffer, static_cast<int>(sizeof(input_buffer)), 
                      ImGuiInputTextFlags_EnterReturnsTrue,
                      nullptr, nullptr)) {
        enter_pressed = true;
    }
    input_active = ImGui::IsItemActive();

    // If Ctrl+C button is visible, show it to the right of the input
    if (is_command_running) {
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "%c", spinner_chars[(spinner_counter/5) % 4]);
        spinner_counter++;
        ImGui::SameLine();
        if (ImGui::SmallButton("\u23CE Ctrl+C")) {
            bash.send_sigint();
            output.add_to_output("Sent Ctrl+C (SIGINT) to running process.", OutputType::System);
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
        // Always update current_working_dir from bash before handling shortcuts
        if (bash.is_interactive()) {
            std::string new_cwd = bash.update_cwd_from_bash();
            if (!new_cwd.empty()) current_working_dir = new_cwd;
        }
        // Check for 'ed <filename>' shortcut with Ctrl+Enter
        if (ctrl_down && user_cmd.starts_with("ed ") && user_cmd.size() > 3) {
            // Extract filename (relative to current_working_dir)
            std::string filename = user_cmd.substr(3);
            std::filesystem::path file_path = std::filesystem::path(current_working_dir) / filename;
            // Call the edit connector slot
            "edit"_sfn(file_path.string());
            // Add to history for convenience
            command_history.push_back(user_cmd);
            if (command_history.size() > 50) command_history.erase(command_history.begin());
            history_index = command_history.size();
            // Output to terminal for feedback
            output.add_to_output(std::format("Editing file: {}", file_path.string()), OutputType::System);
        } else {
            bool use_llm = ctrl_down;
            std::string actual_command;
            commands.execute_command(input_buffer, use_llm, output, current_working_dir, 
                                 command_history, history_index, is_command_running, 
                                 bash.is_interactive(), show_sudo_prompt, sudo_command, &actual_command);
            if (bash.is_interactive() && !show_sudo_prompt && !actual_command.empty()) {
                // Send the actual command (Grok or user) to bash
                bash.send_to_bash(actual_command);
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
        } else if (history_index == command_history.size() - 1) {
            // At the newest command, clear the input
            history_index = command_history.size();
            buffer[0] = '\0';
            return true;
        }
    }
    return false;
}

std::string terminal::get_uri() const {
    if (current_working_dir.empty() || current_working_dir == std::filesystem::current_path().string()) {
        return "terminal";
    }
    return std::format("terminal:{}", current_working_dir);
}

} // namespace rouen::cards
