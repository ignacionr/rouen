#include "terminal.hpp"
#include "../../registrar.hpp"
#include "../../fonts.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
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
    
    const char* shell_env = std::getenv("SHELL");
    if (shell_env && shell_env[0] != '\0') {
        shell_path_ = shell_env;
    } else {
        shell_path_ = "/bin/zsh";
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

bool terminal::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now;
            }

            if (!adaptive_error_.empty()) {
                ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
            } else {
                adaptive_renderer_.render(
                    adaptive_bound_, adaptive_input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

        rouen::fonts::with_font const fnt{rouen::fonts::FontType::Mono};
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

bool terminal::render() {
    rouen::ui::imgui_ui_context_impl ui;
    return render(ui);
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
    ImGui::SetNextItemWidth(window_width - 70.0f);
    bool const enter_pressed = ImGui::InputText("##SudoPassword", password_buffer, static_cast<int>(sizeof(password_buffer)), 
                                        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                                        nullptr, nullptr);
    
    ImGui::SameLine();
    
    // Submit button
    if (ImGui::Button("Submit", ImVec2(60.0f, 0)) || enter_pressed) {
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
    // If buttons are visible, make room for them
    if (is_command_running.load()) {
        input_width -= 160.0f; // Reserve space for spinner + Ctrl+C + Kill buttons + spacing
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
        bool const ctrl_down = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        std::string const user_cmd = input_buffer;
        // Always update current_working_dir from bash before handling shortcuts, but only if no command is running
        if (bash.is_interactive() && !is_command_running.load()) {
            std::string const new_cwd = bash.get_cwd();
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
        } else if (ctrl_down && !is_command_running.load()) {
            last_command_ = user_cmd;
            std::string actual_command;
            commands.execute_command(input_buffer, true, output, current_working_dir, 
                                 command_history, history_index, is_command_running, 
                                 bash.is_interactive(), show_sudo_prompt, sudo_command, &actual_command);
            if (bash.is_interactive() && !show_sudo_prompt && !actual_command.empty()) {
                bash.send_to_bash(actual_command, false);
            }
        } else {
            execute_command_line(user_cmd);
        }
        input_buffer[0] = '\0';  // Clear the input
        focus_input = true;
    }
    
    ImGui::PopStyleColor();
}

void terminal::execute_command_line(std::string user_cmd) {
    if (user_cmd.empty()) return;

    last_command_ = user_cmd;

    // Always update current_working_dir from bash before handling shortcuts, but only if no command is running
    if (bash.is_interactive() && !is_command_running.load()) {
        std::string const new_cwd = bash.get_cwd();
        if (!new_cwd.empty()) current_working_dir = new_cwd;
    }

    if (handle_slash_command(user_cmd)) {
        // Handled as slash command
    } else if (is_command_running.load()) {
        // A process is running in the foreground. Send the raw input to its stdin.
        output.add_to_output(user_cmd, OutputType::Command);
        if (bash.is_interactive()) {
            bash.send_to_bash(user_cmd, true); // raw = true
        }
    } else {
        std::string actual_command;
        commands.execute_command(user_cmd, false, output, current_working_dir, 
                             command_history, history_index, is_command_running, 
                             bash.is_interactive(), show_sudo_prompt, sudo_command, &actual_command);
        if (bash.is_interactive() && !show_sudo_prompt && !actual_command.empty()) {
            // Normal commands should emit the completion marker so the UI can
            // reliably clear the running state when the foreground job exits.
            bash.send_to_bash(actual_command, false);
        }
    }
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
        std::string const new_cwd = bash.get_cwd();
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
        size_t const space_pos = clean_cmd.find_first_of(" \t");
        std::string const raw_arg = (space_pos != std::string::npos) ? clean_cmd.substr(space_pos + 1) : "";
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
        std::string const full_text = output.get_all_text();
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
            for (char const c : prompt) {
                if (c == '"' || c == '\\' || c == '`' || c == '$') {
                    escaped_prompt += '\\';
                }
                escaped_prompt += c;
            }
            
            const char* home_env = std::getenv("HOME");
            std::string const home_dir = home_env ? home_env : "";
            std::string agy_path = home_dir.empty() ? "" : (std::filesystem::path(home_dir) / ".local" / "bin" / "agy").string();
            std::string src_path = home_dir.empty() ? "" : (std::filesystem::path(home_dir) / "src").string();
            
            std::string const agy_cmd = std::format(
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
    std::string const home_dir = (home && home[0] != '\0') ? home : std::filesystem::current_path().string();
    if (current_working_dir.empty() || current_working_dir == home_dir) {
        return "terminal";
    }
    return std::format("terminal:{}", current_working_dir);
}

std::string terminal::get_recent_output() const {
    return output.get_all_text();
}

std::string terminal::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::vector<glz::json_t> body;

    // 1. Header Container
    {
        glz::json_t container;
        container["type"] = "Container";
        container["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> cols;

        // Column 1: Emoji icon
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> col_items;
            glz::json_t tb;
            tb["type"] = "TextBlock";
            tb["text"] = "💻";
            tb["size"] = "Large";
            col_items.push_back(std::move(tb));
            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        // Column 2: Title and Subtitle
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "stretch";
            std::vector<glz::json_t> col_items;

            glz::json_t tb_title;
            tb_title["type"] = "TextBlock";
            tb_title["text"] = "Interactive Shell Terminal";
            tb_title["weight"] = "Bolder";
            tb_title["size"] = "Large";
            col_items.push_back(std::move(tb_title));

            glz::json_t tb_sub;
            tb_sub["type"] = "TextBlock";
            tb_sub["text"] = std::format("{} • {}", shell_path_, current_working_dir);
            tb_sub["isSubtle"] = true;
            tb_sub["size"] = "Small";
            col_items.push_back(std::move(tb_sub));

            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        // Column 3: Status Badge / Exit Code
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> col_items;

            glz::json_t tb_exit;
            tb_exit["type"] = "TextBlock";
            tb_exit["text"] = std::format("EXIT: {}", last_exit_code_);
            tb_exit["weight"] = "Bolder";
            tb_exit["color"] = (last_exit_code_ == 0 ? "Good" : "Attention");
            tb_exit["size"] = "Small";
            col_items.push_back(std::move(tb_exit));

            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        col_set["columns"] = std::move(cols);
        std::vector<glz::json_t> container_items;
        container_items.push_back(std::move(col_set));
        container["items"] = std::move(container_items);
        body.push_back(std::move(container));
    }

    // 2. Output Container
    {
        glz::json_t container;
        container["type"] = "Container";
        container["style"] = "accent";
        std::vector<glz::json_t> items;

        std::string raw_output = get_recent_output();
        if (raw_output.empty()) {
            raw_output = "(No output)";
        }

        glz::json_t tb_output;
        tb_output["type"] = "TextBlock";
        tb_output["text"] = std::move(raw_output);
        tb_output["fontType"] = "Monospace";
        tb_output["wrap"] = true;
        tb_output["size"] = "Small";
        items.push_back(std::move(tb_output));

        container["items"] = std::move(items);
        body.push_back(std::move(container));
    }

    // 3. Command Input Container
    {
        glz::json_t container;
        container["type"] = "Container";
        container["separator"] = true;
        std::vector<glz::json_t> items;

        glz::json_t input;
        input["type"] = "Input.Text";
        input["id"] = "command_line";
        input["placeholder"] = "Enter command to execute (e.g. git status)...";
        items.push_back(std::move(input));

        container["items"] = std::move(items);
        body.push_back(std::move(container));
    }

    card["body"] = std::move(body);

    // 4. Actions
    {
        std::vector<glz::json_t> actions;

        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Execute";
            act["verb"] = "term_exec";
            actions.push_back(std::move(act));
        }
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Clear Console";
            act["verb"] = "term_clear";
            actions.push_back(std::move(act));
        }
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Interrupt (Ctrl+C)";
            act["verb"] = "term_sigint";
            actions.push_back(std::move(act));
        }

        card["actions"] = std::move(actions);
    }

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

void terminal::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        glz::json_t payload{};
        std::string verb;
        glz::json_t data{};

        auto err = glz::read_json(payload, std::string(action_json));
        if (!err && payload.is_object()) {
            if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                verb = payload["verb"].get<std::string>();
            } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                verb = payload["action"].get<std::string>();
            }

            if (payload.contains("data") && payload["data"].is_object()) {
                data = payload["data"];
            } else {
                data = payload;
            }
        } else {
            verb = std::string(action_json);
        }

        if (verb == "term_exec") {
            last_action_ = "term_exec";
            std::string cmd;
            if (data.is_object()) {
                if (data.contains("command_line") && data["command_line"].holds<std::string>()) {
                    cmd = data["command_line"].get<std::string>();
                } else if (data.contains("cmd") && data["cmd"].holds<std::string>()) {
                    cmd = data["cmd"].get<std::string>();
                } else if (data.contains("command") && data["command"].holds<std::string>()) {
                    cmd = data["command"].get<std::string>();
                }
            }

            if (!cmd.empty()) {
                execute_command_line(cmd);
            }
            last_adaptive_parse_time_ = {};
        } else if (verb == "term_clear") {
            last_action_ = "term_clear";
            output.clear_terminal(current_working_dir);
            last_adaptive_parse_time_ = {};
        } else if (verb == "term_sigint") {
            last_action_ = "term_sigint";
            bash.send_sigint();
            output.add_to_output("Sent Ctrl+C (SIGINT) to running process.", OutputType::System);
            last_adaptive_parse_time_ = {};
        } else if (verb == "toggle_adaptive_view") {
            adaptive_view_mode = !adaptive_view_mode;
            last_action_ = "toggle_adaptive_view";
            last_adaptive_parse_time_ = {};
        }
    } catch (const std::exception& e) {
        TERM_ERROR_FMT("Adaptive card action error: {}", e.what());
    }
}

} // namespace rouen::cards
