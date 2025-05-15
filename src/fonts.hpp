#pragma once

// 1. Standard includes in alphabetic order
#include <string>

// 2. Libraries used in the project, in alphabetic order
#include "helpers/imgui_include.hpp"

// 3. All other includes
#include "../external/IconsMaterialDesign.h"

namespace rouen::fonts {
    // Font size in points (default ImGui is 13)
    constexpr float base_size = 14.0f;

    // The different font types available
    enum class FontType {
        Default,      // Default font
        Mono          // Monospace font
    };

    // Setup fonts for the application
    void setup();

    // Get a font by type
    ImFont* get_font(FontType type);

    // Check if a glyph/character is available in a given font
    bool is_glyph_available(ImWchar c, FontType type = FontType::Default);

    // Check if a UTF-8 character is available in a given font
    // This handles multi-byte UTF-8 characters correctly
    bool is_character_available(const char* utf8_char, FontType type = FontType::Default);

    // RAII class for using a font temporarily in a scope
    class with_font {
    public:
        explicit with_font(FontType type);
        ~with_font();

        // Disable copy and move
        with_font(const with_font&) = delete;
        with_font& operator=(const with_font&) = delete;
        with_font(with_font&&) = delete;
        with_font& operator=(with_font&&) = delete;
    };
}
