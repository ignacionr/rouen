#include "terminal_output.hpp"
#include <vector>

namespace rouen::cards {

namespace {
void RenderAnsiText(const std::string& line, const ImVec4& default_color) {
    ImVec4 current_color = default_color;
    bool has_color_push = false;
    
    size_t last_pos = 0;
    size_t pos = 0;
    bool first_segment = true;
    
    while ((pos = line.find("\x1b[", last_pos)) != std::string::npos) {
        // Render text up to the escape sequence
        if (pos > last_pos) {
            std::string segment = line.substr(last_pos, pos - last_pos);
            if (!first_segment) {
                ImGui::SameLine(0.0f, 0.0f);
            }
            if (has_color_push) {
                ImGui::PushStyleColor(ImGuiCol_Text, current_color);
            }
            ImGui::TextUnformatted(segment.c_str());
            if (has_color_push) {
                ImGui::PopStyleColor();
            }
            first_segment = false;
        }
        
        // Find the end of the escape sequence (marked by a command letter like 'm')
        size_t end_pos = line.find_first_of("ABCDEFGHJKSTm", pos + 2);
        if (end_pos == std::string::npos) {
            last_pos = pos + 2;
            continue;
        }
        
        char code_type = line[end_pos];
        if (code_type == 'm') {
            std::string params_str = line.substr(pos + 2, end_pos - (pos + 2));
            std::vector<int> params;
            size_t p_last_pos = 0;
            size_t p_pos = 0;
            while ((p_pos = params_str.find(';', p_last_pos)) != std::string::npos) {
                try {
                    params.push_back(std::stoi(params_str.substr(p_last_pos, p_pos - p_last_pos)));
                } catch (...) {}
                p_last_pos = p_pos + 1;
            }
            if (p_last_pos < params_str.size() || params_str.empty()) {
                try {
                    params.push_back(std::stoi(params_str.substr(p_last_pos)));
                } catch (...) {
                    if (params_str.empty()) params.push_back(0); // empty means reset
                }
            }
            
            // Apply parameters
            for (int p : params) {
                if (p == 0) {
                    current_color = default_color;
                    has_color_push = false;
                } else if (p >= 30 && p <= 37) {
                    has_color_push = true;
                    switch (p) {
                        case 30: current_color = ImVec4(0.15f, 0.15f, 0.15f, 1.0f); break; // Black
                        case 31: current_color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); break;   // Red
                        case 32: current_color = ImVec4(0.3f, 0.9f, 0.3f, 1.0f); break;   // Green
                        case 33: current_color = ImVec4(0.9f, 0.9f, 0.3f, 1.0f); break;   // Yellow
                        case 34: current_color = ImVec4(0.3f, 0.3f, 0.9f, 1.0f); break;   // Blue
                        case 35: current_color = ImVec4(0.9f, 0.3f, 0.9f, 1.0f); break;   // Magenta
                        case 36: current_color = ImVec4(0.3f, 0.9f, 0.9f, 1.0f); break;   // Cyan
                        case 37: current_color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); break;   // White
                    }
                } else if (p >= 90 && p <= 97) {
                    has_color_push = true;
                    switch (p) {
                        case 90: current_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;   // Gray
                        case 91: current_color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); break;   // Bright Red
                        case 92: current_color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); break;   // Bright Green
                        case 93: current_color = ImVec4(1.0f, 1.0f, 0.5f, 1.0f); break;   // Bright Yellow
                        case 94: current_color = ImVec4(0.5f, 0.5f, 1.0f, 1.0f); break;   // Bright Blue
                        case 95: current_color = ImVec4(1.0f, 0.5f, 1.0f, 1.0f); break;   // Bright Magenta
                        case 96: current_color = ImVec4(0.5f, 1.0f, 1.0f, 1.0f); break;   // Bright Cyan
                        case 97: current_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;   // Bright White
                    }
                } else if (p == 39) {
                    current_color = default_color;
                    has_color_push = false;
                }
            }
        }
        
        last_pos = end_pos + 1;
    }
    
    // Render the final segment
    if (last_pos < line.size()) {
        std::string segment = line.substr(last_pos);
        if (!first_segment) {
            ImGui::SameLine(0.0f, 0.0f);
        }
        if (has_color_push) {
            ImGui::PushStyleColor(ImGuiCol_Text, current_color);
        }
        ImGui::TextUnformatted(segment.c_str());
        if (has_color_push) {
            ImGui::PopStyleColor();
        }
    } else if (first_segment) {
        ImGui::TextUnformatted("");
    }
}
}

