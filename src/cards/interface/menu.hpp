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
#include "../../helpers/ui_context.hpp"
#include <SDL3/SDL.h>

// 3. All other includes
#include "../../helpers/media_player.hpp"
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

        bool has_video_overlay() const override { return false; }
        void render_video_ui() override {}

        std::string get_uri() const override {
            return "menu";
        }
        
        bool render(rouen::ui::ui_context& ui) override {
            if (should_close_) {
                return false;
            }

            bool is_open = render_window([this, &ui]() {
                const auto& menu_categories = get_categories();
                
                // Flatten menu items for search
                std::vector<std::tuple<int, int, std::string>> all_menu_items;
                for (size_t cat_idx = 0; cat_idx < menu_categories.size(); cat_idx++) {
                    for (size_t item_idx = 0; item_idx < menu_categories[cat_idx].items.size(); item_idx++) {
                        all_menu_items.emplace_back(
                            static_cast<int>(cat_idx), static_cast<int>(item_idx), menu_categories[cat_idx].items[item_idx].first
                        );
                    }
                }
                
                // Search filter implementation
                static char search_buffer[256] = "";
                static int selected_index = 0;
                bool enter_pressed = false;
                
                // Command palette style input box at the top
                ui.text_colored(colors[5], "Launch Application");
                ui.separator();
                
                // Search box with icon
                ui.push_style_color(rouen::ui::style_color::frame_bg, ImVec4(0.15f, 0.15f, 0.2f, 0.6f));
                
                ui.push_item_width(-1);
                if (ui.is_window_focused() && !ui.is_any_item_active() && !ui.is_mouse_clicked(0)) {
                    ui.set_keyboard_focus_here(0);
                }
                
                // Input with placeholder text
                bool input_submitted = ui.input_text_with_placeholder("##search", search_buffer, static_cast<int>(sizeof(search_buffer)), "Search applications... (Type to filter)", true);
                
                bool cmd_enter_pressed = false;
                if (input_submitted) {
                    if (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl) {
                        cmd_enter_pressed = true;
                    } else {
                        enter_pressed = true;
                    }
                }
                
                if (cmd_enter_pressed) {
                    std::string query = search_buffer;
                    if (!query.empty()) {
                        std::string encoded_query = ::helpers::StringHelper::url_encode(query);
                        "create_card"_sfn("ai-chat:" + encoded_query);
                        search_buffer[0] = '\0';
                        selected_index = 0;
                        should_close_ = true;
                    }
                }
                
                ui.pop_item_width();
                ui.pop_style_color(); // Pop FrameBg
                
                ui.separator();
                
                // Filter items based on search text
                std::string search_text = search_buffer;
                
                // Create a vector of filtered items
                std::vector<std::tuple<int, int, std::string>> filtered_items;
                if (search_text.empty()) {
                    // Display categorized menu when no search text
                    ui.begin_child("MenuCategories", ImVec2(0, 0), false);
                    
                    for (const auto& category : menu_categories) {
                        
                        // Category header
                        ui.push_style_color(rouen::ui::style_color::text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
                        ui.text_unformatted(category.name);
                        ui.pop_style_color();
                        ui.separator();
                        
                        // Category items (horizontal layout with wrapping, avoiding horizontal scrolling)
                        float const window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().ItemSpacing.x;
                        for (size_t i = 0; i < category.items.size(); ++i) {
                            const auto& item = category.items[i];
                            
                            ui.push_style_color(rouen::ui::style_color::header, colors[2]);
                            ui.push_style_color(rouen::ui::style_color::header_hovered, colors[3]);
                            ui.push_style_color(rouen::ui::style_color::header_active, colors[4]);
                            
                            // Calculate item width
                            float item_width = ImGui::CalcTextSize(item.first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                            
                            if (i > 0) {
                                float next_x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + item_width;
                                if (next_x < window_visible_x2) {
                                    ImGui::SameLine();
                                }
                            }
                            
                            if (ImGui::Selectable(item.first.c_str(), false, 0, ImVec2(item_width, 0))) {
                                item.second();
                                should_close_ = true;
                            }
                            
                            ui.pop_style_color(3);
                        }
                        
                        // Add spacing between categories
                        ui.spacing();
                        ui.spacing();
                    }
                    
                    ui.end_child();
                } else {
                    // Filter and display search results
                    for (const auto& [cat_idx, item_idx, item_text] : all_menu_items) {
                        if (::helpers::StringHelper::contains_case_insensitive(item_text, search_text)) {
                            filtered_items.emplace_back(cat_idx, item_idx, item_text);
                        }
                    }
                    
                    // Handle keyboard navigation
                    if (ui.is_window_focused()) {
                        if (ui.is_key_pressed(rouen::ui::key::down_arrow) && !filtered_items.empty()) {
                            selected_index = static_cast<int>((static_cast<size_t>(selected_index) + 1) % filtered_items.size());
                        }
                        else if (ui.is_key_pressed(rouen::ui::key::up_arrow) && !filtered_items.empty()) {
                            selected_index = static_cast<int>((static_cast<size_t>(selected_index) + filtered_items.size() - 1) % filtered_items.size());
                        }
                        
                        // Execute the selected command on Enter key
                        if (enter_pressed && !filtered_items.empty()) {
                            if (selected_index >= 0 && static_cast<size_t>(selected_index) < filtered_items.size()) {
                                auto [cat_idx, item_idx, _] = filtered_items[static_cast<size_t>(selected_index)];
                                menu_categories[static_cast<size_t>(cat_idx)].items[static_cast<size_t>(item_idx)].second();
                                // also clear the filter
                                search_buffer[0] = '\0';
                                should_close_ = true;
                            }
                        }
                    }
                    
                    // Display search results
                    ui.begin_child("SearchResults", ImVec2(0, 0), false);
                    
                    // If no items match the filter, show a message
                    if (filtered_items.empty()) {
                        ui.text_colored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No applications match your search");
                    } 
                    else {
                        // Render filtered items
                        for (size_t i = 0; i < filtered_items.size(); i++) {
                            auto [cat_idx, item_idx, item_text] = filtered_items[i];
                            
                            // Set selection color for the currently selected item
                            bool is_selected = (static_cast<int>(i) == selected_index);
                            
                            ui.push_style_color(rouen::ui::style_color::header, colors[2]);
                            ui.push_style_color(rouen::ui::style_color::header_hovered, colors[3]);
                            ui.push_style_color(rouen::ui::style_color::header_active, colors[4]);
                            
                            if (ui.selectable(item_text, is_selected)) {
                                menu_categories[static_cast<size_t>(cat_idx)].items[static_cast<size_t>(item_idx)].second();
                                should_close_ = true;
                            }
                            
                            // Make the selected item visible - auto-scroll to it
                            if (is_selected) {
                                ui.set_item_default_focus();
                            }
                            
                            ui.pop_style_color(3);
                        }
                    }
                    
                    ui.end_child();
                }
            });
            return is_open && !should_close_;
        }

        // Override to provide MCP functions
        std::vector<mcp_function> get_mcp_functions() const override {
            return {
                mcp_function(
                    "get_menu_commands",
                    "List all available commands and applications in the menu card that can be executed.",
                    R"({"type": "object", "properties": {}})",
                    [](const std::string&) -> std::string {
                        std::string out = "{\n  \"commands\": [\n";
                        bool first = true;
                        for (const auto& cat : get_categories()) {
                            for (const auto& item : cat.items) {
                                if (!first) out += ",\n";
                                out += std::format("    {{\"name\": \"{}\", \"category\": \"{}\"}}", item.first, cat.name);
                                first = false;
                            }
                        }
                        out += "\n  ]\n}";
                        return out;
                    }
                ),
                mcp_function(
                    "execute_menu_command",
                    "Execute one of the commands/applications from the menu by its exact name.",
                    R"mcp({"type":"object","properties":{"name":{"type":"string","description":"The exact name of the command/application to execute (e.g. 'Git', 'Pomodoro', 'Calendar')"}},"required":["name"]})mcp",
                    [this](const std::string& params) -> std::string {
                        std::string target_name;
                        auto start = params.find("\"name\"");
                        if (start != std::string::npos) {
                            start = params.find(":", start);
                            if (start != std::string::npos) {
                                start = params.find("\"", start);
                                if (start != std::string::npos) {
                                    start++;
                                    auto end = params.find("\"", start);
                                    if (end != std::string::npos) {
                                        target_name = params.substr(start, end - start);
                                    }
                                }
                            }
                        }
                        
                        if (target_name.empty()) {
                            return R"({"status": "error", "message": "Missing 'name' parameter"})";
                        }
                        
                        for (const auto& cat : get_categories()) {
                            for (const auto& item : cat.items) {
                                if (::helpers::StringHelper::to_lower(item.first) == ::helpers::StringHelper::to_lower(target_name)) {
                                    item.second(); // Run the command function
                                    should_close_ = true;
                                    return std::format(R"({{"status": "success", "message": "Executed command '{}'"}})", item.first);
                                }
                            }
                        }
                        
                        return std::format(R"({{"status": "error", "message": "Command '{}' not found"}})", target_name);
                    }
                )
            };
        }

    private:
        mutable bool should_close_{false};

    public:
        struct MenuCategory {
            std::string name;
            std::vector<std::pair<std::string, std::function<void()>>> items;
        };

        static std::vector<MenuCategory> get_categories() {
            std::string detach_title = "Detach Currently Playing Media";
            auto active_item = media_player::get_currently_playing_item();
            if (active_item && !active_item->item_title.empty()) {
                detach_title = "Detach Playing Media (" + active_item->item_title + ")";
            }

            auto detach_action = []() {
                auto item = media_player::get_currently_playing_item();
                if (item) {
                    media_player::set_detached_item(item);
                    if (media_player::get_active_fullscreen_item() == item) {
                        media_player::clear_active_fullscreen_item();
                    }
                } else {
                    std::shared_ptr<media_player_item> fallback = nullptr;
                    {
                        std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
                        if (!media_player::items().empty()) {
                            fallback = media_player::items().begin()->second;
                        }
                    }
                    if (fallback) {
                        media_player::set_detached_item(fallback);
                    } else {
                        media_player::set_detached_mode_active(true);
                    }
                }
            };

            std::vector<MenuCategory> categories = {
                { "Development", {
                    {"Git", []() { "create_card"_sfn("git"); }},
                    {"GitHub", []() { "create_card"_sfn("github"); }},
                    {"CMake", []() { "create_card"_sfn("cmake:" + std::filesystem::current_path().string() + "/CMakeLists.txt"); }},
                    {"Root Directory", []() { "create_card"_sfn("dir:/"); }},
                    {"Home Directory", []() { "create_card"_sfn("dir:$HOME"); }}
                }},
                { "Productivity", {
                    {"Objectives", []() { "create_card"_sfn("objectives"); }},
                    {"Sovereign KPIs", []() { "create_card"_sfn("kpis"); }},
                    {"Pomodoro", []() { "create_card"_sfn("pomodoro"); }},
                    {"Alarm", []() { "create_card"_sfn("alarm"); }},
                    {"Unit Converter", []() { "create_card"_sfn("converter"); }},
                    {"Jira", []() { "create_card"_sfn("jira"); }},
                    {"Jira Projects", []() { "create_card"_sfn("jira-projects"); }},
                    {"Jira Search", []() { "create_card"_sfn("jira-search"); }},
                    {"Trello", []() { "create_card"_sfn("trello"); }},
                    {"Invoice Card", []() { "create_card"_sfn("invoice"); }},
                    {"Contacts Directory", []() { "create_card"_sfn("directory"); }},
                }},
                { "Information", {
                    {"Calendar", []() { "create_card"_sfn("calendar"); }},
                    {"AI Chat", []() { "create_card"_sfn("ai-chat"); }},
                    {"Podcasts and News", []() { "create_card"_sfn("rss"); }},
                    {"RSS Smart Lists", []() { "create_card"_sfn("rss-smart-list:Smart List"); }},
                    {"Travel Plans", []() { "create_card"_sfn("travel"); }},
                    {"Markdown Notes", []() { "create_card"_sfn("notes:"); }},
                    {"PDF Viewer", []() { "create_card"_sfn("pdf"); }},
                    {"Image Viewer", []() { "create_card"_sfn("image"); }},
                    {"Weather & Time", []() { "create_card"_sfn("weather"); }},
                    {"Wikipedia", []() { "create_card"_sfn("wikipedia"); }},
                    {"Email", []() { "create_card"_sfn("mail"); }},
                    {"WhatsApp", []() { "create_card"_sfn("whatsapp"); }},
                    {"Bybit Assets", []() { "create_card"_sfn("bybit-assets"); }},
                    {"Movies & Watchlists", []() { "create_card"_sfn("movies"); }},
                    {"Adaptive Cards", []() { "create_card"_sfn("adaptive-card"); }}
                }},
                { "Media", {
                    {detach_title, detach_action},
                    {"Media Companion", []() { "create_card"_sfn("media-companion"); }},
                    {"Live Camera", []() { "create_card"_sfn("camera"); }},
                    {"Ad-Lib Studio", []() { "create_card"_sfn("adlib"); }},
                    {"Radio", []() { "create_card"_sfn("radio"); }},
                    {"RadioCut Client", []() { "create_card"_sfn("radiocut"); }},
                    {"YouTube Search", []() { "create_card"_sfn("youtube"); }},
                    {"Chess Replay", []() { "create_card"_sfn("chess"); }}
                }},
                { "Debug", {
                    {"Adaptive Card", []() { "create_card"_sfn("adaptive-card"); }},
                    {"Number Series: Monthly Sales", []() { "create_card"_sfn("number-series:sales"); }},
                    {"Number Series: Temperature Forecast", []() { "create_card"_sfn("number-series:temps"); }},
                    {"Number Series: CPU Load", []() { "create_card"_sfn("number-series:cpu"); }}
                }},
                { "System", {
                    {"Display Settings", []() { "create_card"_sfn("display"); }},
                    {"Expand to Full Width", []() {
                        auto expand_svc = registrar::get<std::function<void()>>("expand_to_full_width");
                        if (expand_svc) {
                            (*expand_svc)();
                        }
                    }},
                    {"Video Feed & Cast Control", []() { "create_card"_sfn("cast-control"); }},
                    {"System Info", []() { "create_card"_sfn("sysinfo"); }},
                    {"Notifications", []() { "create_card"_sfn("notifications"); }},
                    {"Settings", []() { "create_card"_sfn("settings"); }},
                    {"Universal Sync", []() { "create_card"_sfn("sync"); }},
                    {"Theme Settings", []() { "create_card"_sfn("theme"); }},
                    {"Terminal", []() { "create_card"_sfn("terminal"); }},
                    {"Environment Variables", []() { "create_card"_sfn("envvars"); }},
                    {"Subnet Scanner", []() { "create_card"_sfn("subnet-scanner"); }},
                    {"Database Repair", []() { "create_card"_sfn("dbrepair"); }},
                    {"About", []() { "create_card"_sfn("about"); }},
                    {"Exit Application", []() { [[maybe_unused]] bool was_exiting = "exit"_fnb(); }}
                }}
            };
            return categories;
        }
    };
}
