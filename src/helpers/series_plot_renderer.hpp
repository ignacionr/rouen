#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../models/series/series_repository.hpp"
#include "imgui_include.hpp"

namespace rouen::helpers {

struct series_plot_config {
    std::string series_name;
    std::string unit;
    bool is_bar_chart{false};
    ImVec4 active_color{0.20f, 0.60f, 0.90f, 1.0f};
    bool show_imgui_tooltip{true};
    double low_limit{0.0};
    double high_limit{0.0};
};

class series_plot_renderer {
public:
    static void draw(
        ImDrawList* draw_list,
        ImVec2 canvas_pos,
        ImVec2 canvas_size,
        std::span<const models::series::data_point> points,
        int hovered_idx,
        series_plot_config const& cfg
    ) {
        if (!draw_list) {
            return;
        }
        float const margin_left = 55.0f;
        float const margin_right = 20.0f;
        float const margin_top = 20.0f;
        float const margin_bottom = 30.0f;

        ImVec2 const plot_pos = ImVec2(canvas_pos.x + margin_left, canvas_pos.y + margin_top);
        ImVec2 const plot_size = ImVec2(
            std::max(10.0f, canvas_size.x - margin_left - margin_right),
            std::max(10.0f, canvas_size.y - margin_top - margin_bottom));

        float min_val = 0.0f;
        float max_val = 0.0f;
        size_t const n = points.size();

        if (n > 0) {
            min_val = points[0].value;
            max_val = points[0].value;
            for (const auto& pt : points) {
                min_val = std::min(min_val, pt.value);
                max_val = std::max(max_val, pt.value);
            }
        }

        if (max_val - min_val < 0.0001f) {
            max_val += 1.0f;
            min_val -= 1.0f;
        }

        float y_min = 0.0f;
        if (cfg.series_name == "temps" || min_val < 0.0f) {
            float const diff = max_val - min_val;
            y_min = min_val - diff * 0.1f;
        }
        float const diff = max_val - y_min;
        float const y_max = max_val + diff * 0.1f;

        int const grid_lines = 4;
        for (int i = 0; i <= grid_lines; ++i) {
            float const ratio = static_cast<float>(i) / static_cast<float>(grid_lines);
            float const y = plot_pos.y + plot_size.y * (1.0f - ratio);
            float const val = y_min + ratio * (y_max - y_min);

            draw_list->AddLine(
                ImVec2(plot_pos.x, y),
                ImVec2(plot_pos.x + plot_size.x, y),
                ImGui::GetColorU32(ImGuiCol_Separator, 0.25f),
                1.0f
            );

            std::string label;
            if (cfg.unit == "$") {
                if (val >= 1000.0f) {
                    label = std::format("${:.1f}k", val / 1000.0f);
                } else {
                    label = std::format("${:.0f}", val);
                }
            } else {
                label = std::format("{:.1f}{}", val, cfg.unit == "C" ? "°C" : cfg.unit);
            }

            ImVec2 const label_size = ImGui::CalcTextSize(label.c_str());
            draw_list->AddText(
                ImVec2(plot_pos.x - label_size.x - 6.0f, y - label_size.y * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                label.c_str()
            );
        }

        // Draw optional industrial high/low limit lines
        if (cfg.high_limit > 0.0 && static_cast<float>(cfg.high_limit) >= y_min && static_cast<float>(cfg.high_limit) <= y_max) {
            float const ratio = (static_cast<float>(cfg.high_limit) - y_min) / (y_max - y_min);
            float const y = plot_pos.y + plot_size.y * (1.0f - ratio);
            draw_list->AddLine(
                ImVec2(plot_pos.x, y),
                ImVec2(plot_pos.x + plot_size.x, y),
                ImGui::ColorConvertFloat4ToU32(ImVec4{0.92f, 0.30f, 0.24f, 0.80f}),
                1.5f
            );
        }
        if (cfg.low_limit > 0.0 && static_cast<float>(cfg.low_limit) >= y_min && static_cast<float>(cfg.low_limit) <= y_max) {
            float const ratio = (static_cast<float>(cfg.low_limit) - y_min) / (y_max - y_min);
            float const y = plot_pos.y + plot_size.y * (1.0f - ratio);
            draw_list->AddLine(
                ImVec2(plot_pos.x, y),
                ImVec2(plot_pos.x + plot_size.x, y),
                ImGui::ColorConvertFloat4ToU32(ImVec4{0.95f, 0.72f, 0.20f, 0.80f}),
                1.5f
            );
        }

        ImU32 const theme_color_u32 = ImGui::ColorConvertFloat4ToU32(cfg.active_color);
        ImU32 const theme_color_trans_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(cfg.active_color.x, cfg.active_color.y, cfg.active_color.z, 0.15f));
        ImU32 const theme_color_bright_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::min(1.0f, cfg.active_color.x * 1.25f),
            std::min(1.0f, cfg.active_color.y * 1.25f),
            std::min(1.0f, cfg.active_color.z * 1.25f),
            1.0f
        ));

