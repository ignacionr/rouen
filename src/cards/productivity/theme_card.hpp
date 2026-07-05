#pragma once

#include "../interface/card.hpp"
#include "../../helpers/theme_manager.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <array>
#include <string>

namespace rouen::cards {

    class theme_card : public card {
    public:
        theme_card() {
            name("Theme Settings");
            width = 350.0f;
            new_theme_name_[0] = '\0';
        }

        std::string get_uri() const override {
            return "theme";
        }

        bool render() override {
            return render_window([this]() {
                auto& tm = rouen::theme::theme_manager::get();
                auto& themes = tm.get_themes();
                size_t active_idx = tm.get_active_theme_index();

                ImGui::TextColored(colors[0], ICON_MD_PALETTE " Color Themes");
                ImGui::Separator();
                ImGui::Spacing();

                // Theme Selector
                ImGui::TextUnformatted("Select Active Theme:");
                if (ImGui::BeginCombo("##theme_select", themes[active_idx].name.c_str())) {
                    for (size_t i = 0; i < themes.size(); ++i) {
                        bool is_selected = (active_idx == i);
                        if (ImGui::Selectable(themes[i].name.c_str(), is_selected)) {
                            tm.select_theme(i);
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::Spacing();

                // Custom theme deletion (only indexes >= 4 are custom/deletable)
                if (active_idx >= 4) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
                    if (ImGui::Button(ICON_MD_DELETE " Delete Current Custom Theme")) {
                        tm.delete_theme(active_idx);
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::Spacing();
                }

                ImGui::Separator();
                ImGui::Spacing();

                // Theme Editor Section
                ImGui::TextColored(colors[0], ICON_MD_EDIT " Edit Active Theme");
                ImGui::TextWrapped("Changes will apply immediately. You can save updates to this theme or create a new custom theme below.");
                ImGui::Spacing();

                // Create a temporary copy to edit
                auto active_theme = tm.get_active_theme();
                bool modified = false;

                if (ImGui::BeginTabBar("##theme_tabs")) {
                    if (ImGui::BeginTabItem("UI Elements")) {
                        ImGui::Spacing();
                        modified |= ImGui::ColorEdit4("Window BG", active_theme.window_bg.data());
                        modified |= ImGui::ColorEdit4("Text Color", active_theme.text.data());
                        modified |= ImGui::ColorEdit4("Text Disabled", active_theme.text_disabled.data());
                        modified |= ImGui::ColorEdit4("Title Bar BG", active_theme.title_bg.data());
                        modified |= ImGui::ColorEdit4("Title Active", active_theme.title_bg_active.data());
                        modified |= ImGui::ColorEdit4("Menu Bar BG", active_theme.menu_bar_bg.data());
                        modified |= ImGui::ColorEdit4("Button BG", active_theme.button.data());
                        modified |= ImGui::ColorEdit4("Button Hover", active_theme.button_hovered.data());
                        modified |= ImGui::ColorEdit4("Button Active", active_theme.button_active.data());
                        modified |= ImGui::ColorEdit4("Frame BG", active_theme.frame_bg.data());
                        modified |= ImGui::ColorEdit4("Frame Hover", active_theme.frame_bg_hovered.data());
                        modified |= ImGui::ColorEdit4("Frame Active", active_theme.frame_bg_active.data());
                        modified |= ImGui::ColorEdit4("Checkmark", active_theme.check_mark.data());
                        modified |= ImGui::ColorEdit4("Slider Grab", active_theme.slider_grab.data());
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Card Palette")) {
                        ImGui::Spacing();
                        ImGui::TextWrapped("These colors define card-specific UI accents, charts, and file lists.");
                        ImGui::Spacing();

                        modified |= ImGui::ColorEdit4("Primary Accent (0)", active_theme.card_colors[0].data());
                        modified |= ImGui::ColorEdit4("Secondary Accent (1)", active_theme.card_colors[1].data());
                        modified |= ImGui::ColorEdit4("Error / Red (2)", active_theme.card_colors[2].data());
                        modified |= ImGui::ColorEdit4("Success / Green (3)", active_theme.card_colors[3].data());
                        modified |= ImGui::ColorEdit4("Warning / Yellow (4)", active_theme.card_colors[4].data());
                        modified |= ImGui::ColorEdit4("Info / Blue (5)", active_theme.card_colors[5].data());
                        modified |= ImGui::ColorEdit4("Special 1 / Purple (6)", active_theme.card_colors[6].data());
                        modified |= ImGui::ColorEdit4("Special 2 / Pink (7)", active_theme.card_colors[7].data());
                        modified |= ImGui::ColorEdit4("Special 3 / Orange (8)", active_theme.card_colors[8].data());
                        modified |= ImGui::ColorEdit4("Special 4 / Gray (9)", active_theme.card_colors[9].data());
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                if (modified) {
                    // Update active theme in manager immediately
                    tm.save_or_update_theme(active_theme);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Save As New Custom Theme
                ImGui::TextColored(colors[0], ICON_MD_ADD " Create Custom Theme");
                ImGui::InputText("Theme Name", new_theme_name_, sizeof(new_theme_name_));
                
                ImGui::BeginDisabled(new_theme_name_[0] == '\0');
                if (ImGui::Button("Save Current Theme As New")) {
                    auto new_theme = active_theme;
                    new_theme.name = new_theme_name_;
                    tm.save_or_update_theme(new_theme);
                    new_theme_name_[0] = '\0'; // Clear input
                }
                ImGui::EndDisabled();
            });
        }

    private:
        char new_theme_name_[128];
    };

} // namespace rouen::cards
