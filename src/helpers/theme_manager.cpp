#include "theme_manager.hpp"
#include "../cards/interface/card.hpp"
#include "platform_utils.hpp"
#include <fstream>
#include <iostream>

namespace rouen::theme {

    namespace {
        struct theme_save_model {
            size_t active_index;
            std::vector<theme_palette> custom_themes;
            struct glaze {
                using T = theme_save_model;
                static constexpr auto value = glz::object(
                    "active_index", &T::active_index,
                    "custom_themes", &T::custom_themes
                );
            };
        };
    }

    theme_manager& theme_manager::get() {
        static theme_manager instance;
        return instance;
    }

    theme_manager::theme_manager() {
        setup_default_themes();
        load_themes();
    }

    void theme_manager::setup_default_themes() {
        themes_.clear();

        // 1. Classic Dark (Slate Blue)
        theme_palette dark;
        dark.name = "Classic Dark";
        dark.window_bg = {0.10f, 0.10f, 0.12f, 1.00f};
        dark.text = {0.90f, 0.90f, 0.92f, 1.00f};
        dark.text_disabled = {0.60f, 0.60f, 0.60f, 1.00f};
        dark.title_bg = {0.13f, 0.14f, 0.18f, 1.00f};
        dark.title_bg_active = {0.16f, 0.18f, 0.28f, 1.00f};
        dark.menu_bar_bg = {0.14f, 0.14f, 0.16f, 1.00f};
        dark.button = {0.23f, 0.35f, 0.45f, 1.00f};
        dark.button_hovered = {0.28f, 0.45f, 0.60f, 1.00f};
        dark.button_active = {0.33f, 0.55f, 0.70f, 1.00f};
        dark.frame_bg = {0.17f, 0.18f, 0.22f, 1.00f};
        dark.frame_bg_hovered = {0.24f, 0.26f, 0.31f, 1.00f};
        dark.frame_bg_active = {0.30f, 0.32f, 0.38f, 1.00f};
        dark.check_mark = {0.37f, 0.53f, 0.71f, 1.00f};
        dark.slider_grab = {0.37f, 0.53f, 0.71f, 1.00f};

        // Standard 16 card colors for Classic Dark
        dark.card_colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};  // 0: Accent (Slate Blue)
        dark.card_colors[1] = {0.25f, 0.88f, 0.82f, 0.7f};  // 1: Secondary (Turquoise)
        dark.card_colors[2] = {0.88f, 0.42f, 0.46f, 1.0f};  // 2: Error (Red)
        dark.card_colors[3] = {0.60f, 0.76f, 0.47f, 1.0f};  // 3: Success (Green)
        dark.card_colors[4] = {0.82f, 0.60f, 0.40f, 1.0f};  // 4: Warning (Yellow/Orange)
        dark.card_colors[5] = {0.38f, 0.69f, 0.94f, 1.0f};  // 5: Info (Light Blue)
        dark.card_colors[6] = {0.78f, 0.47f, 0.87f, 1.0f};  // 6: Special 1 (Purple)
        dark.card_colors[7] = {0.95f, 0.45f, 0.65f, 1.0f};  // 7: Special 2 (Pink)
        dark.card_colors[8] = {0.90f, 0.60f, 0.30f, 1.0f};  // 8: Special 3 (Orange)
        dark.card_colors[9] = {0.67f, 0.70f, 0.75f, 1.0f};  // 9: Special 4 (Light Gray)
        for (size_t i = 10; i < 16; ++i) {
            dark.card_colors[i] = {0.50f, 0.50f, 0.50f, 1.0f}; // Fallbacks
        }
        themes_.push_back(dark);

        // 2. Amber Transistor
        theme_palette amber;
        amber.name = "Amber Transistor";
        amber.window_bg = {0.07f, 0.07f, 0.06f, 1.00f};
        amber.text = {0.91f, 0.77f, 0.62f, 1.00f};
        amber.text_disabled = {0.55f, 0.45f, 0.36f, 1.00f};
        amber.title_bg = {0.11f, 0.10f, 0.09f, 1.00f};
        amber.title_bg_active = {0.21f, 0.16f, 0.13f, 1.00f};
        amber.menu_bar_bg = {0.10f, 0.09f, 0.08f, 1.00f};
        amber.button = {0.55f, 0.27f, 0.07f, 1.00f};
        amber.button_hovered = {0.80f, 0.52f, 0.25f, 1.00f};
        amber.button_active = {0.82f, 0.41f, 0.12f, 1.00f};
        amber.frame_bg = {0.14f, 0.12f, 0.11f, 1.00f};
        amber.frame_bg_hovered = {0.18f, 0.16f, 0.15f, 1.00f};
        amber.frame_bg_active = {0.24f, 0.21f, 0.19f, 1.00f};
        amber.check_mark = {1.00f, 0.69f, 0.00f, 1.00f};
        amber.slider_grab = {1.00f, 0.69f, 0.00f, 1.00f};

