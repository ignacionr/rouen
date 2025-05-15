#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <regex>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"

#include "../interface/card.hpp"

namespace rouen::cards
{
    // Helper function to resolve environment variables in path
    inline std::string resolve_env_variables(std::string_view path_with_vars) {
        std::string result(path_with_vars);
        std::regex env_var_regex("\\$(\\w+)");
        
        // Find all environment variables in the path and replace them
        std::smatch match;
        std::string temp = result;
        while (std::regex_search(temp, match, env_var_regex)) {
            std::string var_name = match[1].str();
            std::string var_value = platform::get_env(var_name);
            
            // Replace the variable with its value
            size_t pos = result.find("$" + var_name);
            if (pos != std::string::npos) {
                result.replace(pos, var_name.length() + 1, var_value);
            }
            
            // Continue searching in the remaining string
            temp = match.suffix();
        }
        
        return result;
    }

    struct fs_directory : public card
    {
        fs_directory(std::string_view path) : path_{resolve_env_variables(path)}
        {
            // Base colors (already set in the vector)
            colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};  // Primary color - blue accent
            colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Secondary color - turquoise
            
            // Additional colors for file types
            get_color(2, {150.0f/255.0f, 150.0f/255.0f, 255.0f/255.0f, 1.0f}); // Parent directory - blue (index 2)
            get_color(3, {0.3f, 0.3f, 1.0f, 1.0f}); // Directories - light blue (index 3)
            get_color(4, {120.0f/255.0f, 220.0f/255.0f, 120.0f/255.0f, 1.0f}); // Code files - green (index 4)
            get_color(5, {220.0f/255.0f, 220.0f/255.0f, 120.0f/255.0f, 1.0f}); // Text files - yellow (index 5)
            get_color(6, {220.0f/255.0f, 120.0f/255.0f, 220.0f/255.0f, 1.0f}); // Image files - purple (index 6)
            get_color(7, {255.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 1.0f}); // Executable files - red (index 7)
            get_color(8, {200.0f/255.0f, 200.0f/255.0f, 200.0f/255.0f, 1.0f}); // Other files - light gray (index 8)
            get_color(9, {120.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f}); // Symlinks - cyan (index 9)
            
            // if the path is empty, reset it to the current directory
            if (path_.empty())
            {
                path_ = std::filesystem::current_path();
            }

            name(path_.string());
        }

        std::string get_uri() const override
        {
            return std::format("dir:{}", path_.string());
        }

        void receive_keystrokes()
        {
            if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_T) && ImGui::GetIO().KeyCtrl)
            {
                "create_card"_sfn(std::format("terminal:{}", path_.string()));
                // expunge the strokes
                [[maybe_unused]] auto r {"keystrokes"_fns()};
            }
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
                                    if (ImGui::GetIO().KeyShift) {
                                        // open as terminal
                                        "create_card"_sfn(std::format("terminal:{}", entry.path().string()));
                                    }
                                    else {
                                        // open in a new card
                                        "create_card"_sfn(std::format("dir:{}", entry.path().string()));
                                    }
                                } else {
                                    // open in the same card
                                    path_ = entry.path();
                                    name(path_.string());
                                    filter_.clear();
                                }
                            }
                            else {
                                // If Ctrl is pressed and it's a CMakeLists.txt file, open a cmake card
                                if (ImGui::GetIO().KeyCtrl && entry.path().filename() == "CMakeLists.txt") {
                                    "create_card"_sfn(std::format("cmake:{}", entry.path().string()));
                                } else {
                                    // For other files or normal clicking, use the default editor
                                    "edit"_sfn(entry.path().string());
                                }
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
