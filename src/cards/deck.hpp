#pragma once

#include <imgui/imgui.h>
#include <vector>
#include <utility>

#include "git.hpp"

struct deck {
    deck(SDL_Renderer* renderer) : git_(renderer) {}

    void render(card &c) {
        // make the background color beige
        ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);
        // make the foreground color black
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        // Arrays of ImGui style elements for each color
        const ImGuiCol_ first_color_elements[] = {
            ImGuiCol_TitleBg,
            ImGuiCol_TitleBgActive,
            ImGuiCol_Border,
            ImGuiCol_BorderShadow,
            ImGuiCol_ButtonHovered,
        };
        
        const ImGuiCol_ second_color_elements[] = {
            ImGuiCol_TitleBgCollapsed,
            ImGuiCol_Button,
            ImGuiCol_ButtonActive,
            ImGuiCol_FrameBg,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_FrameBgActive,
            ImGuiCol_ResizeGrip,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_CheckMark,
            ImGuiCol_SliderGrab,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_Separator,
            ImGuiCol_SeparatorHovered,
            ImGuiCol_SeparatorActive,
            ImGuiCol_Tab,
            ImGuiCol_TabHovered,
            ImGuiCol_TabActive,
            ImGuiCol_TabUnfocused,
            ImGuiCol_TabUnfocusedActive,
            ImGuiCol_MenuBarBg,
            ImGuiCol_PopupBg
        };
        
        // Push first color elements
        for (const auto& col : first_color_elements) {
            ImGui::PushStyleColor(col, c.first_color);
        }
        
        // Push second color elements
        for (const auto& col : second_color_elements) {
            ImGui::PushStyleColor(col, c.second_color);
        }
        
        c.render();
        
        // Pop all style colors (2 initial + size of both arrays)
        const int total_style_pushes = 2 + std::size(first_color_elements) + std::size(second_color_elements);
        ImGui::PopStyleColor(total_style_pushes);
    }
    
    void render() {
        render(git_);
    }
private:
    git git_;
    ImVec4 background_color {0.96f, 0.96f, 0.86f, 0.40f};
};