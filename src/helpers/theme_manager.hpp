#pragma once

#include <array>
#include <string>
#include <vector>
#include <memory>
#include "imgui_include.hpp"
#include "glaze_include.hpp"

// Forward declaration of card to avoid circular dependency
struct card;

namespace rouen::theme {

    struct theme_palette {
        std::string name;
        
        // ImGui core style colors
        std::array<float, 4> window_bg;
        std::array<float, 4> text;
        std::array<float, 4> text_disabled;
        std::array<float, 4> title_bg;
        std::array<float, 4> title_bg_active;
        std::array<float, 4> menu_bar_bg;
        std::array<float, 4> button;
        std::array<float, 4> button_hovered;
        std::array<float, 4> button_active;
        std::array<float, 4> frame_bg;
        std::array<float, 4> frame_bg_hovered;
        std::array<float, 4> frame_bg_active;
        std::array<float, 4> check_mark;
        std::array<float, 4> slider_grab;
        
        // Unified 16-color card-specific palette
        std::array<std::array<float, 4>, 16> card_colors;

        struct glaze {
            using T = theme_palette;
            static constexpr auto value = glz::object(
                "name", &T::name,
                "window_bg", &T::window_bg,
                "text", &T::text,
                "text_disabled", &T::text_disabled,
                "title_bg", &T::title_bg,
                "title_bg_active", &T::title_bg_active,
                "menu_bar_bg", &T::menu_bar_bg,
                "button", &T::button,
                "button_hovered", &T::button_hovered,
                "button_active", &T::button_active,
                "frame_bg", &T::frame_bg,
                "frame_bg_hovered", &T::frame_bg_hovered,
                "frame_bg_active", &T::frame_bg_active,
                "check_mark", &T::check_mark,
                "slider_grab", &T::slider_grab,
                "card_colors", &T::card_colors
            );
        };
    };

    class theme_manager {
    public:
        // Retrieve singleton instance
        static theme_manager& get();

        // Get list of all available themes
        const std::vector<theme_palette>& get_themes() const { return themes_; }
        std::vector<theme_palette>& get_themes() { return themes_; }

        // Get index of active theme
        size_t get_active_theme_index() const { return active_theme_index_; }

        // Get the active theme
        const theme_palette& get_active_theme() const { return themes_[active_theme_index_]; }
        theme_palette& get_active_theme() { return themes_[active_theme_index_]; }

        // Select theme by index
        void select_theme(size_t index);

        // Select theme by name
        void select_theme(const std::string& name);

        // Add a new theme or update an existing one
        void save_or_update_theme(const theme_palette& theme);

        // Delete a theme by index (cannot delete default themes if they are the only ones left)
        void delete_theme(size_t index);

        // Apply active theme colors to a card's local colors array
        void apply_theme_to_card(card* c) const;

        // Apply active theme colors to global ImGui style
        void apply_theme_to_imgui() const;

        // Load custom themes from user config directory
        void load_themes();

        // Save custom themes to user config directory
        void save_themes() const;

    private:
        theme_manager();
        ~theme_manager() = default;

        // Prevent copying
        theme_manager(const theme_manager&) = delete;
        theme_manager& operator=(const theme_manager&) = delete;

        // Setup default 4 themes
        void setup_default_themes();

        std::vector<theme_palette> themes_;
        size_t active_theme_index_ = 0;
    };

} // namespace rouen::theme
