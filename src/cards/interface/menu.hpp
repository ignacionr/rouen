#pragma once

#include <format>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "card.hpp"
#include <SDL2/SDL.h>
#include <imgui/imgui.h>
#include "../../registrar.hpp"

namespace rouen::cards {
    struct menu: public card {
        menu() {
            colors[0] = {0.37f, 0.53f, 0.71f, 1.0f}; // Change from orange to blue accent color
            colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
            name("Menu");
        }
        
        // Helper function to convert string to lowercase for case-insensitive comparison
        static std::string to_lower(const std::string& str) {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(), 
                           [](unsigned char c) { return std::tolower(c); });
            return result;
        }
        
        // Function to check if haystack contains needle (case-insensitive)
        static bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
            return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
        }
        
        bool render() override {
            return render_window([this]() {
                static std::vector<std::pair<std::string, std::function<void()>>> menu_items = {
                    {"Git", []() { "create_card"_sfn("git"); }},
                    {"Root Directory", []() { "create_card"_sfn("dir:/"); }},
                    {"Home Directory", []() { "create_card"_sfn("dir:/home"); }},
                    {"Pomodoro", []() { "create_card"_sfn("pomodoro"); }},
                    {"Grok", []() { "create_card"_sfn("grok"); }},
                    {"Radio", []() { "create_card"_sfn("radio"); }},
                    {"Podcasts and News", []() { "create_card"_sfn("rss"); }},
                    {"System Info", []() { "create_card"_sfn("sysinfo"); }},
                    {"Environment Variables", []() { "create_card"_sfn("envvars"); }},
                    {"Exit", []() { auto was_exiting = "exit"_fnb(); }}
                };
                
                // Search filter implementation
                static char search_buffer[256] = "";
                static int selected_index = 0;
                bool enter_pressed = false;
                
                // Command palette style input box at the top
                ImGui::PushItemWidth(-1);
                if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)) {
                    ImGui::SetKeyboardFocusHere(0);
                }
                
                // Input with placeholder text
                ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
                if (ImGui::InputText("##search", search_buffer, IM_ARRAYSIZE(search_buffer), input_flags)) {
                    enter_pressed = true;
                }
                
                // Show placeholder when empty
                if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                    auto pos = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(pos.x + 5, pos.y + 5), 
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), 
                        "Search commands... (Type to filter)"
                    );
                }
                ImGui::PopItemWidth();
                
                ImGui::Separator();
                
                // Filter items based on search text
                std::string search_text = search_buffer;
                
                // Create a vector of filtered items with their indices
                std::vector<std::pair<int, std::string>> filtered_items;
                for (size_t i = 0; i < menu_items.size(); i++) {
                    const auto& item = menu_items[i];
                    if (search_text.empty() || contains_case_insensitive(item.first, search_text)) {
                        filtered_items.push_back({i, item.first});
                    }
                }
                
                // Handle keyboard navigation
                if (ImGui::IsWindowFocused()) {
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !filtered_items.empty()) {
                        selected_index = (selected_index + 1) % filtered_items.size();
                    }
                    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !filtered_items.empty()) {
                        selected_index = (selected_index + filtered_items.size() - 1) % filtered_items.size();
                    }
                    
                    // Execute the selected command on Enter key
                    if (enter_pressed && !filtered_items.empty()) {
                        if (selected_index >= 0 && selected_index < filtered_items.size()) {
                            int original_index = filtered_items[selected_index].first;
                            menu_items[original_index].second();
                        }
                    }
                }
                
                // If no items match the filter, show a message
                if (filtered_items.empty() && !search_text.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No commands match your filter");
                } 
                else {
                    // Render filtered items
                    for (size_t i = 0; i < filtered_items.size(); i++) {
                        int original_index = filtered_items[i].first;
                        const std::string& item_text = filtered_items[i].second;
                        
                        // Set selection color for the currently selected item
                        bool is_selected = (i == selected_index);
                        if (is_selected) {
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.6f, 0.8f, 0.4f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 0.8f, 0.5f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.6f, 0.8f, 0.6f));
                        }
                        
                        if (ImGui::Selectable(item_text.c_str(), is_selected)) {
                            menu_items[original_index].second();
                        }
                        
                        // Make the selected item visible - auto-scroll to it
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                            ImGui::PopStyleColor(3);
                        }
                    }
                }
            });
        }
    };
}