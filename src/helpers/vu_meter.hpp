#pragma once

#include "imgui_include.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cmath>

namespace rouen::helpers::vu_meter {

enum class VUMeterScaleType {
    AudioVU,        // Standard Audio VU scale: -20, -10, -5, -3, 0, +1, +3 dB
    SignalStrength, // Tuning / Signal strength: 0 to 10 scale
    LinearPercent,  // 0% to 100% scale
    Decibel,        // dB scale: -60 to 0 dB
    Custom          // User-provided tick vector
};

enum class VUMeterTheme {
    VintageLitAmber,   // Warm incandescent glow, golden faceplate
    VintageUnlitSlate, // Unlit dark slate faceplate
    ModernDark,        // Dark high-contrast metallic faceplate
    SilverVintage,     // Brushed silver faceplate
    Custom             // Custom colors specified in VUMeterStyle
};

struct VUMeterTick {
    float norm_val = 0.0f; // Normalized [0.0, 1.0] position along sweep arc
    std::string label;     // Label text (e.g. "-20", "0", "100%")
    bool major = true;     // Major tick (longer line & text label) vs minor tick mark
    std::optional<ImU32> color = std::nullopt; // Custom color for this specific tick mark

    VUMeterTick() = default;
    VUMeterTick(float nv, std::string l, bool m = true, std::optional<ImU32> c = std::nullopt)
        : norm_val(nv), label(std::move(l)), major(m), color(c) {}
};

struct VUMeterStyle {
    VUMeterTheme theme = VUMeterTheme::VintageLitAmber;

    // Optional color overrides (if std::nullopt, defaults derived from theme)
    std::optional<ImU32> bg_color_top;
    std::optional<ImU32> bg_color_bottom;
    std::optional<ImU32> bezel_color;
    std::optional<ImU32> scale_norm_color;
    std::optional<ImU32> scale_overload_color;
    std::optional<ImU32> label_color;
    std::optional<ImU32> needle_color;
    std::optional<ImU32> pivot_color;

    bool draw_glass_reflection = true;
    bool show_peak_watermark = true;
    bool enable_dynamic_lighting = true;
    float overload_threshold = 0.75f; // Normalized value where overload red zone starts
};

struct VUMeterConfig {
    VUMeterScaleType scale_type = VUMeterScaleType::AudioVU;
    std::vector<VUMeterTick> custom_ticks;

    std::string title = "VU";                  // Main badge title (e.g., "VU", "SIGNAL", "MIC", "LEVEL")
    std::string left_channel_title = "LEFT";   // Title for left channel / single meter sub-label
    std::string right_channel_title = "RIGHT"; // Title for right channel
    bool show_titles = true;

    VUMeterStyle style;

    // Arc sweep geometry (defaults to standard 110-degree vintage meter)
    float start_angle_rad = -2.530727f; // -145 degrees
    float sweep_angle_rad = 1.919862f;  // 110 degrees
};

// Returns predefined tick mark sets for built-in scales
std::vector<VUMeterTick> get_preset_ticks(VUMeterScaleType type);

// Low-level drawing functions (require pre-calculated screen pos and size)
void draw_analog_dial(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size,
                      float value, float watermark = 0.0f,
                      const std::string& ch_label = "",
                      const VUMeterConfig& config = {});

void draw_stereo_analog_dial(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size,
                             float level_l, float level_r,
                             float watermark_l = 0.0f, float watermark_r = 0.0f,
                             const VUMeterConfig& config = {});

void draw_bar_meter(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size,
                    float level_l, float level_r = -1.0f,
                    float watermark_l = 0.0f, float watermark_r = 0.0f,
                    const VUMeterConfig& config = {});

// High-level ImGui convenience widgets (handles layout, cursor placement, and ImGui::Dummy)
void render_analog_dial(const ImVec2& size, float value, float watermark = 0.0f,
                        const std::string& ch_label = "",
                        const VUMeterConfig& config = {});

void render_stereo_analog_dial(const ImVec2& size, float level_l, float level_r,
                               float watermark_l = 0.0f, float watermark_r = 0.0f,
                               const VUMeterConfig& config = {});

void render_bar_meter(const ImVec2& size, float level_l, float level_r = -1.0f,
                      float watermark_l = 0.0f, float watermark_r = 0.0f,
                      const VUMeterConfig& config = {});

} // namespace rouen::helpers::vu_meter
