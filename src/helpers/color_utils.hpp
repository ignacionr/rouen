#pragma once

#include <algorithm>
#include <cmath>
#include "imgui_include.hpp"

namespace rouen::helpers::color {

    /// Calculate standard WCAG 2.1 relative luminance (0.0 to 1.0)
    inline float relative_luminance(const ImVec4& color) {
        auto to_linear = [](float c) -> float {
            c = std::clamp(c, 0.0f, 1.0f);
            return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
        };
        const float r = to_linear(color.x);
        const float g = to_linear(color.y);
        const float b = to_linear(color.z);
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    /// Calculate WCAG 2.1 contrast ratio between two colors (1.0 to 21.0)
    inline float contrast_ratio(const ImVec4& c1, const ImVec4& c2) {
        const float l1 = relative_luminance(c1);
        const float l2 = relative_luminance(c2);
        const float lighter = std::max(l1, l2);
        const float darker = std::min(l1, l2);
        return (lighter + 0.05f) / (darker + 0.05f);
    }

    /// Select optimal text color (dark vs light) that maximizes WCAG contrast on the given background
    inline ImVec4 pick_readable_text_color(
        const ImVec4& bg,
        const ImVec4& dark_text = ImVec4(0.06f, 0.07f, 0.09f, 1.0f),
        const ImVec4& light_text = ImVec4(0.96f, 0.97f, 0.99f, 1.0f)
    ) {
        const float dark_contrast = contrast_ratio(bg, dark_text);
        const float light_contrast = contrast_ratio(bg, light_text);
        return (dark_contrast >= light_contrast) ? dark_text : light_text;
    }

} // namespace rouen::helpers::color
