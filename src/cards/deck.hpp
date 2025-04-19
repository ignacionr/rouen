#pragma once

#include <imgui/imgui.h>
#include <vector>
#include <utility>

#include "git.hpp"

struct deck {
    deck(SDL_Renderer* renderer){
        cards_.emplace_back(std::make_shared<git>(renderer));
    }

    bool render(card &c) {
        
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
            ImGuiCol_PopupBg,
            ImGuiCol_HeaderHovered,
            ImGuiCol_HeaderActive
        };
        
        // Push first color elements
        for (const auto& col : first_color_elements) {
            ImGui::PushStyleColor(col, c.first_color);
        }
        
        // Push second color elements
        for (const auto& col : second_color_elements) {
            ImGui::PushStyleColor(col, c.second_color);
        }
        
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(c.size, ImGuiCond_Always);
        auto result {c.render()};
        
        // Pop all style colors (2 initial + size of both arrays)
        const int total_style_pushes = std::size(first_color_elements) + std::size(second_color_elements);
        ImGui::PopStyleColor(total_style_pushes);
        return result;
    }
    
    void render() {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        auto cards_to_remove = std::remove_if(cards_.begin(), cards_.end(),
            [this](const std::shared_ptr<card>& c) { return !render(*c); });
        ImGui::PopStyleColor(2);
        cards_.erase(cards_to_remove, cards_.end());
    }

private:
    std::vector<std::shared_ptr<card>> cards_;
    ImVec4 background_color {0.96f, 0.96f, 0.86f, 0.40f};
};