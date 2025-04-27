#pragma once

// 1. Standard includes in alphabetic order
#include <string>

// 2. Libraries used in the project, in alphabetic order
#include <imgui/imgui.h>

// 3. All other includes
#include "../external/IconsMaterialDesign.h"

namespace rouen::fonts {
    enum class FontType {
        Default,
        Mono
    };

    auto constexpr base_size { 15.0f };
    void setup();
    ImFont* get_font(FontType type);

    // Check if a Unicode code point is available in the font
    bool is_glyph_available(ImWchar c, FontType type = FontType::Default);
    
    // Utility to check if a character is available in the font
    bool is_character_available(const char* utf8_char, FontType type = FontType::Default);
    
    struct with_font {
        with_font(FontType type);
        ~with_font();
    };
}