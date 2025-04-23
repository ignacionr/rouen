#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <algorithm> // Added for std::find_if
#include <imgui/imgui.h>
#include <fstream>   // Added for file I/O
#include <sstream>   // Added for string stream

#include "factory.hpp"
#include "../productivity/editor.hpp"
#include "../../registrar.hpp"
#include "../../helpers/deferred_operations.hpp"

struct deck {
    deck(SDL_Renderer* renderer): renderer(renderer), editor_() {
        // Initialize colors
        background_color = {0.0, 0.0f, 0.0f, 0.70f};
        editor_background_color = {0.76f, 0.76f, 0.66f, 0.40f};
        text_color = {1.0f, 1.0f, 1.0f, 1.0f};
        
        // Register to present new cards
        registrar::add<std::function<void(std::string const&)>>(
            "create_card", 
            std::make_shared<std::function<void(std::string const&)>>(
                [this](std::string const& uri) { create_card(uri); }
            )
        );
        
        // Load cards from ImGui configuration or create default menu card
        load_card_uris();
    }

    ~deck() {
        // Save card state when the deck is destroyed
        save_card_uris();
        
        // Unregister the create_card function
        registrar::remove<std::function<void(std::string const&)>>("create_card");
    }

    void create_card(std::string_view uri, bool move_first = false) {
        // this needs to be deferred
        auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
        deferred_ops->queue([this, uri = std::string{uri}, move_first] {
            create_card_impl(uri, move_first);
        });
    }

    void create_card_impl(std::string_view uri, bool move_first = false) {
        // do we have a card with this same uri already?
        auto existing_card = std::find_if(cards_.begin(), cards_.end(),
            [&uri](const auto& card) { return card->get_uri() == uri; });
        if (existing_card != cards_.end()) {
            // If the card already exists, we will make it request the focus
            (*existing_card)->grab_focus = true;
        }
        else {
            static auto card_factory {rouen::cards::factory()};
            auto card_ptr = card_factory.create_card(uri, renderer);
            if (card_ptr) {
                if (move_first) {
                    // Move the card to the front of the vector
                    cards_.insert(cards_.begin(), std::move(card_ptr));
                } else {
                    // Add the card to the end of the vector
                    cards_.emplace_back(std::move(card_ptr));
                }
            }
        }
    }

