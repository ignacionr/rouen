#include "theme_manager.hpp"
#include "../cards/interface/card.hpp"
#include "platform_utils.hpp"
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <glaze/core/common.hpp>
#include <glaze/core/reflect.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <imgui.h>
#include <iostream>
#include <iterator>
#include <vector>

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

        // 1. Dark (Default)
        theme_palette dark;
        dark.name = "Dark";
        dark.draw_card_outline = true;
        dark.window_bg = {0.11f, 0.11f, 0.13f, 1.00f};
        dark.text = {0.95f, 0.96f, 0.98f, 1.00f};
        dark.text_disabled = {0.50f, 0.50f, 0.50f, 1.00f};
        dark.title_bg = {0.08f, 0.08f, 0.09f, 1.00f};
        dark.title_bg_active = {0.16f, 0.16f, 0.20f, 1.00f};
        dark.menu_bar_bg = {0.11f, 0.11f, 0.13f, 1.00f};
        dark.button = {0.20f, 0.22f, 0.27f, 1.00f};
        dark.button_hovered = {0.28f, 0.32f, 0.40f, 1.00f};
        dark.button_active = {0.35f, 0.40f, 0.50f, 1.00f};
        dark.frame_bg = {0.16f, 0.17f, 0.20f, 1.00f};
        dark.frame_bg_hovered = {0.22f, 0.24f, 0.29f, 1.00f};
        dark.frame_bg_active = {0.28f, 0.30f, 0.36f, 1.00f};
        dark.check_mark = {0.40f, 0.65f, 1.00f, 1.00f};
        dark.slider_grab = {0.40f, 0.65f, 1.00f, 1.00f};
        dark.card_colors[0] = {0.0f, 0.0f, 0.0f, 1.0f};
        dark.card_colors[1] = {0.0f, 0.0f, 0.0f, 0.5f};
        themes_.push_back(dark);

        // 2. Light
        theme_palette light;
        light.name = "Light";
        light.draw_card_outline = true;
        light.window_bg = {0.94f, 0.94f, 0.95f, 1.00f};
        light.text = {0.10f, 0.10f, 0.12f, 1.00f};
        light.text_disabled = {0.60f, 0.60f, 0.60f, 1.00f};
        light.title_bg = {0.85f, 0.85f, 0.88f, 1.00f};
        light.title_bg_active = {0.78f, 0.82f, 0.90f, 1.00f};
        light.menu_bar_bg = {0.90f, 0.90f, 0.92f, 1.00f};
        light.button = {0.82f, 0.84f, 0.88f, 1.00f};
        light.button_hovered = {0.72f, 0.76f, 0.84f, 1.00f};
        light.button_active = {0.62f, 0.68f, 0.78f, 1.00f};
        light.frame_bg = {0.86f, 0.87f, 0.90f, 1.00f};
        light.frame_bg_hovered = {0.78f, 0.80f, 0.85f, 1.00f};
        light.frame_bg_active = {0.70f, 0.73f, 0.80f, 1.00f};
        light.check_mark = {0.20f, 0.45f, 0.85f, 1.00f};
        light.slider_grab = {0.20f, 0.45f, 0.85f, 1.00f};
        light.card_colors[0] = {0.95f, 0.95f, 0.96f, 1.0f};
        light.card_colors[1] = {0.85f, 0.85f, 0.88f, 0.8f};
        themes_.push_back(light);

        // 3. Cyberpunk
        theme_palette cyberpunk;
        cyberpunk.name = "Cyberpunk";
        cyberpunk.draw_card_outline = true;
        cyberpunk.window_bg = {0.05f, 0.04f, 0.09f, 1.00f};
        cyberpunk.text = {0.00f, 0.98f, 0.93f, 1.00f}; // Neon Cyan
        cyberpunk.text_disabled = {0.40f, 0.20f, 0.45f, 1.00f};
        cyberpunk.title_bg = {0.18f, 0.00f, 0.24f, 1.00f};
        cyberpunk.title_bg_active = {0.35f, 0.00f, 0.45f, 1.00f};
        cyberpunk.menu_bar_bg = {0.10f, 0.02f, 0.15f, 1.00f};
        cyberpunk.button = {0.30f, 0.00f, 0.38f, 1.00f};
        cyberpunk.button_hovered = {0.95f, 0.00f, 0.50f, 1.00f}; // Neon Pink
        cyberpunk.button_active = {1.00f, 0.85f, 0.00f, 1.00f};  // Neon Yellow
        cyberpunk.frame_bg = {0.14f, 0.05f, 0.20f, 1.00f};
        cyberpunk.frame_bg_hovered = {0.25f, 0.08f, 0.32f, 1.00f};
        cyberpunk.frame_bg_active = {0.38f, 0.10f, 0.48f, 1.00f};
        cyberpunk.check_mark = {1.00f, 0.00f, 0.55f, 1.00f};
        cyberpunk.slider_grab = {0.00f, 0.98f, 0.93f, 1.00f};
        cyberpunk.card_colors[0] = {0.08f, 0.04f, 0.14f, 1.0f};
        cyberpunk.card_colors[1] = {0.25f, 0.00f, 0.35f, 0.6f};
        themes_.push_back(cyberpunk);

        // 4. Nord
        theme_palette nord;
        nord.name = "Nord";
        nord.draw_card_outline = true;
        nord.window_bg = {0.18f, 0.20f, 0.25f, 1.00f}; // nord0
        nord.text = {0.93f, 0.95f, 0.96f, 1.00f};      // nord6
        nord.text_disabled = {0.43f, 0.47f, 0.55f, 1.00f}; // nord3
        nord.title_bg = {0.15f, 0.17f, 0.21f, 1.00f};  // nord1
        nord.title_bg_active = {0.26f, 0.30f, 0.37f, 1.00f}; // nord2
        nord.menu_bar_bg = {0.18f, 0.20f, 0.25f, 1.00f};
        nord.button = {0.26f, 0.30f, 0.37f, 1.00f};    // nord2
        nord.button_hovered = {0.34f, 0.39f, 0.47f, 1.00f}; // nord3
        nord.button_active = {0.53f, 0.75f, 0.82f, 1.00f}; // nord8
        nord.frame_bg = {0.23f, 0.26f, 0.32f, 1.00f};  // nord1 variant
        nord.frame_bg_hovered = {0.30f, 0.34f, 0.42f, 1.00f};
        nord.frame_bg_active = {0.38f, 0.43f, 0.52f, 1.00f};
        nord.check_mark = {0.53f, 0.75f, 0.82f, 1.00f}; // nord8
        nord.slider_grab = {0.53f, 0.75f, 0.82f, 1.00f};
        nord.card_colors[0] = {0.18f, 0.20f, 0.25f, 1.0f};
        nord.card_colors[1] = {0.26f, 0.30f, 0.37f, 0.6f};
        themes_.push_back(nord);

        active_theme_index_ = 0;
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
        for (size_t i = 0; i < themes_.size(); ++i) {
            if (themes_[i].name == theme.name) {
                themes_[i] = theme;
                if (active_theme_index_ == i) {
                    apply_theme_to_imgui();
                }
                save_themes();
                return;
            }
        }

        themes_.push_back(theme);
        active_theme_index_ = themes_.size() - 1;
        apply_theme_to_imgui();
        save_themes();
    }

    void theme_manager::delete_theme(size_t index) {
        if (index >= themes_.size() || themes_.size() <= 1) {
            return;
        }

        themes_.erase(themes_.begin() + static_cast<ptrdiff_t>(index));

        if (active_theme_index_ >= themes_.size()) {
            active_theme_index_ = themes_.size() - 1;
        }

        apply_theme_to_imgui();
        save_themes();
    }

    void theme_manager::apply_theme_to_card(card* c) const {
        if (!c) return;
        const auto& active = get_active_theme();
        for (size_t i = 0; i < 16; ++i) {
            c->colors[i] = ImVec4(active.card_colors[i][0], active.card_colors[i][1], active.card_colors[i][2], active.card_colors[i][3]);
        }
    }

    void theme_manager::apply_theme_to_imgui() const {
        if (!ImGui::GetCurrentContext()) return;

        const auto& active = get_active_theme();
        ImGuiStyle& style = ImGui::GetStyle();

        auto to_imvec4 = [](const std::array<float, 4>& arr) {
            return ImVec4(arr[0], arr[1], arr[2], arr[3]);
        };

        style.Colors[ImGuiCol_WindowBg] = to_imvec4(active.window_bg);
        style.Colors[ImGuiCol_Text] = to_imvec4(active.text);
        style.Colors[ImGuiCol_TextDisabled] = to_imvec4(active.text_disabled);
        style.Colors[ImGuiCol_TitleBg] = to_imvec4(active.title_bg);
        style.Colors[ImGuiCol_TitleBgActive] = to_imvec4(active.title_bg_active);
        style.Colors[ImGuiCol_MenuBarBg] = to_imvec4(active.menu_bar_bg);
        style.Colors[ImGuiCol_Button] = to_imvec4(active.button);
        style.Colors[ImGuiCol_ButtonHovered] = to_imvec4(active.button_hovered);
        style.Colors[ImGuiCol_ButtonActive] = to_imvec4(active.button_active);
        style.Colors[ImGuiCol_FrameBg] = to_imvec4(active.frame_bg);
        style.Colors[ImGuiCol_FrameBgHovered] = to_imvec4(active.frame_bg_hovered);
        style.Colors[ImGuiCol_FrameBgActive] = to_imvec4(active.frame_bg_active);
        style.Colors[ImGuiCol_CheckMark] = to_imvec4(active.check_mark);
        style.Colors[ImGuiCol_SliderGrab] = to_imvec4(active.slider_grab);
        style.Colors[ImGuiCol_SliderGrabActive] = to_imvec4(active.button_active);
        style.Colors[ImGuiCol_Header] = to_imvec4(active.button);
        style.Colors[ImGuiCol_HeaderHovered] = to_imvec4(active.button_hovered);
        style.Colors[ImGuiCol_HeaderActive] = to_imvec4(active.button_active);

        if (active.draw_card_outline) {
            style.WindowBorderSize = 1.0f;
            style.Colors[ImGuiCol_Border] = ImVec4(active.button_hovered[0], active.button_hovered[1], active.button_hovered[2], 0.6f);
        } else {
            style.WindowBorderSize = 0.0f;
            style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
        }
    }

    void theme_manager::load_themes() {
        std::filesystem::path config_path = rouen::platform::get_user_data_path("themes.json");
        if (!std::filesystem::exists(config_path)) {
            return;
        }

        std::ifstream file(config_path);
        if (!file.is_open()) {
            return;
        }

        std::string json_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        theme_save_model model;
        auto ec = glz::read_json(model, json_content);
        if (!ec) {
            // Keep default themes and append loaded custom themes (or update existing)
            for (const auto& custom : model.custom_themes) {
                bool found = false;
                for (auto& existing : themes_) {
                    if (existing.name == custom.name) {
                        existing = custom;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    themes_.push_back(custom);
                }
            }

            if (model.active_index < themes_.size()) {
                active_theme_index_ = model.active_index;
            }
        }

        apply_theme_to_imgui();
    }

    void theme_manager::save_themes() const {
        std::filesystem::path config_path = rouen::platform::get_user_data_path("themes.json");

        theme_save_model model;
        model.active_index = active_theme_index_;
        model.custom_themes = themes_;

        std::string json_output;
        auto ec = glz::write_json(model, json_output);
        if (!ec) {
            std::ofstream file(config_path);
            if (file.is_open()) {
                file << json_output;
            }
        }
    }

} // namespace rouen::theme