        amber.card_colors[0] = {1.00f, 0.69f, 0.00f, 1.0f};  // 0: Accent (Amber Glow)
        amber.card_colors[1] = {0.82f, 0.49f, 0.17f, 0.7f};  // 1: Secondary (Brown/Copper)
        amber.card_colors[2] = {0.88f, 0.35f, 0.28f, 1.0f};  // 2: Error (Rusty Red)
        amber.card_colors[3] = {0.56f, 0.66f, 0.48f, 1.0f};  // 3: Success (Olive Green)
        amber.card_colors[4] = {1.00f, 0.80f, 0.00f, 1.0f};  // 4: Warning (Yellow)
        amber.card_colors[5] = {0.81f, 0.62f, 0.43f, 1.0f};  // 5: Info (Warm Ochre)
        amber.card_colors[6] = {0.64f, 0.56f, 0.44f, 1.0f};  // 6: Special 1 (Taupe)
        amber.card_colors[7] = {0.48f, 0.42f, 0.35f, 1.0f};  // 7: Special 2 (Dark Gray-Brown)
        amber.card_colors[8] = {0.71f, 0.55f, 0.36f, 1.0f};  // 8: Special 3 (Sand)
        amber.card_colors[9] = {0.87f, 0.77f, 0.64f, 1.0f};  // 9: Special 4 (Cream)
        for (size_t i = 10; i < 16; ++i) {
            amber.card_colors[i] = {0.45f, 0.38f, 0.32f, 1.0f};
        }
        themes_.push_back(amber);

        // 3. Cyberpunk Neon
        theme_palette cyberpunk;
        cyberpunk.name = "Cyberpunk Neon";
        cyberpunk.window_bg = {0.02f, 0.01f, 0.04f, 1.00f};
        cyberpunk.text = {0.00f, 1.00f, 0.80f, 1.00f};
        cyberpunk.text_disabled = {0.29f, 0.22f, 0.36f, 1.00f};
        cyberpunk.title_bg = {0.05f, 0.02f, 0.09f, 1.00f};
        cyberpunk.title_bg_active = {0.11f, 0.02f, 0.18f, 1.00f};
        cyberpunk.menu_bar_bg = {0.04f, 0.02f, 0.07f, 1.00f};
        cyberpunk.button = {0.50f, 0.00f, 0.50f, 1.00f};
        cyberpunk.button_hovered = {1.00f, 0.00f, 0.50f, 1.00f};
        cyberpunk.button_active = {1.00f, 0.00f, 1.00f, 1.00f};
        cyberpunk.frame_bg = {0.07f, 0.04f, 0.13f, 1.00f};
        cyberpunk.frame_bg_hovered = {0.11f, 0.05f, 0.20f, 1.00f};
        cyberpunk.frame_bg_active = {0.15f, 0.07f, 0.27f, 1.00f};
        cyberpunk.check_mark = {1.00f, 0.00f, 0.50f, 1.00f};
        cyberpunk.slider_grab = {1.00f, 0.00f, 0.50f, 1.00f};

        cyberpunk.card_colors[0] = {1.00f, 0.00f, 0.50f, 1.0f};  // 0: Accent (Neon Magenta)
        cyberpunk.card_colors[1] = {0.00f, 1.00f, 0.80f, 0.7f};  // 1: Secondary (Neon Cyan)
        cyberpunk.card_colors[2] = {1.00f, 0.20f, 0.20f, 1.0f};  // 2: Error (Hot Red)
        cyberpunk.card_colors[3] = {0.20f, 1.00f, 0.20f, 1.0f};  // 3: Success (Neon Green)
        cyberpunk.card_colors[4] = {1.00f, 1.00f, 0.20f, 1.0f};  // 4: Warning (Neon Yellow)
        cyberpunk.card_colors[5] = {0.20f, 0.80f, 1.00f, 1.0f};  // 5: Info (Bright Blue)
        cyberpunk.card_colors[6] = {0.80f, 0.20f, 1.00f, 1.0f};  // 6: Special 1 (Purple)
        cyberpunk.card_colors[7] = {1.00f, 0.60f, 0.20f, 1.0f};  // 7: Special 2 (Neon Orange)
        cyberpunk.card_colors[8] = {0.20f, 1.00f, 0.60f, 1.0f};  // 8: Special 3 (Neon Mint)
        cyberpunk.card_colors[9] = {0.64f, 0.63f, 0.66f, 1.0f};  // 9: Special 4 (Muted Violet)
        for (size_t i = 10; i < 16; ++i) {
            cyberpunk.card_colors[i] = {0.40f, 0.10f, 0.50f, 1.0f};
        }
        themes_.push_back(cyberpunk);