    bool render(card &c, float &x, float height, int &requested_fps) {
        // Arrays of ImGui style elements for each color
        const ImGuiCol_ first_color_elements[] = {
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
            ImGui::PushStyleColor(col, c.colors[0]);
        }
        
        // Push second color elements
        for (const auto& col : second_color_elements) {
            ImGui::PushStyleColor(col, c.colors[1]);
        }
        
        ImGui::SetNextWindowPos({x, 2.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({c.width, height}, ImGuiCond_Always);

        // does it overlap the screen?
        auto const screen_size {ImGui::GetMainViewport()->Size};
        if (c.grab_focus) {
            c.grab_focus = false;
            ImGui::SetNextWindowFocus();
        }
        bool result = c.render();
        requested_fps = std::max(requested_fps, c.requested_fps);
        
        // Pop all style colors (2 initial + size of both arrays)
        const int total_style_pushes = std::size(first_color_elements) + std::size(second_color_elements);
        ImGui::PopStyleColor(total_style_pushes);
        x += c.width + 2.0f;
        return result;
    }

    void handle_shortcuts() {
        if (ImGui::IsKeyPressed(ImGuiKey_P) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift) {
            // Handle Ctrl+Shift+P shortcut
            // Check if a menu card already exists
            auto menu_card = std::find_if(cards_.begin(), cards_.end(), 
                [](const auto& card) { 
                    // Check if the window title contains "Menu"
                    return card->window_title.find("Menu") != std::string::npos; 
                });
            
            if (menu_card != cards_.end()) {
                // Select existing menu card
                (*menu_card)->grab_focus = true;
            } else {
                // Create a new menu card if none exists
                create_card("menu", true);
            }
        }
    }
    
    // Save current cards to ImGui configuration
    void save_card_uris() {
        // Create a string with all card URIs separated by semicolons
        std::string uris;
        for (const auto& card : cards_) {
            if (!uris.empty()) {
                uris += ";";
            }
            uris += card->get_uri();
        }
        
        // Save to a custom section in imgui.ini through a file write
        // First read the file to preserve other settings
        std::ifstream ini_file("rouen.ini");
        std::stringstream buffer;
        bool found_rouen_section = false;
        bool in_rouen_section = false;

        if (ini_file) {
            std::string line;
            while (std::getline(ini_file, line)) {
                // Check for rouen section
                if (line == "[rouen]") {
                    found_rouen_section = true;
                    in_rouen_section = true;
                    buffer << line << std::endl;
                    buffer << "cards=" << uris << std::endl;
                    continue;
                }
                
                // Check for new section after rouen
                if (in_rouen_section && !line.empty() && line[0] == '[') {
                    in_rouen_section = false;
                }
                
                // Skip card entries in rouen section, keep everything else
                if (!in_rouen_section || line.substr(0, 6) != "cards=") {
                    buffer << line << std::endl;
                }
            }
            ini_file.close();
        }
        
        // If rouen section wasn't found, add it
        if (!found_rouen_section) {
            buffer << "[rouen]" << std::endl;
            buffer << "cards=" << uris << std::endl;
        }
        
        // Write back to the file
        std::ofstream out_file("rouen.ini");
        if (out_file) {
            out_file << buffer.str();
            out_file.close();
        }
    }
    
    // Load cards from ImGui configuration
    void load_card_uris() {
        // Read from imgui.ini file
        std::ifstream ini_file("rouen.ini");
        if (!ini_file) {
            // If file doesn't exist, create the default menu card
            create_card("menu", true);
            return;
        }
        
        std::string line;
        bool in_rouen_section = false;
        std::string uris;
        
        while (std::getline(ini_file, line)) {
            // Check for rouen section
            if (line == "[rouen]") {
                in_rouen_section = true;
                continue;
            }
            
            // Check for new section
            if (in_rouen_section && !line.empty() && line[0] == '[') {
                in_rouen_section = false;
                continue;
            }
            
            // Look for cards entry in rouen section
            if (in_rouen_section && line.substr(0, 6) == "cards=") {
                uris = line.substr(6);
                break;
            }
        }
        
        ini_file.close();
        
        // If no saved state, create the default menu card
        if (uris.empty()) {
            create_card("menu", true);
            return;
        }
        
        // Parse the string and create cards
        size_t pos = 0;
        while (pos < uris.length()) {
            size_t end = uris.find(';', pos);
            if (end == std::string::npos) {
                end = uris.length();
            }
            
            std::string uri = uris.substr(pos, end - pos);
            if (!uri.empty()) {
                create_card(uri);
            }
            
            pos = end + 1;
        }
    }
    
    struct render_status {
        int requested_fps {1};
    };

    [[nodiscard]] render_status render() {
        render_status result;
        handle_shortcuts();
        auto const size {ImGui::GetMainViewport()->Size};
        ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, background_color);
        ImGui::PushStyleColor(ImGuiCol_Text, text_color);
        auto x = 2.0f;
        auto ensure_visible_x {size.x};
        for (auto& c : cards_) {
            ensure_visible_x -= c->width + 2.0f;
            if (c->is_focused) {
                break;
            }
        }
        if (ensure_visible_x < 0.0f) {
            x = ensure_visible_x;
        }
        auto y = editor_.empty() ? 450.0f : 250.0f;
        auto cards_to_remove = std::remove_if(cards_.begin(), cards_.end(),
            [this, &x, y, &result](auto c) { return !render(*c, x, y, result.requested_fps); });
        cards_.erase(cards_to_remove, cards_.end());

        // Render the editor window
        ImGui::SetNextWindowPos({0.0f, 2.0f + y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({size.x, size.y - y}, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, editor_background_color);
        editor_.render();

        // Save card state when a card is added or removed
        static size_t last_card_count = 0;
        if (cards_.size() != last_card_count) {
            save_card_uris();
            last_card_count = cards_.size();
        }

        ImGui::PopStyleColor(4);
        return result;
    }

private:
    SDL_Renderer* renderer;
    std::vector<std::shared_ptr<card>> cards_;
    ImVec4 background_color;
    ImVec4 editor_background_color;
    ImVec4 text_color;
    editor editor_;
};