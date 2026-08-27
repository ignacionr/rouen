#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <string>

#include "../../external/IconsMaterialDesign.h"
#include "../../helpers/config_service.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/media_player_item.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

/**
 * @class display_card
 * @brief Controls workspace layout width multipliers and extended section parameters.
 *
 * The Display card allows users to select or fine-tune the global deck width factor multiplier (1x, 2x, 3x, 4x, 5x, 6x, etc.).
 * This multiplier controls the row capacity of the card deck relative to the OS window viewport size (size.x).
 *
 * Layout Behavior & Section Multipliers:
 * - 1x Mode: Row width equals 1.0x OS window width (single section per row).
 * - 2x/Nx Mode: Row max width equals N * size.x (N viewport sections per row).
 *   Within each row, cards are organized into section boundaries of width size.x.
 *   When a card exceeds a section boundary, the "last fitting window" in that section is automatically
 *   expanded so its right edge perfectly aligns to the section boundary.
 */
class display_card : public card {
public:
    /// Global card width factor multiplier (default: 4.0f).
    inline static float s_width_factor { 4.0f };

    /**
     * @brief Gets the active card width factor multiplier.
     * @return Current width factor (e.g., 4.0f for 4x).
     */
    static float get_width_factor() {
        return s_width_factor;
    }

    /**
     * @brief Sets the card width factor multiplier.
     * @param factor Width multiplier (clamped between 0.5f and 10.0f).
     */
    static void set_width_factor(float factor) {
        if (factor < 0.5f) factor = 0.5f;
        if (factor > 10.0f) factor = 10.0f;
        s_width_factor = factor;
    }

    display_card() {
        name("Display");
        width = 360.0f;

        // Colors setup for Display card
        colors[0] = ImVec4{0.2f, 0.6f, 0.9f, 1.0f}; // Primary accent
        colors[1] = ImVec4{0.4f, 0.7f, 1.0f, 0.8f}; // Secondary text/accent

        registrar::add<std::function<float()>>("get_width_factor",
            std::make_shared<std::function<float()>>([]() { return s_width_factor; }));
        registrar::add<std::function<void(float)>>("set_width_factor",
            std::make_shared<std::function<void(float)>>([](float factor) { set_width_factor(factor); }));
    }

    std::string get_uri() const override {
        return "display";
    }

    bool render() override {
        return render_window([this]() {
            ImGui::TextColored(colors[0], "%s Display Settings", ICON_MD_ASPECT_RATIO);
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped("Configure multi-width factor and layout options for Rouen cards.");
            ImGui::Spacing();

            ImGui::TextColored(colors[1], "Card Width Factor:");
            ImGui::Spacing();

            // Preset buttons (1x, 2x, 3x, 4x, 5x, 6x)
            float factors[] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
            const char* labels[] = { "1x", "2x", "3x", "4x", "5x", "6x" };

            for (int i = 0; i < 6; ++i) {
                if (i > 0) ImGui::SameLine();
                bool is_selected = (std::abs(s_width_factor - factors[i]) < 0.01f);
                if (is_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                }
                if (ImGui::Button(labels[i], ImVec2(48, 0))) {
                    set_width_factor(factors[i]);
                }
                if (is_selected) {
                    ImGui::PopStyleColor();
                }
            }

            // Information summary box
            ImGui::TextColored(colors[0], "Current Layout Metrics:");
            ImGui::BulletText("Active Width Factor: %.1fx", static_cast<double>(s_width_factor));
            ImGui::BulletText("Row Capacity: %.1fx OS Window Width", static_cast<double>(s_width_factor));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(colors[0], "%s YouTube Stream Quality Preference:", ICON_MD_VIDEO_SETTINGS);
            ImGui::Spacing();
            ImGui::TextWrapped("Choose preferred video resolution for YouTube media playback:");
            ImGui::Spacing();

            auto config = rouen::helpers::ConfigService::instance();
            std::string current_q = config ? config->get_env("ROUEN_YOUTUBE_PREFERRED_QUALITY") : "360p";
            if (current_q.empty()) current_q = "360p";

            auto iequals = [](std::string_view a, std::string_view b) -> bool {
                if (a.size() != b.size()) return false;
                return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) {
                    return std::tolower(static_cast<unsigned char>(ca)) == std::tolower(static_cast<unsigned char>(cb));
                });
            };

            const char* qualities[] = { "360p", "720p", "1080p", "1440p", "4K" };
            for (int i = 0; i < 5; ++i) {
                if (i > 0) ImGui::SameLine();
                bool is_sel = iequals(current_q, qualities[i]);
                if (is_sel) {
                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                }
                if (ImGui::Button(qualities[i], ImVec2(58, 0))) {
                    if (config) {
                        config->set_env_value("ROUEN_YOUTUBE_PREFERRED_QUALITY", qualities[i], true);
                    }
                    media_player_item::clear_youtube_cache();
                }
                if (is_sel) {
                    ImGui::PopStyleColor();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Reset to Default (4x)")) {
                set_width_factor(4.0f);
            }

            ImGui::Spacing();
            if (ImGui::Button("Expand Window to Full Display Width", ImVec2(-1, 0))) {
                auto expand_svc = registrar::get<std::function<void()>>("expand_to_full_width");
                if (expand_svc) {
                    (*expand_svc)();
                }
            }
        });
    }
};

} // namespace rouen::cards
