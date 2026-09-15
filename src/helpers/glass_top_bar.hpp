#pragma once

#include "imgui_include.hpp"
#include <functional>
#include <string_view>

namespace rouen::helpers {

class glass_top_bar {
public:
    struct style_config {
        ImVec4 glass_bg;
        ImVec4 glass_border;
        ImVec4 glass_glow;
        float padding_x;
        float padding_y;
        float rounding;
        bool draw_glow;

        style_config()
            : glass_bg(0.10f, 0.12f, 0.18f, 0.72f),
              glass_border(1.0f, 1.0f, 1.0f, 0.15f),
              glass_glow(0.40f, 0.65f, 1.00f, 0.08f),
              padding_x(8.0f),
              padding_y(6.0f),
              rounding(0.0f),
              draw_glow(true) {}
    };

    // Overload using default style_config
    static void render(
        std::string_view child_id,
        ImVec2 const& region_size,
        const std::function<void()>& render_top_controls_fn,
        const std::function<void()>& render_content_fn
    );

    // Overload using custom style_config
    static void render(
        std::string_view child_id,
        ImVec2 const& region_size,
        const std::function<void()>& render_top_controls_fn,
        const std::function<void()>& render_content_fn,
        const style_config& config
    );
};

} // namespace rouen::helpers