        // 4. Retro Light
        theme_palette light;
        light.name = "Retro Light";
        light.window_bg = {0.99f, 0.96f, 0.89f, 1.00f};
        light.text = {0.17f, 0.16f, 0.16f, 1.00f};
        light.text_disabled = {0.58f, 0.63f, 0.63f, 1.00f};
        light.title_bg = {0.93f, 0.91f, 0.84f, 1.00f};
        light.title_bg_active = {0.87f, 0.84f, 0.74f, 1.00f};
        light.menu_bar_bg = {0.96f, 0.93f, 0.85f, 1.00f};
        light.button = {0.83f, 0.78f, 0.67f, 1.00f};
        light.button_hovered = {0.52f, 0.60f, 0.00f, 1.00f};
        light.button_active = {0.35f, 0.43f, 0.46f, 1.00f};
        light.frame_bg = {0.93f, 0.91f, 0.84f, 1.00f};
        light.frame_bg_hovered = {0.89f, 0.86f, 0.75f, 1.00f};
        light.frame_bg_active = {0.85f, 0.81f, 0.65f, 1.00f};
        light.check_mark = {0.71f, 0.54f, 0.00f, 1.00f};
        light.slider_grab = {0.71f, 0.54f, 0.00f, 1.00f};

        light.card_colors[0] = {0.71f, 0.54f, 0.00f, 1.0f};  // 0: Accent (Vintage Amber/Gold)
        light.card_colors[1] = {0.35f, 0.43f, 0.46f, 0.7f};  // 1: Secondary (Slate Gray-Blue)
        light.card_colors[2] = {0.86f, 0.20f, 0.18f, 1.0f};  // 2: Error (Crimson Red)
        light.card_colors[3] = {0.52f, 0.60f, 0.00f, 1.0f};  // 3: Success (Moss Green)
        light.card_colors[4] = {0.80f, 0.29f, 0.09f, 1.0f};  // 4: Warning (Rust Orange)
        light.card_colors[5] = {0.15f, 0.55f, 0.82f, 1.0f};  // 5: Info (Vintage Blue)
        light.card_colors[6] = {0.83f, 0.21f, 0.51f, 1.0f};  // 6: Special 1 (Magenta)
        light.card_colors[7] = {0.42f, 0.44f, 0.77f, 1.0f};  // 7: Special 2 (Slate Violet)
        light.card_colors[8] = {0.16f, 0.63f, 0.60f, 1.0f};  // 8: Special 3 (Vintage Teal)
        light.card_colors[9] = {0.35f, 0.43f, 0.46f, 1.0f};  // 9: Special 4 (Slate Gray)
        for (size_t i = 10; i < 16; ++i) {
            light.card_colors[i] = {0.60f, 0.60f, 0.55f, 1.0f};
        }
        themes_.push_back(light);
    }

    void theme_manager::select_theme(size_t index) {
        if (index < themes_.size()) {
            active_theme_index_ = index;
            apply_theme_to_imgui();
            save_themes();
        }
    }

    void theme_manager::select_theme(const std::string& name) {
        for (size_t i = 0; i < themes_.size(); ++i) {
            if (themes_[i].name == name) {
                select_theme(i);
                return;
            }
        }
    }

    void theme_manager::save_or_update_theme(const theme_palette& theme) {
        bool found = false;
        for (size_t i = 0; i < themes_.size(); ++i) {
            if (themes_[i].name == theme.name) {
                themes_[i] = theme;
                if (active_theme_index_ == i) {
                    apply_theme_to_imgui();
                }
                found = true;
                break;
            }
        }
        if (!found) {
            themes_.push_back(theme);
            active_theme_index_ = themes_.size() - 1;
            apply_theme_to_imgui();
        }
        save_themes();
    }

    void theme_manager::delete_theme(size_t index) {
        // Prevent deleting built-in/default themes if we're left with just those
        if (index < 4) {
            std::cout << "[Theme] Cannot delete built-in theme." << '\n';
            return;
        }
        if (index < themes_.size()) {
            themes_.erase(themes_.begin() + static_cast<std::ptrdiff_t>(index));
            if (active_theme_index_ >= themes_.size()) {
                active_theme_index_ = themes_.size() - 1;
            }
            apply_theme_to_imgui();
            save_themes();
        }
    }

    void theme_manager::apply_theme_to_card(card* c) const {
        if (!c) return;
        const auto& active = get_active_theme();
        for (size_t i = 0; i < 16; ++i) {
            c->colors[i] = ImVec4(active.card_colors[i][0], active.card_colors[i][1], active.card_colors[i][2], active.card_colors[i][3]);
        }
    }

    void theme_manager::apply_theme_to_imgui() const {
        const auto& active = get_active_theme();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Apply theme window background
        colors[ImGuiCol_WindowBg] = ImVec4(active.window_bg[0], active.window_bg[1], active.window_bg[2], active.window_bg[3]);
        
        // Text colors
        colors[ImGuiCol_Text] = ImVec4(active.text[0], active.text[1], active.text[2], active.text[3]);
        colors[ImGuiCol_TextDisabled] = ImVec4(active.text_disabled[0], active.text_disabled[1], active.text_disabled[2], active.text_disabled[3]);
        
        // Title BG colors
        colors[ImGuiCol_TitleBg] = ImVec4(active.title_bg[0], active.title_bg[1], active.title_bg[2], active.title_bg[3]);
        colors[ImGuiCol_TitleBgActive] = ImVec4(active.title_bg_active[0], active.title_bg_active[1], active.title_bg_active[2], active.title_bg_active[3]);
        
        // Menu Bar BG
        colors[ImGuiCol_MenuBarBg] = ImVec4(active.menu_bar_bg[0], active.menu_bar_bg[1], active.menu_bar_bg[2], active.menu_bar_bg[3]);
        
        // Buttons
        colors[ImGuiCol_Button] = ImVec4(active.button[0], active.button[1], active.button[2], active.button[3]);
        colors[ImGuiCol_ButtonHovered] = ImVec4(active.button_hovered[0], active.button_hovered[1], active.button_hovered[2], active.button_hovered[3]);
        colors[ImGuiCol_ButtonActive] = ImVec4(active.button_active[0], active.button_active[1], active.button_active[2], active.button_active[3]);
        
        // Frames
        colors[ImGuiCol_FrameBg] = ImVec4(active.frame_bg[0], active.frame_bg[1], active.frame_bg[2], active.frame_bg[3]);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(active.frame_bg_hovered[0], active.frame_bg_hovered[1], active.frame_bg_hovered[2], active.frame_bg_hovered[3]);
        colors[ImGuiCol_FrameBgActive] = ImVec4(active.frame_bg_active[0], active.frame_bg_active[1], active.frame_bg_active[2], active.frame_bg_active[3]);
        
        // Checks & sliders
        colors[ImGuiCol_CheckMark] = ImVec4(active.check_mark[0], active.check_mark[1], active.check_mark[2], active.check_mark[3]);
        colors[ImGuiCol_SliderGrab] = ImVec4(active.slider_grab[0], active.slider_grab[1], active.slider_grab[2], active.slider_grab[3]);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(active.slider_grab[0] * 1.2f, active.slider_grab[1] * 1.2f, active.slider_grab[2] * 1.2f, active.slider_grab[3]);
        
        // Text selection bg
        colors[ImGuiCol_TextSelectedBg] = ImVec4(active.check_mark[0], active.check_mark[1], active.check_mark[2], 0.40f);
    }

    void theme_manager::load_themes() {
        try {
            auto path = rouen::platform::get_user_config_directory() / "themes.json";
            if (!std::filesystem::exists(path)) return;

            std::ifstream file(path);
            if (!file.is_open()) return;

            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            theme_save_model model;
            auto err = glz::read_json(model, content);
            if (err) {
                std::cerr << "[Theme] Failed to parse themes.json: " << glz::format_error(err, content) << '\n';
                return;
            }

            // Append custom themes to our default ones
            for (const auto& t : model.custom_themes) {
                // Check if theme name conflicts with built-in theme
                bool conflict = false;
                for (size_t i = 0; i < 4; ++i) {
                    if (themes_[i].name == t.name) {
                        conflict = true;
                        break;
                    }
                }
                if (!conflict) {
                    themes_.push_back(t);
                }
            }

            if (model.active_index < themes_.size()) {
                active_theme_index_ = model.active_index;
            }
        } 
        catch (const std::exception& e) {
            std::cerr << "[Theme] Exception loading themes: " << e.what() << '\n';
        }
    }

    void theme_manager::save_themes() const {
        try {
            auto path = rouen::platform::get_user_config_directory() / "themes.json";
            
            theme_save_model model;
            model.active_index = active_theme_index_;
            // Only serialize custom themes (index 4 and onwards)
            if (themes_.size() > 4) {
                model.custom_themes.assign(themes_.begin() + 4, themes_.end());
            }

            std::string buffer;
            auto err = glz::write_json(model, buffer);
            if (!err) {
                std::ofstream file(path);
                if (file.is_open()) {
                    file << buffer;
                }
            }
        } 
        catch (const std::exception& e) {
            std::cerr << "[Theme] Exception saving themes: " << e.what() << '\n';
        }
    }

} // namespace rouen::theme