        if (n > 0) {
            if (cfg.is_bar_chart) {
                float const col_width = plot_size.x / static_cast<float>(n);
                float const bar_width = col_width * 0.7f;
                float const spacing = col_width * 0.3f;

                for (size_t i = 0; i < n; ++i) {
                    float const ratio = (points[i].value - y_min) / (y_max - y_min);
                    float const bar_height = plot_size.y * ratio;

                    float const x_left = plot_pos.x + static_cast<float>(i) * col_width + spacing * 0.5f;
                    float const x_right = x_left + bar_width;
                    float const y_top = plot_pos.y + plot_size.y - bar_height;
                    float const y_bottom = plot_pos.y + plot_size.y;

                    bool const is_bar_hovered = (static_cast<int>(i) == hovered_idx);
                    ImU32 const col_top = is_bar_hovered ? theme_color_bright_u32 : theme_color_u32;
                    ImU32 const col_bottom = ImGui::ColorConvertFloat4ToU32(ImVec4(cfg.active_color.x, cfg.active_color.y, cfg.active_color.z, 0.20f));

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
                            ImGui::ColorConvertFloat4ToU32(ImVec4(cfg.active_color.x, cfg.active_color.y, cfg.active_color.z, 0.40f)),
                            2.0f
                        );
                    }

                    std::string const x_label = points[i].label;
                    ImVec2 const label_size = ImGui::CalcTextSize(x_label.c_str());
                    float const label_x = x_left + bar_width * 0.5f - label_size.x * 0.5f;
                    draw_list->AddText(
                        ImVec2(label_x, plot_pos.y + plot_size.y + 6.0f),
                        is_bar_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        x_label.c_str()
                    );
                }
            } else {
                std::vector<ImVec2> line_pts(n);
                float const step = (n > 1) ? (plot_size.x / static_cast<float>(n - 1)) : plot_size.x;

                for (size_t i = 0; i < n; ++i) {
                    float const ratio = (points[i].value - y_min) / (y_max - y_min);
                    line_pts[i] = ImVec2(
                        plot_pos.x + static_cast<float>(i) * step,
                        plot_pos.y + plot_size.y * (1.0f - ratio)
                    );
                }

                if (n > 1) {
                    draw_list->PathClear();
                    draw_list->PathLineTo(ImVec2(line_pts[0].x, plot_pos.y + plot_size.y));
                    for (size_t i = 0; i < n; ++i) {
                        draw_list->PathLineTo(line_pts[i]);
                    }
                    draw_list->PathLineTo(ImVec2(line_pts[n - 1].x, plot_pos.y + plot_size.y));
                    draw_list->PathFillConvex(theme_color_trans_u32);
                }

                for (size_t i = 0; i < n - 1; ++i) {
                    draw_list->AddLine(line_pts[i], line_pts[i + 1], theme_color_u32, 2.5f);
                }

                for (size_t i = 0; i < n; ++i) {
                    bool const is_pt_hovered = (static_cast<int>(i) == hovered_idx);

                    draw_list->AddCircleFilled(line_pts[i], is_pt_hovered ? 6.0f : 4.0f, is_pt_hovered ? theme_color_bright_u32 : theme_color_u32);
                    draw_list->AddCircle(line_pts[i], is_pt_hovered ? 6.0f : 4.0f, ImGui::GetColorU32(ImGuiCol_WindowBg), 0, 1.5f);

                    std::string const x_label = points[i].label;
                    ImVec2 const label_size = ImGui::CalcTextSize(x_label.c_str());
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

        if (hovered_idx >= 0 && hovered_idx < static_cast<int>(n)) {
            float const col_width = plot_size.x / static_cast<float>(n);
            float const step = (n > 1) ? (plot_size.x / static_cast<float>(n - 1)) : plot_size.x;

            float const x_hover = cfg.is_bar_chart ?
                (plot_pos.x + static_cast<float>(hovered_idx) * col_width + col_width * 0.5f) :
                (plot_pos.x + static_cast<float>(hovered_idx) * step);

            size_t const h_idx = static_cast<size_t>(hovered_idx);
            float const ratio = (points[h_idx].value - y_min) / (y_max - y_min);
            float const y_hover = plot_pos.y + plot_size.y * (1.0f - ratio);

            draw_list->AddLine(
                ImVec2(x_hover, plot_pos.y),
                ImVec2(x_hover, plot_pos.y + plot_size.y),
                ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.4f),
                1.0f
            );

            std::string const label = points[h_idx].label;
            std::string val_str;
            if (cfg.unit == "$") {
                val_str = std::format("${:.1f}", points[h_idx].value);
            } else {
                val_str = std::format("{:.1f} {}", points[h_idx].value, cfg.unit == "C" ? "°C" : cfg.unit);
            }

            std::string const tooltip_text = std::format("{}: {}", label, val_str);

            ImVec2 const text_size = ImGui::CalcTextSize(tooltip_text.c_str());
            float const box_w = text_size.x + 16.0f;
            float const box_h = text_size.y + 8.0f;
            float box_x = x_hover - box_w * 0.5f;
            float box_y = y_hover - box_h - 10.0f;

            box_x = std::max(plot_pos.x, std::min(box_x, plot_pos.x + plot_size.x - box_w));
            box_y = std::max(plot_pos.y, std::min(box_y, plot_pos.y + plot_size.y - box_h));

            ImVec2 const box_min = ImVec2(box_x, box_y);
            ImVec2 const box_max = ImVec2(box_x + box_w, box_y + box_h);

            draw_list->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImGuiCol_PopupBg, 0.92f), 4.0f);
            draw_list->AddRect(box_min, box_max, theme_color_bright_u32, 4.0f, 0, 1.0f);
            draw_list->AddText(ImVec2(box_x + 8.0f, box_y + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), tooltip_text.c_str());

            if (cfg.show_imgui_tooltip) {
                ImGui::BeginTooltip();
                ImGui::TextColored(cfg.active_color, "%s", label.c_str());
                ImGui::Separator();
                ImGui::Text("Value: %s", val_str.c_str());
                ImGui::EndTooltip();
            }
        }
    }
};

} // namespace rouen::helpers
