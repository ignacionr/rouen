#pragma once

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"
#include <SDL.h>

// 3. All other includes
#include "../../helpers/string_helper.hpp"
#include "../../registrar.hpp"
#include "card.hpp"

namespace rouen::cards {
    struct menu: public card {
        menu() {
            colors[0] = {0.37f, 0.53f, 0.71f, 1.0f}; // Blue accent color for primary elements
            colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise for secondary elements
            
            // Additional colors for menu selection
            get_color(2, ImVec4(0.2f, 0.4f, 0.8f, 0.6f)); // Menu item selection background
            get_color(3, ImVec4(0.3f, 0.5f, 0.9f, 0.6f)); // Menu item hover background
            get_color(4, ImVec4(0.15f, 0.35f, 0.75f, 0.6f)); // Menu item active background
            get_color(5, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // Menu item text
            
            name("Application Menu");
            width = 320.0f; // Slightly wider for better menu display
        }

        std::string get_uri() const override {
            return "menu";
        }
        
        bool render() override {
            return render_window([this]() {
                // Define menu categories
                struct MenuCategory {
                    std::string name;
                    std::vector<std::pair<std::string, std::function<void()>>> items;
                };
                
                static std::vector<MenuCategory> menu_categories = {
                    { "Development", {
                        {"Git", []() { "create_card"_sfn("git"); }},
                        {"GitHub", []() { "create_card"_sfn("github"); }},
                        {"CMake", []() { "create_card"_sfn("cmake:" + std::filesystem::current_path().string() + "/CMakeLists.txt"); }},
                        {"Root Directory", []() { "create_card"_sfn("dir:/"); }},
                        {"Home Directory", []() { "create_card"_sfn("dir:/home"); }}
                    }},
                    { "Productivity", {
                        {"Pomodoro", []() { "create_card"_sfn("pomodoro"); }},
                        {"Alarm", []() { "create_card"_sfn("alarm"); }},
                        {"Jira", []() { "create_card"_sfn("jira"); }},
                        {"Jira Projects", []() { "create_card"_sfn("jira-projects"); }},
                        {"Jira Search", []() { "create_card"_sfn("jira-search"); }},
                    }},
                    { "Information", {
                        {"Calendar", []() { "create_card"_sfn("calendar"); }},
                        {"Grok AI Chat", []() { "create_card"_sfn("grok"); }},
                        {"Podcasts and News", []() { "create_card"_sfn("rss"); }},
                        {"Travel Plans", []() { "create_card"_sfn("travel"); }},
                        {"Weather & Time", []() { "create_card"_sfn("weather"); }},
                        {"Email", []() { "create_card"_sfn("mail"); }}
                    }},
                    { "Media", {
                        {"Radio", []() { "create_card"_sfn("radio"); }},
                        {"Chess Replay", []() { "create_card"_sfn("chess"); }}
                    }},
                    { "System", {
                        {"System Info", []() { "create_card"_sfn("sysinfo"); }},
                        {"Terminal", []() { "create_card"_sfn("terminal"); }},
                        {"Environment Variables", []() { "create_card"_sfn("envvars"); }},
                        {"Subnet Scanner", []() { "create_card"_sfn("subnet-scanner"); }},
                        {"Database Repair", []() { "create_card"_sfn("dbrepair"); }},
                        {"Exit Application", []() { [[maybe_unused]] bool was_exiting = "exit"_fnb(); }} // Fixed: Use [[maybe_unused]] to suppress nodiscard warning
                    }}
                };
                
                // Flatten menu items for search
                static std::vector<std::tuple<int, int, std::string>> all_menu_items;
                if (all_menu_items.empty()) {
                    for (size_t cat_idx = 0; cat_idx < menu_categories.size(); cat_idx++) {
                        for (size_t item_idx = 0; item_idx < menu_categories[cat_idx].items.size(); item_idx++) {
                            all_menu_items.push_back(std::make_tuple(
                                cat_idx, item_idx, menu_categories[cat_idx].items[item_idx].first
                            ));
                        }
                    }
                }
                
                // Search filter implementation
                static char search_buffer[256] = "";
                static int selected_index = 0;
                bool enter_pressed = false;
                
                // Command palette style input box at the top
                ImGui::TextColored(colors[5], "%s", "Launch Application");
                ImGui::Separator();
                
                // Search box with icon
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.6f));
                
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
                        ImVec2(pos.x + 5, pos.y + 2), 
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), 
                        "Search applications... (Type to filter)"
                    );
                }
                ImGui::PopItemWidth();
                ImGui::PopStyleColor(); // Pop FrameBg
                
