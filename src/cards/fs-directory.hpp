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
            // Base colors (already set in the vector)
            colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};  // Primary color - blue accent
            colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Secondary color - turquoise
            
            // Additional colors for file types
            get_color(2, {150.0f/255.0f, 150.0f/255.0f, 255.0f/255.0f, 1.0f}); // Parent directory - blue (index 2)
            get_color(3, {0.0f/255.0f, 0.0f/255.0f, 96.0f/255.0f, 1.0f}); // Directories - dark blue (index 3)
            get_color(4, {120.0f/255.0f, 220.0f/255.0f, 120.0f/255.0f, 1.0f}); // Code files - green (index 4)
            get_color(5, {220.0f/255.0f, 220.0f/255.0f, 120.0f/255.0f, 1.0f}); // Text files - yellow (index 5)
            get_color(6, {220.0f/255.0f, 120.0f/255.0f, 220.0f/255.0f, 1.0f}); // Image files - purple (index 6)
            get_color(7, {255.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 1.0f}); // Executable files - red (index 7)
            get_color(8, {200.0f/255.0f, 200.0f/255.0f, 200.0f/255.0f, 1.0f}); // Other files - light gray (index 8)
            get_color(9, {120.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f}); // Symlinks - cyan (index 9)
            
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
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[2])); // Parent directory color
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
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[3])); // Directories
                        } else if (entry.is_regular_file()) {
                            // Check file extension for common types
                            std::string ext = entry.path().extension().string();
                            if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cc") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4])); // Code files
                            } else if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".yaml" || ext == ".yml") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[5])); // Text files
                            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[6])); // Image files
                            } else if (ext == ".exe" || ext == "" || ext == ".bin" || ext == ".sh") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[7])); // Executable files
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[8])); // Other files
                            }
                        } else if (entry.is_symlink()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[9])); // Symlinks
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[8])); // Other types
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