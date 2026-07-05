#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <regex>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/config_service.hpp"

#include "../interface/card.hpp"

namespace rouen::cards
{
    // Helper function to resolve environment variables in path
    inline std::string resolve_env_variables(std::string_view path_with_vars) {
        std::string result(path_with_vars);
        std::regex env_var_regex("\\$(\\w+)");
        
        // Get centralized configuration service for consistent environment variable access
        auto config_service = helpers::ConfigService::instance();
        
        // Find all environment variables in the path and replace them
        std::smatch match;
        std::string temp = result;
        while (std::regex_search(temp, match, env_var_regex)) {
            std::string var_name = match[1].str();
            std::string var_value = config_service->get_env(var_name);
            
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
            // Additional colors for file types mapped to standard indices
            get_color(2, {255.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 1.0f}); // 2: Executable files - Red/Error
            get_color(3, {120.0f/255.0f, 220.0f/255.0f, 120.0f/255.0f, 1.0f}); // 3: Code files - Green/Success
            get_color(4, {220.0f/255.0f, 220.0f/255.0f, 120.0f/255.0f, 1.0f}); // 4: Text files - Yellow/Warning
            get_color(5, {150.0f/255.0f, 150.0f/255.0f, 255.0f/255.0f, 1.0f}); // 5: Parent & Directories - Blue/Info
            get_color(6, {220.0f/255.0f, 120.0f/255.0f, 220.0f/255.0f, 1.0f}); // 6: Image files - Purple/Special 1
            get_color(8, {120.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f}); // 8: Symlinks - Cyan/Special 3
            get_color(9, {200.0f/255.0f, 200.0f/255.0f, 200.0f/255.0f, 1.0f}); // 9: Other files - Light Gray/Special 4
            
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
                
                // Check if current directory is a git repository
                bool is_git_repo = std::filesystem::exists(path_ / ".git");
                
                if (is_git_repo) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.37f, 0.53f, 0.71f, 1.0f)); // Git blue color
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.47f, 0.63f, 0.81f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.43f, 0.61f, 1.0f));
                    
                    if (ImGui::Button("🔀 Open as Git Repo")) {
                        "create_card"_sfn(std::format("git:{}", path_.string()));
                    }
                    
                    ImGui::PopStyleColor(3);
                    ImGui::Separator();
                }
                
                // List files in the directory
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[5])); // Parent directory color (Blue/Info)
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
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[5])); // Directories (Blue/Info)
                        } else if (entry.is_regular_file()) {
                            // Check file extension for common types
                            std::string ext = entry.path().extension().string();
                            if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cc") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[3])); // Code files (Green/Success)
                            } else if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".yaml" || ext == ".yml") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4])); // Text files (Yellow/Warning)
                            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[6])); // Image files (Purple/Special 1)
                            } else if (ext == ".exe" || ext == "" || ext == ".bin" || ext == ".sh") {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[2])); // Executable files (Red/Error)
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[9])); // Other files (Light Gray/Special 4)
                            }
                        } else if (entry.is_symlink()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[8])); // Symlinks (Cyan/Special 3)
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[9])); // Other types (Light Gray)
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
