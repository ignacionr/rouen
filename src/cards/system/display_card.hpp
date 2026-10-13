#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "../../external/IconsMaterialDesign.h"
#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/media_player_item.hpp"
#include "../../helpers/platform_utils.hpp"
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
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

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

            ImGui::TextColored(colors[0], "%s Font Size Multiplier:", ICON_MD_TEXT_FIELDS);
            ImGui::Spacing();
            ImGui::TextWrapped("Choose global font size scale factor:");
            ImGui::Spacing();

            float font_scales[] = { 0.75f, 1.0f, 1.5f, 2.0f, 3.0f };
            const char* font_labels[] = { "0.75x", "1.0x", "1.5x", "2.0x", "3.0x" };
            float current_font_scale = rouen::fonts::get_font_scale_multiplier();

            for (int i = 0; i < 5; ++i) {
                if (i > 0) ImGui::SameLine();
                bool is_selected = (std::abs(current_font_scale - font_scales[i]) < 0.01f);
                if (is_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                }
                if (ImGui::Button(font_labels[i], ImVec2(52, 0))) {
                    rouen::fonts::set_font_scale_multiplier(font_scales[i]);
                    auto config_svc = rouen::helpers::ConfigService::instance();
                    if (config_svc) {
                        config_svc->set_env_value("ROUEN_FONT_SCALE_MULTIPLIER", std::format("{:.2f}", font_scales[i]), true);
                    }
                }
                if (is_selected) {
                    ImGui::PopStyleColor();
                }
            }

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

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // Header Container
        {
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Column 1: Emoji monitor icon
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "🖥️";
                tb["size"] = "Large";
                items.push_back(std::move(tb));
                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 2: Title and Subtitle
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = "Display & Graphics Configuration";
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = "Apple ProMotion Liquid Retina XDR";
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 3: Refresh rate badge
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = std::format("{} Hz", target_fps_);
                tb["weight"] = "Bolder";
                tb["color"] = "Good";
                tb["size"] = "Small";
                items.push_back(std::move(tb));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header_container));
        }

        // FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            facts.push_back(glz::json_t::object_t{
                {"title", "Native Resolution:"},
                {"value", std::format("{} x {} @ {}Hz", current_width_, current_height_, target_fps_)}
            });

            facts.push_back(glz::json_t::object_t{
                {"title", "Renderer API:"},
                {"value", "Apple Metal / SDL3 Hardware Accelerated"}
            });

            facts.push_back(glz::json_t::object_t{
                {"title", "Active Scale:"},
                {"value", std::format("{:.1f}x (Retina Retina High-DPI)", static_cast<double>(ui_scale_))}
            });

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // Input Controls Container
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            input_container["separator"] = true;

            std::vector<glz::json_t> items;

            // Target FPS ChoiceSet
            {
                glz::json_t choice_set;
                choice_set["type"] = "Input.ChoiceSet";
                choice_set["id"] = "target_fps";
                choice_set["label"] = "Max Framerate Cap";
                choice_set["value"] = std::to_string(target_fps_);

                std::vector<glz::json_t> choices;
                choices.push_back(glz::json_t::object_t{{"title", "30 FPS (Power Saver)"}, {"value", "30"}});
                choices.push_back(glz::json_t::object_t{{"title", "60 FPS (Standard)"}, {"value", "60"}});
                choices.push_back(glz::json_t::object_t{{"title", "120 FPS (High Refresh)"}, {"value", "120"}});
                choice_set["choices"] = std::move(choices);
                items.push_back(std::move(choice_set));
            }

            // VSync Toggle
            {
                glz::json_t toggle;
                toggle["type"] = "Input.Toggle";
                toggle["id"] = "vsync_toggle";
                toggle["title"] = "Enable Vertical Sync (VSync)";
                toggle["value"] = vsync_enabled_ ? "true" : "false";
                items.push_back(std::move(toggle));
            }

            input_container["items"] = std::move(items);
            body.push_back(std::move(input_container));
        }

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_apply;
            act_apply["type"] = "Action.Execute";
            act_apply["title"] = "Apply Display Settings";
            act_apply["verb"] = "display_apply";
            actions.push_back(std::move(act_apply));

            glz::json_t act_reset;
            act_reset["type"] = "Action.Execute";
            act_reset["title"] = "Reset Defaults";
            act_reset["verb"] = "display_reset";
            actions.push_back(std::move(act_reset));
        }
        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override {
        if (action_json.empty()) return;
        try {
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "display_apply") {
                if (data.is_object()) {
                    if (data.contains("target_fps")) {
                        if (data["target_fps"].holds<double>()) {
                            target_fps_ = static_cast<int>(data["target_fps"].get<double>());
                        } else if (data["target_fps"].holds<std::string>()) {
                            try {
                                target_fps_ = std::stoi(data["target_fps"].get<std::string>());
                            } catch (...) {}
                        }
                    }
                    if (data.contains("ui_scale")) {
                        if (data["ui_scale"].holds<double>()) {
                            ui_scale_ = static_cast<float>(data["ui_scale"].get<double>());
                        } else if (data["ui_scale"].holds<std::string>()) {
                            try {
                                ui_scale_ = std::stof(data["ui_scale"].get<std::string>());
                            } catch (...) {}
                        }
                    }
                    if (data.contains("vsync")) {
                        if (data["vsync"].holds<bool>()) {
                            vsync_enabled_ = data["vsync"].get<bool>();
                        } else if (data["vsync"].holds<std::string>()) {
                            std::string const s = data["vsync"].get<std::string>();
                            vsync_enabled_ = (s == "true" || s == "1" || s == "yes");
                        }
                    } else if (data.contains("vsync_toggle")) {
                        if (data["vsync_toggle"].holds<bool>()) {
                            vsync_enabled_ = data["vsync_toggle"].get<bool>();
                        } else if (data["vsync_toggle"].holds<std::string>()) {
                            std::string const s = data["vsync_toggle"].get<std::string>();
                            vsync_enabled_ = (s == "true" || s == "1" || s == "yes");
                        }
                    }
                }
                last_adaptive_parse_time_ = {};
            } else if (verb == "display_reset") {
                current_width_ = 3024;
                current_height_ = 1964;
                target_fps_ = 60;
                ui_scale_ = 2.0f;
                vsync_enabled_ = true;
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[display] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }

    [[nodiscard]] int get_display_count() const noexcept { return display_count_; }
    [[nodiscard]] int get_current_width() const noexcept { return current_width_; }
    [[nodiscard]] int get_current_height() const noexcept { return current_height_; }
    [[nodiscard]] int get_target_fps() const noexcept { return target_fps_; }
    [[nodiscard]] float get_ui_scale() const noexcept { return ui_scale_; }
    [[nodiscard]] bool is_vsync_enabled() const noexcept { return vsync_enabled_; }

    void set_target_fps(int fps) noexcept { target_fps_ = fps; }
    void set_ui_scale(float scale) noexcept { ui_scale_ = scale; }
    void set_vsync_enabled(bool enabled) noexcept { vsync_enabled_ = enabled; }

private:
    int display_count_{1};
    int current_width_{3024};
    int current_height_{1964};
    int target_fps_{120};
    float ui_scale_{2.0f};
    bool vsync_enabled_{true};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    mutable std::string adaptive_error_{};
    mutable std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
};

} // namespace rouen::cards

