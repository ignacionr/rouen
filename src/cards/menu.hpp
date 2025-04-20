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
            name("Menu");
        }
        
        bool render() override {
            return render_window([]() {
                static std::vector<std::pair<std::string, std::function<void()>>> menu_items = {
                    {"Git", []() { "create_card"_sfn("git"); }},
                    {"Root Directory", []() { "create_card"_sfn("dir:/"); }},
                    {"Home Directory", []() { "create_card"_sfn("dir:/home"); }},
                    {"Pomodoro", []() { "create_card"_sfn("pomodoro"); }},
                    {"Exit", []() { auto was_exiting = "exit"_fnb(); }}
                };
                for (const auto& [title,action] : menu_items) {
                    if (ImGui::Selectable(title.c_str())) {
                        action();
                    }
                }
            });
        }
    };
}