void TerminalOutput::add_to_output(const std::string& text, OutputType type) {
    std::lock_guard<std::mutex> lock(output_mutex);
    output_buffer.emplace_back(text, type);
    
    // Clear partial buffers of the corresponding type since we now have a new complete line
    if (type == OutputType::StdOut) {
        stdout_partial.clear();
    } else if (type == OutputType::StdErr) {
        stderr_partial.clear();
    }
    
    // Keep buffer from growing too large (max 500 lines)
    if (output_buffer.size() > 500) {
        output_buffer.pop_front();
    }
    
    should_auto_scroll = true;
}

void TerminalOutput::add_multiple_outputs(const std::vector<std::pair<std::string, OutputType>>& entries) {
    std::lock_guard<std::mutex> lock(output_mutex);
    for (const auto& [text, type] : entries) {
        output_buffer.emplace_back(text, type);
        
        // Clear partial buffers of the corresponding type
        if (type == OutputType::StdOut) {
            stdout_partial.clear();
        } else if (type == OutputType::StdErr) {
            stderr_partial.clear();
        }
    }
    
    // Keep buffer from growing too large (max 500 lines)
    while (output_buffer.size() > 500) {
        output_buffer.pop_front();
    }
    
    should_auto_scroll = true;
}

void TerminalOutput::clear_terminal(const std::string& cwd) {
    std::lock_guard<std::mutex> lock(output_mutex);
    output_buffer.clear();
    stdout_partial.clear();
    stderr_partial.clear();
    output_buffer.emplace_back("Terminal cleared.", OutputType::System);
    output_buffer.emplace_back("", OutputType::Blank);
    
    // Add prompt with updated working directory
    output_buffer.emplace_back(std::format("{}$ ", cwd), OutputType::Prompt);
    should_auto_scroll = true;
}

void TerminalOutput::add_prompt(const std::string& working_dir) {
    // Add the directory prompt (like "user@host:~/dir$")
    std::string prompt = std::format("{}$ ", working_dir);
    add_to_output(prompt, OutputType::Prompt);
}

void TerminalOutput::set_partial_line(const std::string& text, OutputType type) {
    std::lock_guard<std::mutex> lock(output_mutex);
    if (type == OutputType::StdOut) {
        stdout_partial = text;
    } else if (type == OutputType::StdErr) {
        stderr_partial = text;
    }
    should_auto_scroll = true;
}

void TerminalOutput::display_buffer(const ImVec4* colors, bool& auto_scroll) {
    std::lock_guard<std::mutex> lock(output_mutex);
    for (const auto& [text, type] : output_buffer) {
        ImVec4 default_color;
        switch (type) {
            case OutputType::Command:
                default_color = colors[6];
                break;
            case OutputType::StdOut:
                default_color = colors[2];
                break;
            case OutputType::StdErr:
                default_color = colors[4];
                break;
            case OutputType::System:
                default_color = colors[5];
                break;
            case OutputType::Prompt:
                default_color = colors[3];
                break;
            case OutputType::Blank:
                ImGui::Spacing();
                continue;
            default:
                default_color = colors[2];
        }
        
        RenderAnsiText(text, default_color);
    }
    
    // Display partial stdout if present
    if (!stdout_partial.empty()) {
        RenderAnsiText(stdout_partial, colors[2]);
    }
    
    // Display partial stderr if present
    if (!stderr_partial.empty()) {
        RenderAnsiText(stderr_partial, colors[4]);
    }
    
    // Auto-scroll to the bottom if needed
    if (should_auto_scroll || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
        ImGui::SetScrollHereY(1.0f);
        should_auto_scroll = false;
    }
    
    // Update the passed reference
    auto_scroll = should_auto_scroll;
}

} // namespace rouen::cards
