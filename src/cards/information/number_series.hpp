#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../models/series/series_repository.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class number_series_card : public card {
public:
    using data_point = models::series::data_point;
    using series_data = models::series::series_record;

    explicit number_series_card(std::string_view locator = {})
        : repository_{} {
        refresh_datasets();

        if (!locator.empty()) {
            handle_uri(locator.starts_with("number-series:") ? locator : std::format("number-series:{}", locator));
        } else if (!datasets_.empty()) {
            selected_dataset_index_ = 0;
            current_data_ = datasets_[0];
        }

        colors[0] = {0.20f, 0.43f, 0.70f, 1.0f};
        colors[1] = {0.14f, 0.32f, 0.55f, 0.75f};
        name(current_data_.title.empty() ? "Number Series" : current_data_.title);
        width = 540.0f;
    }

    std::string get_uri() const override {
        if (current_data_.name.empty()) {
            return "number-series";
        }
        return std::format("number-series:{}", current_data_.name);
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "number-series" || uri.starts_with("number-series:");
    }

    void handle_uri(std::string_view uri) override {
        std::string target;
        if (uri.starts_with("number-series:")) {
            target = models::series::series_repository::trim(uri.substr(14));
        } else {
            target = models::series::series_repository::trim(uri);
        }

        if (target.empty()) {
            return;
        }

        if (target.starts_with('{')) {
            series_data custom_data{};
            auto result = glz::read_json(custom_data, target);
            if (!result) {
                if (custom_data.name.empty()) {
                    custom_data.name = models::series::series_repository::slugify(custom_data.title);
                }
                repository_.save_series(custom_data);
                refresh_datasets();
                select_series_by_name(custom_data.name);
            }
            return;
        }

        auto existing = repository_.get_series_by_name(target);
        if (existing.has_value()) {
            refresh_datasets();
            select_series_by_name(existing->name);
        }
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

            // Action toolbar: New, Save, Delete buttons
            if (ImGui::Button("New Series")) {
                create_new_series();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                save_current_series();
            }
            ImGui::SameLine();

            bool can_delete = !datasets_.empty();
            if (!can_delete) ImGui::BeginDisabled();
            if (ImGui::Button("Delete")) {
                if (!current_data_.name.empty()) {
                    repository_.delete_series(current_data_.name);
                    refresh_datasets();
                    if (!datasets_.empty()) {
                        selected_dataset_index_ = 0;
                        current_data_ = datasets_[0];
                        name(current_data_.title);
                    }
                }
            }
            if (!can_delete) ImGui::EndDisabled();

            ImGui::Spacing();

            // Dropdown to select preset/saved dataset
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo("Series", current_data_.title.c_str())) {
                for (size_t i = 0; i < datasets_.size(); ++i) {
                    bool is_selected = (i == selected_dataset_index_);
                    if (ImGui::Selectable(datasets_[i].title.c_str(), is_selected)) {
                        save_current_series();
                        selected_dataset_index_ = i;
                        current_data_ = datasets_[i];
                        name(current_data_.title);
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            
            // Toggle chart type
            if (ImGui::Checkbox("Bar Chart", &current_data_.is_bar_chart)) {
                save_current_series();
            }

            ImGui::SameLine();

            // Color Selector from Theme Colors
            ImGui::SetNextItemWidth(100.0f);
            static const char* color_names[] = {
                "Accent (0)", "Secondary (1)", "Red/Error (2)", "Green/Success (3)",
                "Warning (4)", "Info (5)", "Purple (6)", "Pink (7)", "Orange (8)", "Gray (9)"
            };
            if (ImGui::Combo("Color", &current_data_.color_index, color_names, IM_ARRAYSIZE(color_names))) {
                save_current_series();
            }

            ImGui::Dummy(ImVec2(0.0f, 5.0f));

            // Chart area
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            canvas_size.y = 220.0f; // Fixed height for chart

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImU32 bg_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
            draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), bg_color, 6.0f);
            draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), border_color, 6.0f);

            // Margins for plot
            float margin_left = 55.0f;
            float margin_right = 20.0f;
            float margin_top = 20.0f;
            float margin_bottom = 30.0f;

            ImVec2 plot_pos = ImVec2(canvas_pos.x + margin_left, canvas_pos.y + margin_top);
            ImVec2 plot_size = ImVec2(canvas_size.x - margin_left - margin_right, canvas_size.y - margin_top - margin_bottom);

            // Mouse Hover detection
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            bool is_hovering = ImGui::IsMouseHoveringRect(plot_pos, ImVec2(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y));
            int hovered_idx = -1;
            size_t n = current_data_.points.size();

            if (is_hovering && n > 0) {
                float local_x = mouse_pos.x - plot_pos.x;
                if (current_data_.is_bar_chart) {
                    float col_width = plot_size.x / static_cast<float>(n);
                    hovered_idx = static_cast<int>(local_x / col_width);
                } else {
                    if (n > 1) {
                        float step = plot_size.x / static_cast<float>(n - 1);
                        hovered_idx = static_cast<int>((local_x + step * 0.5f) / step);
                    } else {
                        hovered_idx = 0;
                    }
                }
                if (hovered_idx < 0) hovered_idx = 0;
                if (hovered_idx >= static_cast<int>(n)) hovered_idx = static_cast<int>(n) - 1;
            }

            ImVec4 active_color = get_color(static_cast<size_t>(current_data_.color_index), ImVec4{0.2f, 0.6f, 0.9f, 1.0f});

            // Render chart canvas with interactive hover state
            draw_series_plot(draw_list, canvas_pos, canvas_size, hovered_idx, active_color, true);

            ImGui::Dummy(ImVec2(0.0f, canvas_size.y + 5.0f));

            // Stats row
            float sum = 0.0f;
            float max_val_stat = n > 0 ? -std::numeric_limits<float>::infinity() : 0.0f;
            float min_val_stat = n > 0 ? std::numeric_limits<float>::infinity() : 0.0f;
            for (const auto& pt : current_data_.points) {
                sum += pt.value;
                if (pt.value > max_val_stat) max_val_stat = pt.value;
                if (pt.value < min_val_stat) min_val_stat = pt.value;
            }
            float avg = n == 0 ? 0.0f : sum / static_cast<float>(n);

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 2.0f));

            ImGui::Columns(4, "StatsGrid", false);
            
            // Total/Sum
            ImGui::TextDisabled("TOTAL");
            if (current_data_.unit == "$") {
                ImGui::TextColored(active_color, "%s", std::format("${:.0f}", sum).c_str());
            } else {
                std::string label = current_data_.unit == "C" ? "°C" : current_data_.unit;
                ImGui::TextColored(active_color, "%s", std::format("{:.1f} {}", sum, label).c_str());
            }
            ImGui::NextColumn();

            // Average
            ImGui::TextDisabled("AVERAGE");
            if (current_data_.unit == "$") {
                ImGui::TextColored(active_color, "%s", std::format("${:.1f}", avg).c_str());
            } else {
                std::string label = current_data_.unit == "C" ? "°C" : current_data_.unit;
                ImGui::TextColored(active_color, "%s", std::format("{:.1f} {}", avg, label).c_str());
            }
            ImGui::NextColumn();

            // Min
            ImGui::TextDisabled("MIN");
            if (current_data_.unit == "$") {
                ImGui::TextColored(get_color(2), "%s", std::format("${:.1f}", min_val_stat).c_str());
            } else {
                std::string label = current_data_.unit == "C" ? "°C" : current_data_.unit;
                ImGui::TextColored(get_color(2), "%s", std::format("{:.1f} {}", min_val_stat, label).c_str());
            }
            ImGui::NextColumn();

            // Max
            ImGui::TextDisabled("MAX");
            if (current_data_.unit == "$") {
                ImGui::TextColored(get_color(3), "%s", std::format("${:.1f}", max_val_stat).c_str());
            } else {
                std::string label = current_data_.unit == "C" ? "°C" : current_data_.unit;
                ImGui::TextColored(get_color(3), "%s", std::format("{:.1f} {}", max_val_stat, label).c_str());
            }
            ImGui::Columns(1);

            ImGui::Spacing();
            ImGui::Separator();

            // Edit Data Section
            if (ImGui::CollapsingHeader("Data Points Editor")) {
                bool changed = false;

                // Title and Unit editor
                char title_buf[128];
                strncpy(title_buf, current_data_.title.c_str(), sizeof(title_buf) - 1);
                title_buf[sizeof(title_buf) - 1] = '\0';
                ImGui::SetNextItemWidth(230.0f);
                if (ImGui::InputText("Series Title", title_buf, sizeof(title_buf))) {
                    current_data_.title = title_buf;
                    name(current_data_.title);
                    changed = true;
                }

                ImGui::SameLine();

                char unit_buf[32];
                strncpy(unit_buf, current_data_.unit.c_str(), sizeof(unit_buf) - 1);
                unit_buf[sizeof(unit_buf) - 1] = '\0';
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::InputText("Unit", unit_buf, sizeof(unit_buf))) {
                    current_data_.unit = unit_buf;
                    changed = true;
                }

                ImGui::Spacing();

                // Points table
                ImGui::BeginChild("PointsList", ImVec2(0, 140.0f), true);
                int delete_idx = -1;
                for (size_t i = 0; i < current_data_.points.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));

                    char name_buf[64];
                    strncpy(name_buf, current_data_.points[i].label.c_str(), sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::InputText("##LabelInput", name_buf, sizeof(name_buf))) {
                        current_data_.points[i].label = name_buf;
                        changed = true;
                    }

                    ImGui::SameLine();

                    float val = current_data_.points[i].value;
                    ImGui::SetNextItemWidth(180.0f);

                    float range_min = (current_data_.name == "temps") ? -20.0f : 0.0f;
                    float range_max = (current_data_.name == "cpu") ? 100.0f : 
                                      (current_data_.name == "temps") ? 50.0f : 100000.0f;

                    if (ImGui::SliderFloat("##ValueInput", &val, range_min, range_max, "%.1f")) {
                        current_data_.points[i].value = val;
                        changed = true;
                    }

                    if (ImGui::IsItemHovered()) {
                        hovered_idx = static_cast<int>(i);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Delete")) {
                        delete_idx = static_cast<int>(i);
                    }

                    ImGui::PopID();
                }

                if (delete_idx >= 0 && delete_idx < static_cast<int>(current_data_.points.size())) {
                    current_data_.points.erase(current_data_.points.begin() + delete_idx);
                    changed = true;
                }

                ImGui::EndChild();

                // Add Point Form
                static char add_label[64] = "New Point";
                static float add_val = 10.0f;

                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputText("Label##Add", add_label, sizeof(add_label));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputFloat("Value##Add", &add_val);
                ImGui::SameLine();
                if (ImGui::Button("Add Point")) {
                    current_data_.points.push_back({add_label, add_val});
                    changed = true;
                }

                if (changed) {
                    save_current_series();
                }
            }
        });
    }

    bool has_video_overlay() const override { return true; }

    void render_video_ui() override {
        if (current_data_.points.empty()) return;

        // Calculate continuous animation index (cycles 1.2s per data point)
        size_t n = current_data_.points.size();
        auto now = std::chrono::steady_clock::now();
        float elapsed_sec = std::chrono::duration<float>(now.time_since_epoch()).count();
        float cycle_duration = 1.2f; // 1.2 seconds per point
        float total_cycle_sec = cycle_duration * static_cast<float>(n);
        float current_sec = std::fmod(elapsed_sec, total_cycle_sec);
        int animated_hover_idx = static_cast<int>(current_sec / cycle_duration);
        animated_hover_idx = std::clamp(animated_hover_idx, 0, static_cast<int>(n) - 1);

        ImVec2 display_size = ImGui::GetIO().DisplaySize;
        float overlay_w = std::clamp(display_size.x * 0.42f, 380.0f, 620.0f);
        float overlay_h = std::clamp(display_size.y * 0.38f, 260.0f, 400.0f);
        float pos_x = 36.0f;
        float pos_y = std::max(36.0f, display_size.y - overlay_h - 36.0f);

        ImVec4 active_color = get_color(static_cast<size_t>(current_data_.color_index), ImVec4{0.2f, 0.6f, 0.9f, 1.0f});

        ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
        ImGui::SetNextWindowSize(ImVec2(overlay_w, overlay_h));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.5f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.12f, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_Border, active_color);

        if (ImGui::Begin("##NumberSeriesVideoOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
            // Header Bar
            ImGui::TextColored(active_color, "%s", current_data_.is_bar_chart ? ICON_MD_BAR_CHART : ICON_MD_SHOW_CHART);
            ImGui::SameLine();
            ImGui::Text("%s", current_data_.title.c_str());

            // Telemetry Metric Badge on Right (updates dynamically to active animated point!)
            const auto& active_pt = current_data_.points[static_cast<size_t>(animated_hover_idx)];
            std::string metric_str;
            if (current_data_.unit == "$") {
                metric_str = std::format("{}: ${:.1f}", active_pt.label, active_pt.value);
            } else {
                metric_str = std::format("{}: {:.1f}{}", active_pt.label, active_pt.value, current_data_.unit == "C" ? "°C" : current_data_.unit);
            }

            ImVec2 badge_size = ImGui::CalcTextSize(metric_str.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - badge_size.x);
            ImGui::TextColored(active_color, "%s", metric_str.c_str());

            ImGui::Separator();
            ImGui::Spacing();

            // Chart Canvas
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            canvas_size.y = std::max(120.0f, canvas_size.y - 35.0f);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImU32 bg_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
            draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), bg_color, 6.0f);
            draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), border_color, 6.0f);

            // Draw series plot with animated hover index!
            draw_series_plot(draw_list, canvas_pos, canvas_size, animated_hover_idx, active_color, false);

            ImGui::Dummy(ImVec2(0.0f, canvas_size.y + 4.0f));

            // Summary Stats Footer
            float sum = 0.0f;
            float max_v = current_data_.points[0].value;
            float min_v = current_data_.points[0].value;
            for (const auto& pt : current_data_.points) {
                sum += pt.value;
                max_v = std::max(max_v, pt.value);
                min_v = std::min(min_v, pt.value);
            }
            float avg = sum / static_cast<float>(n);

            std::string unit_lbl = (current_data_.unit == "C") ? "°C" : current_data_.unit;
            std::string fmt_prefix = (current_data_.unit == "$") ? "$" : "";
            std::string fmt_suffix = (current_data_.unit == "$") ? "" : unit_lbl;

            ImGui::TextDisabled("Avg: %s%.1f%s  |  Max: %s%.1f%s  |  Min: %s%.1f%s",
                fmt_prefix.c_str(), static_cast<double>(avg), fmt_suffix.c_str(),
                fmt_prefix.c_str(), static_cast<double>(max_v), fmt_suffix.c_str(),
                fmt_prefix.c_str(), static_cast<double>(min_v), fmt_suffix.c_str());
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // Stats calculation
        float sum = 0.0f;
        float max_val_stat = current_data_.points.empty() ? 0.0f : -std::numeric_limits<float>::infinity();
        float min_val_stat = current_data_.points.empty() ? 0.0f : std::numeric_limits<float>::infinity();
        for (const auto& pt : current_data_.points) {
            sum += pt.value;
            if (pt.value > max_val_stat) max_val_stat = pt.value;
            if (pt.value < min_val_stat) min_val_stat = pt.value;
        }
        float avg = current_data_.points.empty() ? 0.0f : sum / static_cast<float>(current_data_.points.size());

        std::string unit_lbl = (current_data_.unit == "C") ? "°C" : current_data_.unit;
        auto format_val = [&](float val) -> std::string {
            if (current_data_.unit == "$") {
                return std::format("${:.1f}", val);
            }
            return std::format("{:.1f}{}", val, unit_lbl);
        };

        std::string current_val_str = current_data_.points.empty() ? "N/A" : format_val(current_data_.points.back().value);
        std::string min_val_str = current_data_.points.empty() ? "N/A" : format_val(min_val_stat);
        std::string max_val_str = current_data_.points.empty() ? "N/A" : format_val(max_val_stat);
        std::string avg_val_str = current_data_.points.empty() ? "Avg: N/A" : std::format("Avg: {}", format_val(avg));

        // 1. Header Container
        {
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Column 1: Emoji icon
            {
                glz::json_t col1;
                col1["type"] = "Column";
                col1["width"] = "auto";
                std::vector<glz::json_t> col1_items;
                glz::json_t tb_icon;
                tb_icon["type"] = "TextBlock";
                tb_icon["text"] = "📈";
                tb_icon["size"] = "Large";
                col1_items.push_back(std::move(tb_icon));
                col1["items"] = std::move(col1_items);
                columns.push_back(std::move(col1));
            }

            // Column 2: Title & Subtitle
            {
                glz::json_t col2;
                col2["type"] = "Column";
                col2["width"] = "stretch";
                std::vector<glz::json_t> col2_items;

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                std::string display_title = current_data_.title.empty() ? "Number Series" : current_data_.title;
                if (!unit_lbl.empty() && display_title.find(unit_lbl) == std::string::npos) {
                    display_title = std::format("{} ({})", display_title, unit_lbl);
                }
                tb_title["text"] = display_title;
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                col2_items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = std::format("Series: {} ({} samples)", current_data_.name.empty() ? "default" : current_data_.name, current_data_.points.size());
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                col2_items.push_back(std::move(tb_sub));

                col2["items"] = std::move(col2_items);
                columns.push_back(std::move(col2));
            }

            // Column 3: Avg badge
            {
                glz::json_t col3;
                col3["type"] = "Column";
                col3["width"] = "auto";
                std::vector<glz::json_t> col3_items;

                glz::json_t tb_badge;
                tb_badge["type"] = "TextBlock";
                tb_badge["text"] = avg_val_str;
                tb_badge["weight"] = "Bolder";
                tb_badge["color"] = "Good";
                tb_badge["size"] = "Medium";
                col3_items.push_back(std::move(tb_badge));

                col3["items"] = std::move(col3_items);
                columns.push_back(std::move(col3));
            }

            col_set["columns"] = std::move(columns);
            std::vector<glz::json_t> header_items;
            header_items.push_back(std::move(col_set));
            header_container["items"] = std::move(header_items);
            body.push_back(std::move(header_container));
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            facts.push_back(glz::json_t::object_t{
                {"title", "Current Reading:"},
                {"value", current_val_str}
            });
            facts.push_back(glz::json_t::object_t{
                {"title", "Minimum Value:"},
                {"value", min_val_str}
            });
            facts.push_back(glz::json_t::object_t{
                {"title", "Peak Value:"},
                {"value", max_val_str}
            });
            facts.push_back(glz::json_t::object_t{
                {"title", "Sample Count:"},
                {"value", std::format("{} data points", current_data_.points.size())}
            });

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Selection & Filter Container
        {
            glz::json_t selector_container;
            selector_container["type"] = "Container";
            selector_container["separator"] = true;
            std::vector<glz::json_t> container_items;

            glz::json_t choice_set;
            choice_set["type"] = "Input.ChoiceSet";
            choice_set["id"] = "dataset_selector";
            choice_set["label"] = "Select Metric Series";
            choice_set["value"] = current_data_.name.empty() ? "cpu" : current_data_.name;
            std::vector<glz::json_t> choices;

            if (!datasets_.empty()) {
                for (const auto& ds : datasets_) {
                    std::string title = ds.title;
                    if (!ds.unit.empty() && title.find(ds.unit) == std::string::npos) {
                        std::string u = (ds.unit == "C") ? "°C" : ds.unit;
                        title = std::format("{} ({})", title, u);
                    }
                    choices.push_back(glz::json_t::object_t{
                        {"title", title},
                        {"value", ds.name}
                    });
                }
            } else {
                choices.push_back(glz::json_t::object_t{
                    {"title", "Host CPU Load (%)"},
                    {"value", "cpu"}
                });
                choices.push_back(glz::json_t::object_t{
                    {"title", "Weekly Temperature Forecast (°C)"},
                    {"value", "temps"}
                });
                choices.push_back(glz::json_t::object_t{
                    {"title", "Monthly Sales Revenue ($)"},
                    {"value", "sales"}
                });
            }
            choice_set["choices"] = std::move(choices);
            container_items.push_back(std::move(choice_set));

            selector_container["items"] = std::move(container_items);
            body.push_back(std::move(selector_container));
        }

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_select;
            act_select["type"] = "Action.Execute";
            act_select["title"] = "Switch Series";
            act_select["verb"] = "select_series";
            actions.push_back(std::move(act_select));

            glz::json_t act_filter;
            act_filter["type"] = "Action.Execute";
            act_filter["title"] = "Filter Range";
            act_filter["verb"] = "filter_range";
            actions.push_back(std::move(act_filter));

            glz::json_t act_export;
            act_export["type"] = "Action.Execute";
            act_export["title"] = "Export CSV";
            act_export["verb"] = "export_csv";
            actions.push_back(std::move(act_export));
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
            std::string verb;
            glz::json_t data{};
            glz::json_t payload{};

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

            if (verb == "select_series") {
                std::string dataset;
                if (data.is_object()) {
                    if (data.contains("dataset") && data["dataset"].holds<std::string>()) {
                        dataset = data["dataset"].get<std::string>();
                    } else if (data.contains("dataset_selector") && data["dataset_selector"].holds<std::string>()) {
                        dataset = data["dataset_selector"].get<std::string>();
                    } else if (data.contains("series") && data["series"].holds<std::string>()) {
                        dataset = data["series"].get<std::string>();
                    } else if (data.contains("series_id") && data["series_id"].holds<std::string>()) {
                        dataset = data["series_id"].get<std::string>();
                    } else if (data.contains("name") && data["name"].holds<std::string>()) {
                        dataset = data["name"].get<std::string>();
                    }
                }
                if (!dataset.empty()) {
                    select_series_by_name(dataset);
                }
                last_action_ = "select_series";
                last_adaptive_parse_time_ = {};
            } else if (verb == "filter_range") {
                std::string range;
                if (data.is_object()) {
                    if (data.contains("range") && data["range"].holds<std::string>()) {
                        range = data["range"].get<std::string>();
                    } else if (data.contains("filter_range") && data["filter_range"].holds<std::string>()) {
                        range = data["filter_range"].get<std::string>();
                    }
                }
                if (!range.empty()) {
                    selected_range_ = range;
                }
                last_action_ = "filter_range";
                last_adaptive_parse_time_ = {};
            } else if (verb == "export_csv") {
                std::string export_path;
                if (data.is_object() && data.contains("path") && data["path"].holds<std::string>()) {
                    export_path = data["path"].get<std::string>();
                } else {
                    auto dir = rouen::platform::get_user_data_path();
                    std::filesystem::create_directories(dir);
                    export_path = (dir / std::format("{}.csv", current_data_.name.empty() ? "series" : current_data_.name)).string();
                }
                try {
                    std::ofstream ofs(export_path);
                    if (ofs.is_open()) {
                        ofs << "Order,Label,Value\n";
                        for (size_t i = 0; i < current_data_.points.size(); ++i) {
                            ofs << i << ",\"" << current_data_.points[i].label << "\"," << current_data_.points[i].value << "\n";
                        }
                        ofs.close();
                        last_exported_csv_path_ = export_path;
                    }
                } catch (...) {}
                last_action_ = "export_csv";
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_action_ = "toggle_adaptive_view";
            } else if (verb == "refresh") {
                refresh_datasets();
                last_action_ = "refresh";
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[number-series] Action error: " << e.what() << std::endl;
        }
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool mode) noexcept { adaptive_view_mode = mode; }
    [[nodiscard]] std::string get_last_action() const { return last_action_; }
    [[nodiscard]] std::string get_selected_series_name() const { return current_data_.name; }
    [[nodiscard]] std::string get_selected_range() const { return selected_range_; }
    [[nodiscard]] std::string get_last_exported_csv() const { return last_exported_csv_path_; }

