#pragma once

#include <filesystem>
#include <format>
#include <string>

#include <imgui/imgui.h>

#include "card.hpp"

namespace rouen::cards
{
    struct fs_directory : public card
    {
        fs_directory(std::string_view path) : path_{path}
        {
            first_color = {0.37f, 0.53f, 0.71f, 1.0f};  // Changed from orange to blue accent color
            second_color = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
            name(path);
        }

        void receive_keystrokes()
        {
            for (char c : "keystrokes"_fns())
            {
                if (c == '\b')
                {
                    if (!filter_.empty())
                    {
                        filter_.pop_back();
                    }
                }
                else if (c == '\n' || c == '\033' || c == '\r')
                {
                    filter_.clear();
                }
                else if (c == '\t') {
                    // ignore
                }
                else
                {
                    filter_ += c;
                }
            }
        }

        bool render() override
        {
            return render_window([this]()
                                 {
                if (ImGui::IsWindowFocused()) {
                    receive_keystrokes();
                }
                ImGui::Text("Directory: %s", path_.string().c_str());
                ImGui::Separator();
                
                // List files in the directory
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 255, 255)); // Parent directory in blue
                if (ImGui::Selectable("..")) {
                    // Go up one directory
                    auto entry = path_.parent_path();
                    // if Ctrl is pressed, open on a different card
                    if (ImGui::GetIO().KeyCtrl) {
                        "create_card"_sfn(std::format("dir:{}", entry.string()));
                    } else {
                        // otherwise, open in the same card
                        path_ = entry;
                        name(path_.string());
                    }
                }
                ImGui::PopStyleColor();
                
                for (const auto& entry : std::filesystem::directory_iterator(path_)) {
                    if (filter_.empty() || entry.path().filename().string().starts_with(filter_)) {
                    // Set color based on file type
                        if (entry.is_directory()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 96, 255)); // Directories in dark blue
                        } else if (entry.is_regular_file()) {
                            // Check file extension for common types
                            std::string ext = entry.path().extension().string();
                            if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cc") {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 220, 120, 255)); // Code files in green
                            } else if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".yaml" || ext == ".yml") {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 120, 255)); // Text files in yellow
                            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp") {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 120, 220, 255)); // Image files in purple
                            } else if (ext == ".exe" || ext == "" || ext == ".bin" || ext == ".sh") {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255)); // Executable files in red
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 200, 200, 255)); // Other files in light gray
                            }
                        } else if (entry.is_symlink()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 220, 220, 255)); // Symlinks in cyan
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 200, 200, 255)); // Other types in light gray
                        }
                        
                        if (ImGui::Selectable(entry.path().filename().string().c_str())) {
                            if (entry.is_directory()) {
                                // if Ctrl is pressed, open on a different card
                                if (ImGui::GetIO().KeyCtrl) {
                                    "create_card"_sfn(std::format("dir:{}", entry.path().string()));
                                } else {
                                    // otherwise, open in the same card
                                    path_ = entry.path();
                                    name(path_.string());
                                    filter_.clear();
                                }
                            }
                            else {
                                "edit"_sfn(entry.path().string());
                            }
                        }
                        ImGui::PopStyleColor(); // Don't forget to pop the color after each item
                }
            } });
        }

    private:
        std::filesystem::path path_;
        std::string filter_;
    };
}