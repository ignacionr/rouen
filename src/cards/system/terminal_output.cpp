#include "terminal_output.hpp"

namespace rouen::cards {

void TerminalOutput::add_to_output(const std::string& text, OutputType type) {
    std::lock_guard<std::mutex> lock(output_mutex);
    output_buffer.emplace_back(text, type);
    
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

void TerminalOutput::display_buffer(const ImVec4* colors, bool& auto_scroll) {
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
    
    // Update the passed reference
    auto_scroll = should_auto_scroll;
}

} // namespace rouen::cards