private:
    void refresh_datasets() {
        datasets_ = repository_.list_series();
    }

    void save_current_series() {
        if (current_data_.title.empty()) return;
        if (current_data_.name.empty()) {
            current_data_.name = models::series::series_repository::slugify(current_data_.title);
        }
        int id = repository_.save_series(current_data_);
        current_data_.id = id;
        refresh_datasets();
        select_series_by_name(current_data_.name);
    }

    void create_new_series() {
        static int new_counter = 1;
        series_data new_s{
            .id = 0,
            .name = std::format("new_series_{}", new_counter),
            .title = std::format("New Series {}", new_counter),
            .unit = "",
            .points = {
                {"Point 1", 10.0f},
                {"Point 2", 25.0f},
                {"Point 3", 18.0f}
            },
            .is_bar_chart = true,
            .color_index = 0,
            .created_at = "",
            .updated_at = ""
        };
        new_counter++;
        repository_.save_series(new_s);
        refresh_datasets();
        select_series_by_name(new_s.name);
    }

    void select_series_by_name(const std::string& name_or_title) {
        for (size_t i = 0; i < datasets_.size(); ++i) {
            if (datasets_[i].name == name_or_title || datasets_[i].title == name_or_title) {
                selected_dataset_index_ = i;
                current_data_ = datasets_[i];
                name(current_data_.title);
                return;
            }
        }
        if (!datasets_.empty()) {
            selected_dataset_index_ = 0;
            current_data_ = datasets_[0];
            name(current_data_.title);
        }
    }

    void draw_series_plot(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size, int hovered_idx, ImVec4 active_color, bool show_imgui_tooltip = true) {
        // Margins for plot
        float margin_left = 55.0f;
        float margin_right = 20.0f;
        float margin_top = 20.0f;
        float margin_bottom = 30.0f;

        ImVec2 plot_pos = ImVec2(canvas_pos.x + margin_left, canvas_pos.y + margin_top);
        ImVec2 plot_size = ImVec2(canvas_size.x - margin_left - margin_right, canvas_size.y - margin_top - margin_bottom);

        // Bounds computation
        float min_val = 0.0f;
        float max_val = 0.0f;
        size_t n = current_data_.points.size();

        if (n > 0) {
            min_val = current_data_.points[0].value;
            max_val = current_data_.points[0].value;
            for (const auto& pt : current_data_.points) {
                min_val = std::min(min_val, pt.value);
                max_val = std::max(max_val, pt.value);
            }
        }

        if (max_val - min_val < 0.0001f) {
            max_val += 1.0f;
            min_val -= 1.0f;
        }

        // Force min Y to 0 for positive/non-temperature series
        float y_min = 0.0f;
        if (current_data_.name == "temps" || min_val < 0.0f) {
            float diff = max_val - min_val;
            y_min = min_val - diff * 0.1f;
        }
        float diff = max_val - y_min;
        float y_max = max_val + diff * 0.1f;

        // Grid lines (horizontal)
        int grid_lines = 4;
        for (int i = 0; i <= grid_lines; ++i) {
            float ratio = static_cast<float>(i) / static_cast<float>(grid_lines);
            float y = plot_pos.y + plot_size.y * (1.0f - ratio);
            float val = y_min + ratio * (y_max - y_min);

            draw_list->AddLine(
                ImVec2(plot_pos.x, y),
                ImVec2(plot_pos.x + plot_size.x, y),
                ImGui::GetColorU32(ImGuiCol_Separator, 0.25f),
                1.0f
            );

            std::string label;
            if (current_data_.unit == "$") {
                if (val >= 1000.0f) {
                    label = std::format("${:.1f}k", val / 1000.0f);
                } else {
                    label = std::format("${:.0f}", val);
                }
            } else {
                label = std::format("{:.1f}{}", val, current_data_.unit == "C" ? "°C" : current_data_.unit);
            }

            ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
            draw_list->AddText(
                ImVec2(plot_pos.x - label_size.x - 6.0f, y - label_size.y * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                label.c_str()
            );
        }

        // Setup colors
        ImU32 theme_color_u32 = ImGui::ColorConvertFloat4ToU32(active_color);
        ImU32 theme_color_trans_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(active_color.x, active_color.y, active_color.z, 0.15f));
        ImU32 theme_color_bright_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::min(1.0f, active_color.x * 1.25f),
            std::min(1.0f, active_color.y * 1.25f),
            std::min(1.0f, active_color.z * 1.25f),
            1.0f
        ));

        // Draw series data
        if (n > 0) {
            if (current_data_.is_bar_chart) {
                float col_width = plot_size.x / static_cast<float>(n);
                float bar_width = col_width * 0.7f;
                float spacing = col_width * 0.3f;

                for (size_t i = 0; i < n; ++i) {
                    float ratio = (current_data_.points[i].value - y_min) / (y_max - y_min);
                    float bar_height = plot_size.y * ratio;

                    float x_left = plot_pos.x + static_cast<float>(i) * col_width + spacing * 0.5f;
                    float x_right = x_left + bar_width;
                    float y_top = plot_pos.y + plot_size.y - bar_height;
                    float y_bottom = plot_pos.y + plot_size.y;

                    bool is_bar_hovered = (static_cast<int>(i) == hovered_idx);
                    ImU32 col_top = is_bar_hovered ? theme_color_bright_u32 : theme_color_u32;
                    ImU32 col_bottom = ImGui::ColorConvertFloat4ToU32(ImVec4(active_color.x, active_color.y, active_color.z, 0.20f));

                    draw_list->AddRectFilledMultiColor(
                        ImVec2(x_left, y_top), ImVec2(x_right, y_bottom),
                        col_top, col_top, col_bottom, col_bottom
                    );

                    if (is_bar_hovered) {
                        draw_list->AddRect(
                            ImVec2(x_left, y_top), ImVec2(x_right, y_bottom),
                            ImGui::GetColorU32(ImGuiCol_Text), 2.0f, 0, 1.5f
                        );
                    } else {
                        draw_list->AddRect(
                            ImVec2(x_left, y_top), ImVec2(x_right, y_bottom),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(active_color.x, active_color.y, active_color.z, 0.40f)),
                            2.0f
                        );
                    }

                    // X Label
                    std::string x_label = current_data_.points[i].label;
                    ImVec2 label_size = ImGui::CalcTextSize(x_label.c_str());
                    float label_x = x_left + bar_width * 0.5f - label_size.x * 0.5f;
                    draw_list->AddText(
                        ImVec2(label_x, plot_pos.y + plot_size.y + 6.0f),
                        is_bar_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        x_label.c_str()
                    );
                }
            } else {
                // Line Chart
                std::vector<ImVec2> line_pts(n);
                float step = (n > 1) ? (plot_size.x / static_cast<float>(n - 1)) : plot_size.x;

                for (size_t i = 0; i < n; ++i) {
                    float ratio = (current_data_.points[i].value - y_min) / (y_max - y_min);
                    line_pts[i] = ImVec2(
                        plot_pos.x + static_cast<float>(i) * step,
                        plot_pos.y + plot_size.y * (1.0f - ratio)
                    );
                }

                // Fill Area
                if (n > 1) {
                    draw_list->PathClear();
                    draw_list->PathLineTo(ImVec2(line_pts[0].x, plot_pos.y + plot_size.y));
                    for (size_t i = 0; i < n; ++i) {
                        draw_list->PathLineTo(line_pts[i]);
                    }
                    draw_list->PathLineTo(ImVec2(line_pts[n - 1].x, plot_pos.y + plot_size.y));
                    draw_list->PathFillConvex(theme_color_trans_u32);
                }

                // Draw Line segments
                for (size_t i = 0; i < n - 1; ++i) {
                    draw_list->AddLine(line_pts[i], line_pts[i + 1], theme_color_u32, 2.5f);
                }

                // Points & Labels
                for (size_t i = 0; i < n; ++i) {
                    bool is_pt_hovered = (static_cast<int>(i) == hovered_idx);

                    draw_list->AddCircleFilled(line_pts[i], is_pt_hovered ? 6.0f : 4.0f, is_pt_hovered ? theme_color_bright_u32 : theme_color_u32);
                    draw_list->AddCircle(line_pts[i], is_pt_hovered ? 6.0f : 4.0f, ImGui::GetColorU32(ImGuiCol_WindowBg), 0, 1.5f);

                    std::string x_label = current_data_.points[i].label;
                    ImVec2 label_size = ImGui::CalcTextSize(x_label.c_str());
                    float label_x = line_pts[i].x - label_size.x * 0.5f;

                    if (i == 0) label_x = std::max(label_x, plot_pos.x);
                    if (i == n - 1) label_x = std::min(label_x, plot_pos.x + plot_size.x - label_size.x);

                    draw_list->AddText(
                        ImVec2(label_x, plot_pos.y + plot_size.y + 6.0f),
                        is_pt_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        x_label.c_str()
                    );
                }
            }
        }

        // Draw interaction guide line and tooltip
        if (hovered_idx >= 0 && hovered_idx < static_cast<int>(n)) {
            float col_width = plot_size.x / static_cast<float>(n);
            float step = (n > 1) ? (plot_size.x / static_cast<float>(n - 1)) : plot_size.x;

            float x_hover = current_data_.is_bar_chart ?
                (plot_pos.x + static_cast<float>(hovered_idx) * col_width + col_width * 0.5f) :
                (plot_pos.x + static_cast<float>(hovered_idx) * step);

            size_t h_idx = static_cast<size_t>(hovered_idx);
            float ratio = (current_data_.points[h_idx].value - y_min) / (y_max - y_min);
            float y_hover = plot_pos.y + plot_size.y * (1.0f - ratio);

            // Vertical line guide
            draw_list->AddLine(
                ImVec2(x_hover, plot_pos.y),
                ImVec2(x_hover, plot_pos.y + plot_size.y),
                ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.4f),
                1.0f
            );

            // Tooltip text formatting
            std::string label = current_data_.points[h_idx].label;
            std::string val_str;
            if (current_data_.unit == "$") {
                val_str = std::format("${:.1f}", current_data_.points[h_idx].value);
            } else {
                val_str = std::format("{:.1f} {}", current_data_.points[h_idx].value, current_data_.unit == "C" ? "°C" : current_data_.unit);
            }

            std::string tooltip_text = std::format("{}: {}", label, val_str);

            // Draw floating tag inside plot canvas
            ImVec2 text_size = ImGui::CalcTextSize(tooltip_text.c_str());
            float box_w = text_size.x + 16.0f;
            float box_h = text_size.y + 8.0f;
            float box_x = x_hover - box_w * 0.5f;
            float box_y = y_hover - box_h - 10.0f;

            box_x = std::max(plot_pos.x, std::min(box_x, plot_pos.x + plot_size.x - box_w));
            box_y = std::max(plot_pos.y, std::min(box_y, plot_pos.y + plot_size.y - box_h));

            ImVec2 box_min = ImVec2(box_x, box_y);
            ImVec2 box_max = ImVec2(box_x + box_w, box_y + box_h);

            // Background
            draw_list->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImGuiCol_PopupBg, 0.92f), 4.0f);
            draw_list->AddRect(box_min, box_max, theme_color_bright_u32, 4.0f, 0, 1.0f);
            draw_list->AddText(ImVec2(box_x + 8.0f, box_y + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), tooltip_text.c_str());

            if (show_imgui_tooltip) {
                ImGui::BeginTooltip();
                ImGui::TextColored(active_color, "%s", label.c_str());
                ImGui::Separator();
                ImGui::Text("Value: %s", val_str.c_str());
                ImGui::EndTooltip();
            }
        }
    }

    models::series::series_repository repository_;
    series_data current_data_;
    std::vector<series_data> datasets_;
    size_t selected_dataset_index_ = 0;

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::string last_action_{};
    std::string selected_range_{"All"};
    std::string last_exported_csv_path_{};
};

} // namespace rouen::cards