                ImGui::Separator();
                
                // Filter items based on search text
                std::string search_text = search_buffer;
                
                // Create a vector of filtered items
                std::vector<std::tuple<int, int, std::string>> filtered_items;
                if (search_text.empty()) {
                    // Display categorized menu when no search text
                    ImGui::BeginChild("MenuCategories", ImVec2(0, 0), false);
                    
                    for (size_t cat_idx = 0; cat_idx < menu_categories.size(); cat_idx++) {
                        const auto& category = menu_categories[cat_idx];
                        
                        // Category header
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
                        ImGui::TextUnformatted(category.name.c_str());
                        ImGui::PopStyleColor();
                        ImGui::Separator();
                        
                        // Category items
                        for (size_t item_idx = 0; item_idx < category.items.size(); item_idx++) {
                            const auto& item = category.items[item_idx];
                            
                            ImGui::PushStyleColor(ImGuiCol_Header, colors[2]);
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors[3]);
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors[4]);
                            
                            if (ImGui::Selectable(item.first.c_str(), false)) {
                                item.second();
                            }
                            
                            ImGui::PopStyleColor(3);
                        }
                        
                        // Add spacing between categories
                        ImGui::Spacing();
                        ImGui::Spacing();
                    }
                    
                    ImGui::EndChild();
                } else {
                    // Filter and display search results
                    for (const auto& [cat_idx, item_idx, item_text] : all_menu_items) {
                        if (::helpers::StringHelper::contains_case_insensitive(item_text, search_text)) {
                            filtered_items.push_back(std::make_tuple(cat_idx, item_idx, item_text));
                        }
                    }
                    
                    // Handle keyboard navigation
                    if (ImGui::IsWindowFocused()) {
                        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !filtered_items.empty()) {
                            selected_index = static_cast<int>((static_cast<size_t>(selected_index) + 1) % filtered_items.size());
                        }
                        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !filtered_items.empty()) {
                            selected_index = static_cast<int>((static_cast<size_t>(selected_index) + filtered_items.size() - 1) % filtered_items.size());
                        }
                        
                        // Execute the selected command on Enter key
                        if (enter_pressed && !filtered_items.empty()) {
                            if (selected_index >= 0 && static_cast<size_t>(selected_index) < filtered_items.size()) {
                                auto [cat_idx, item_idx, _] = filtered_items[static_cast<size_t>(selected_index)];
                                menu_categories[static_cast<size_t>(cat_idx)].items[static_cast<size_t>(item_idx)].second();
                                // also clear the filter
                                search_buffer[0] = '\0';
                            }
                        }
                    }
                    
                    // Display search results
                    ImGui::BeginChild("SearchResults", ImVec2(0, 0), false);
                    
                    // If no items match the filter, show a message
                    if (filtered_items.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", "No applications match your search");
                    } 
                    else {
                        // Render filtered items
                        for (size_t i = 0; i < filtered_items.size(); i++) {
                            auto [cat_idx, item_idx, item_text] = filtered_items[i];
                            
                            // Set selection color for the currently selected item
                            bool is_selected = (static_cast<int>(i) == selected_index);
                            
                            ImGui::PushStyleColor(ImGuiCol_Header, colors[2]);
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors[3]);
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors[4]);
                            
                            if (ImGui::Selectable(item_text.c_str(), is_selected)) {
                                menu_categories[static_cast<size_t>(cat_idx)].items[static_cast<size_t>(item_idx)].second();
                            }
                            
                            // Make the selected item visible - auto-scroll to it
                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                            
                            ImGui::PopStyleColor(3);
                        }
                    }
                    
                    ImGui::EndChild();
                }
            });
        }
    };
}
