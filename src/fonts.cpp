#include "fonts.hpp"

namespace rouen::fonts {
    void setup() {
        // Load font with Cyrillic support and symbols
        // Add default font with Cyrillic character range and geometric symbols
        static const ImWchar ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin Supplement
            0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
            0x2DE0, 0x2DFF, // Cyrillic Extended-A
            0xA640, 0xA69F, // Cyrillic Extended-B
            0x25A0, 0x25FF, // Geometric Shapes (includes triangles)
            0x2B00, 0x2BFF, // Miscellaneous Symbols and Arrows
            0,
        };
        auto & io = ImGui::GetIO();
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", base_size, NULL, ranges);
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", base_size, NULL, ranges);
    }

    ImFont* get_font(FontType type) {
        auto & io = ImGui::GetIO();
        if (type == FontType::Default) {
            return io.Fonts->Fonts[0];
        } else if (type == FontType::Mono) {
            return io.Fonts->Fonts[1];
        }
        return nullptr;
    }

    with_font::with_font(FontType type) {
        ImGui::PushFont(get_font(type));
    }

    with_font::~with_font() {
        ImGui::PopFont();
    }
}