#pragma once

#include <imgui/imgui.h>

namespace rouen::fonts {
    enum class FontType {
        Default,
        Mono
    };

    auto constexpr base_size { 15.0f };
    void setup();
    ImFont* get_font(FontType type);
    
    struct with_font {
        with_font(FontType type);
        ~with_font();
    };
}