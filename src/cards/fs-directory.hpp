#pragma once

#include <filesystem>
#include <format>
#include <string>

#include <imgui/imgui.h>

#include "card.hpp"

namespace rouen::cards {
    struct fs_directory: public card {
        fs_directory(std::string_view path): path_{path} {
            first_color = {1.0f, 0.341f, 0.133f, 1.0f}; // git Orange
            second_color = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
            name(path);
        }
        
        bool render() override {
            return render_window([this]() {
                ImGui::Text("Directory: %s", path_.string().c_str());
                ImGui::Separator();
                
                // List files in the directory
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
                else for (const auto& entry : std::filesystem::directory_iterator(path_)) {
                    if (ImGui::Selectable(entry.path().filename().string().c_str())) {
                        if (entry.is_directory()) {
                            // if Ctrl is pressed, open on a different card
                            if (ImGui::GetIO().KeyCtrl) {
                                "create_card"_sfn(std::format("dir:{}", entry.path().string()));
                            } else {
                                // otherwise, open in the same card
                                path_ = entry.path();
                                name(path_.string());
                            }
                        }
                    }
                }
            });
        }
    private:
        std::filesystem::path path_;
    };
}