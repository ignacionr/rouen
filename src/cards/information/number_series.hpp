#pragma once

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class number_series_card : public card {
public:
    struct data_point {
        std::string label;
        float value;

        struct glaze {
            using T = data_point;
            static constexpr auto value = glz::object(
                "label", &T::label,
                "value", &T::value
            );
        };
    };

    struct series_data {
        std::string name;
        std::string title;
        std::string unit;
        std::vector<data_point> points;
        bool is_bar_chart = true;
        int color_index = 0;

        struct glaze {
            using T = series_data;
            static constexpr auto value = glz::object(
                "name", &T::name,
                "title", &T::title,
                "unit", &T::unit,
                "points", &T::points,
                "is_bar_chart", &T::is_bar_chart,
                "color_index", &T::color_index
            );
        };
    };

    explicit number_series_card(std::string_view locator = {}) {
        // Initialize default datasets
        datasets_ = {
            get_default_sales(),
            get_default_temps(),
            get_default_cpu()
        };

        if (locator.starts_with('{')) {
            series_data custom_data{};
            auto result = glz::read_json(custom_data, locator);
            if (!result) {
                if (custom_data.name.empty()) {
                    custom_data.name = "custom";
                }
                datasets_.push_back(custom_data);
                selected_dataset_index_ = datasets_.size() - 1;
            } else {
                selected_dataset_index_ = 0;
            }
        } else if (locator == "sales") {
            selected_dataset_index_ = 0;
        } else if (locator == "temps") {
            selected_dataset_index_ = 1;
        } else if (locator == "cpu") {
            selected_dataset_index_ = 2;
        } else {
            selected_dataset_index_ = 0;
        }

        current_data_ = datasets_[selected_dataset_index_];
        
        colors[0] = {0.20f, 0.43f, 0.70f, 1.0f};
        colors[1] = {0.14f, 0.32f, 0.55f, 0.75f};
        name(current_data_.title);
        width = 500.0f;
    }

    std::string get_uri() const override {
        return std::format("number-series:{}", current_data_.name);
    }

    bool render() override {
        return render_window([this]() {
            // Dropdown to select preset dataset
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("Preset Dataset", current_data_.title.c_str())) {
                for (size_t i = 0; i < datasets_.size(); ++i) {
                    bool is_selected = (i == selected_dataset_index_);
                    if (ImGui::Selectable(datasets_[i].title.c_str(), is_selected)) {
                        // Save current data modifications
                        datasets_[selected_dataset_index_] = current_data_;
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
            ImGui::Checkbox("Bar Chart", &current_data_.is_bar_chart);

            ImGui::SameLine();

            // Color Selector from Theme Colors
            ImGui::SetNextItemWidth(110.0f);
            static const char* color_names[] = {
                "Accent (0)", "Secondary (1)", "Red/Error (2)", "Green/Success (3)",
                "Warning (4)", "Info (5)", "Purple (6)", "Pink (7)", "Orange (8)", "Gray (9)"
            };
            if (ImGui::Combo("Color", &current_data_.color_index, color_names, IM_ARRAYSIZE(color_names))) {
                // Color changed
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
            ImVec4 active_color = get_color(static_cast<size_t>(current_data_.color_index), ImVec4{0.2f, 0.6f, 0.9f, 1.0f});
            ImU32 theme_color_u32 = ImGui::ColorConvertFloat4ToU32(active_color);
            ImU32 theme_color_trans_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(active_color.x, active_color.y, active_color.z, 0.15f));
            ImU32 theme_color_bright_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(
                std::min(1.0f, active_color.x * 1.25f),
                std::min(1.0f, active_color.y * 1.25f),
                std::min(1.0f, active_color.z * 1.25f),
                1.0f
            ));

            // Hover detection
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            bool is_hovering = ImGui::IsMouseHoveringRect(plot_pos, ImVec2(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y));
            int hovered_idx = -1;

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

                // Standard ImGui tooltip
                ImGui::BeginTooltip();
                ImGui::TextColored(active_color, "%s", label.c_str());
                ImGui::Separator();
                ImGui::Text("Value: %s", val_str.c_str());
                ImGui::EndTooltip();
            }

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

            ImGui::Dummy(ImVec2(0.0f, 5.0f));

            // Edit Data Section
            if (ImGui::CollapsingHeader("Edit Data Points")) {
                ImGui::BeginChild("EditList", ImVec2(0.0f, 150.0f), true);

                int delete_idx = -1;
                for (size_t i = 0; i < current_data_.points.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));

                    char name_buf[64];
                    strncpy(name_buf, current_data_.points[i].label.c_str(), sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::InputText("##LabelInput", name_buf, sizeof(name_buf))) {
                        current_data_.points[i].label = name_buf;
                    }

                    ImGui::SameLine();

                    float val = current_data_.points[i].value;
                    ImGui::SetNextItemWidth(180.0f);

                    float range_min = (current_data_.name == "temps") ? -20.0f : 0.0f;
                    float range_max = (current_data_.name == "cpu") ? 100.0f : 
                                      (current_data_.name == "temps") ? 50.0f : 100000.0f;

                    if (ImGui::SliderFloat("##ValueInput", &val, range_min, range_max, "%.1f")) {
                        current_data_.points[i].value = val;
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
                }
            }
        });
    }

private:
    series_data current_data_;
    std::vector<series_data> datasets_;
    size_t selected_dataset_index_ = 0;

    static series_data get_default_sales() {
        return {
            "sales",
            "Monthly Sales Revenue",
            "$",
            {
                {"Jan", 12000.0f},
                {"Feb", 15000.0f},
                {"Mar", 14000.0f},
                {"Apr", 18000.0f},
                {"May", 22000.0f},
                {"Jun", 25000.0f},
                {"Jul", 23000.0f},
                {"Aug", 21000.0f},
                {"Sep", 26000.0f},
                {"Oct", 30000.0f},
                {"Nov", 35000.0f},
                {"Dec", 42000.0f}
            },
            true, // Default bar chart
            0     // Accent
        };
    }

    static series_data get_default_temps() {
        return {
            "temps",
            "Weekly Temperature Forecast",
            "C",
            {
                {"Mon", 18.5f},
                {"Tue", 19.0f},
                {"Wed", 21.0f},
                {"Thu", 20.5f},
                {"Fri", 23.0f},
                {"Sat", 25.5f},
                {"Sun", 24.0f}
            },
            false, // Default line chart
            8      // Orange
        };
    }

    static series_data get_default_cpu() {
        return {
            "cpu",
            "System CPU Load",
            "%",
            {
                {"10s ago", 12.0f},
                {"9s ago", 18.5f},
                {"8s ago", 25.0f},
                {"7s ago", 15.0f},
                {"6s ago", 30.0f},
                {"5s ago", 45.5f},
                {"4s ago", 60.0f},
                {"3s ago", 35.0f},
                {"2s ago", 20.0f},
                {"1s ago", 10.0f},
                {"now", 5.0f}
            },
            false, // Default line chart
            6      // Purple
        };
    }
};

} // namespace rouen::cards
