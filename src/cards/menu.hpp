#pragma once

#include <format>
#include <functional>
#include <string>

#include "card.hpp"
#include <SDL2/SDL.h>
#include <imgui/imgui.h>
#include "../registrar.hpp"

namespace rouen::cards {
    struct menu: public card {
        menu() {
            first_color = {1.0f, 0.341f, 0.133f, 1.0f}; // git Orange
            second_color = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
        }
        
        bool render() override {
            bool is_open = true;
            if (ImGui::Begin(std::format("Menu##{}", static_cast<void*>(this)).c_str(), &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
                is_open &= run_focused_handlers();
                static std::vector<std::pair<std::string, std::function<void()>>> menu_items = {
                    {"Git", []() { "create_card"_sfn("git"); }},
                    {"Settings", []() { /* Call settings card */ }},
                    {"Exit", []() { /* Call exit function */ }}
                };
                for (const auto& [title,action] : menu_items) {
                    if (ImGui::Selectable(title.c_str())) {
                        action();
                    }
                }
            }
            ImGui::End();
            return is_open;
        }
    };
}