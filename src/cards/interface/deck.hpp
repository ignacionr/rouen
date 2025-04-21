#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <algorithm> // Added for std::find_if
#include <imgui/imgui.h>

#include "factory.hpp"
#include "../productivity/editor.hpp"
#include "../../registrar.hpp"
#include "../../helpers/deferred_operations.hpp"

struct deck {
    deck(SDL_Renderer* renderer): renderer(renderer), editor_() {
        // Initialize colors
        background_color = {0.96f, 0.96f, 0.86f, 0.40f};
        editor_background_color = {0.76f, 0.76f, 0.66f, 0.40f};
        text_color = {0.0f, 0.0f, 0.0f, 1.0f}; // Black text
        
        // Register to present new cards
        registrar::add<std::function<void(std::string const&)>>(
            "create_card", 
            std::make_shared<std::function<void(std::string const&)>>(
                [this](std::string const& uri) { create_card(uri); }
            )
        );
        create_card("menu", true);
    }

    void create_card(std::string_view uri, bool move_first = false) {
        // this needs to be deferred
        auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
        deferred_ops->queue([this, uri = std::string{uri}, move_first] {
            create_card_impl(uri, move_first);
        });
    }

    void create_card_impl(std::string_view uri, bool move_first = false) {
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
        bool result {true};
        if ((x < screen_size.x && x + c.width > 0.0f) || c.grab_focus) {
            if (c.grab_focus) {
                c.grab_focus = false;
                ImGui::SetNextWindowFocus();
            }
            result = c.render();
            requested_fps = std::max(requested_fps, c.requested_fps);
        }
        
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