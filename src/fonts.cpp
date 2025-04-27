#include <codecvt>
#include <locale>

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
            0x2000, 0x206F, // General Punctuation (includes special quotes and apostrophes)
            0,
        };
        
        // Setup Material Design Icons font
        // Use a smaller range that fits within ImWchar limits (unsigned short)
        static const ImWchar icon_ranges[] = { 
            ICON_MIN_MD, 
            static_cast<ImWchar>(0xFFFF), // Limit to what ImWchar can hold
            0 
        };
        
        auto & io = ImGui::GetIO();
        
        // First load regular fonts
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", base_size, NULL, ranges);
        
        // Create a merged font with icons
        ImFontConfig icons_config;
        icons_config.MergeMode = true;  // Set merge mode to true
        icons_config.PixelSnapH = true;
        icons_config.GlyphOffset = ImVec2(0, 2.0f); // Align icons with text
        
        // Merge Material Design Icons with the default font
        io.Fonts->AddFontFromFileTTF("external/MaterialIcons-Regular.ttf", base_size, &icons_config, icon_ranges);
        
        // Add monospace font
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", base_size, NULL, ranges);
        
        // Also merge Material Design Icons with the monospace font
        icons_config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF("external/MaterialIcons-Regular.ttf", base_size, &icons_config, icon_ranges);
    }

    ImFont* get_font(FontType type) {
        auto & io = ImGui::GetIO();
        if (type == FontType::Default) {
            return io.Fonts->Fonts[0]; // Default font merged with icons
        } else if (type == FontType::Mono) {
            return io.Fonts->Fonts[1]; // Monospace font merged with icons 
        }
        return nullptr;
    }

    bool is_glyph_available(ImWchar c, FontType type) {
        ImFont* font = get_font(type);
        if (font) {
            return font->FindGlyphNoFallback(c) != nullptr;
        }
        return false;
    }
    
    bool is_character_available(const char* utf8_char, FontType type) {
        // Convert UTF-8 character to Unicode code point
        try {
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
            std::u32string utf32 = converter.from_bytes(utf8_char);
            
            if (utf32.empty()) {
                return false;
            }
            
            // Check if the glyph is available for this code point
            return is_glyph_available(static_cast<ImWchar>(utf32[0]), type);
        } catch (const std::exception&) {
            return false;
        }
    }

    with_font::with_font(FontType type) {
        ImGui::PushFont(get_font(type));
    }

    with_font::~with_font() {
        ImGui::PopFont();
    }
}