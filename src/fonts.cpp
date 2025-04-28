#include <codecvt>
#include <locale>
#include <filesystem>
#include <iostream>
#include <vector>  // Added missing header for std::vector

#include "fonts.hpp"
#include "helpers/debug.hpp"  // For logging

namespace rouen::fonts {
    // Helper function to find font file
    std::string find_font_path(const std::string& filename, const std::vector<std::string>& search_paths) {
        for (const auto& base_path : search_paths) {
            std::filesystem::path full_path = std::filesystem::path(base_path) / filename;
            if (std::filesystem::exists(full_path)) {
                return full_path.string();
            }
        }
        return "";
    }
    
    // Helper to get OS-specific font paths
    std::vector<std::string> get_system_font_paths() {
        std::vector<std::string> paths;
        
        #ifdef __APPLE__
        // macOS font paths
        paths.push_back("/System/Library/Fonts/");
        paths.push_back("/Library/Fonts/");
        paths.push_back("/Users/ignaciorodriguez/Library/Fonts/");  // User fonts
        paths.push_back("/opt/homebrew/share/fonts/");              // Homebrew fonts location
        #else
        // Linux font paths
        paths.push_back("/usr/share/fonts/truetype/dejavu/");
        paths.push_back("/usr/share/fonts/TTF/");
        paths.push_back("/usr/local/share/fonts/");
        #endif
        
        return paths;
    }
    
    // Helper to get application directory for relative paths
    std::string get_application_directory() {
        try {
            return std::filesystem::current_path().string();
        } catch (const std::exception& e) {
            std::cerr << "Error getting current path: " << e.what() << std::endl;
            return ".";
        }
    }
    
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
        
        // Get font search paths based on the operating system
        auto font_paths = get_system_font_paths();
        auto app_path = get_application_directory();
        
        // Find default font on the system
        // First try DejaVu Sans, which might be installed on macOS via Homebrew
        std::string default_font_path = find_font_path("DejaVuSans.ttf", font_paths);
        
        // Fallback to system fonts if DejaVu is not found
        if (default_font_path.empty()) {
            #ifdef __APPLE__
            default_font_path = find_font_path("Arial.ttf", font_paths);
            if (default_font_path.empty()) {
                default_font_path = find_font_path("Helvetica.ttc", font_paths);
            }
            if (default_font_path.empty()) {
                default_font_path = find_font_path("SFNSText-Regular.ttf", font_paths);  // Modern macOS
            }
            #else
            // Additional Linux fallbacks if needed
            default_font_path = find_font_path("FreeSans.ttf", font_paths);
            #endif
        }
        
        // Find monospace font
        std::string mono_font_path = find_font_path("DejaVuSansMono.ttf", font_paths);
        
        // Fallback for monospace font
        if (mono_font_path.empty()) {
            #ifdef __APPLE__
            mono_font_path = find_font_path("Menlo.ttc", font_paths);
            if (mono_font_path.empty()) {
                mono_font_path = find_font_path("Courier.ttc", font_paths);
            }
            if (mono_font_path.empty()) {
                mono_font_path = find_font_path("SFMono-Regular.otf", font_paths);  // Modern macOS
            }
            #else
            // Additional Linux fallbacks if needed
            mono_font_path = find_font_path("FreeMono.ttf", font_paths);
            #endif
        }
        
        // Get path to Material Icons font
        std::vector<std::string> icon_search_paths = {
            app_path,                              // Current working directory
            app_path + "/external",                // /external subdirectory 
            app_path + "/../external",             // One level up, for running from build dir
            std::string(app_path + "/../../external") // Two levels up, alternative build layout
        };
        
        std::string material_icons_path = find_font_path("MaterialIcons-Regular.ttf", icon_search_paths);
        
        // Log font paths for debugging
        std::cout << "Default font path: " << default_font_path << std::endl;
        std::cout << "Monospace font path: " << mono_font_path << std::endl;
        std::cout << "Material icons font path: " << material_icons_path << std::endl;
        
        // Check if we found the fonts
        if (default_font_path.empty()) {
            std::cerr << "ERROR: Could not find a suitable default font!" << std::endl;
            // Use a fallback to ImGui's default embedded font
            // This will prevent the assertion failure but won't have all the glyphs
        } else {
            // First load regular fonts
            io.Fonts->AddFontFromFileTTF(default_font_path.c_str(), base_size, NULL, ranges);
        }
        
        // Create a merged font with icons if we found the icon font
        if (!material_icons_path.empty()) {
            ImFontConfig icons_config;
            icons_config.MergeMode = true;  // Set merge mode to true
            icons_config.PixelSnapH = true;
            icons_config.GlyphOffset = ImVec2(0, 2.0f); // Align icons with text
            
            // Merge Material Design Icons with the default font
            io.Fonts->AddFontFromFileTTF(material_icons_path.c_str(), base_size, &icons_config, icon_ranges);
        } else {
            std::cerr << "WARNING: Could not find Material Icons font!" << std::endl;
        }
        
        // Add monospace font if found
        if (!mono_font_path.empty()) {
            io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), base_size, NULL, ranges);
            
            // Also merge Material Design Icons with the monospace font if found
            if (!material_icons_path.empty()) {
                ImFontConfig icons_config;
                icons_config.MergeMode = true;
                icons_config.PixelSnapH = true;
                icons_config.GlyphOffset = ImVec2(0, 2.0f);
                io.Fonts->AddFontFromFileTTF(material_icons_path.c_str(), base_size, &icons_config, icon_ranges);
            }
        } else {
            std::cerr << "WARNING: Could not find a suitable monospace font!" << std::endl;
        }
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
        // Convert UTF-8 character to Unicode code point using modern C++
        if (!utf8_char || *utf8_char == '\0') {
            return false;
        }
        
        // Simple UTF-8 to codepoint conversion
        const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8_char);
        uint32_t codepoint = 0;
        
        // Decode UTF-8 sequence
        if ((*s & 0x80) == 0) {
            // 1-byte character
            codepoint = static_cast<uint32_t>(*s);
        } else if ((*s & 0xE0) == 0xC0 && *(s + 1) != 0) {
            // 2-byte character
            codepoint = ((static_cast<uint32_t>(*s & 0x1F)) << 6) | 
                        (static_cast<uint32_t>(*(s + 1) & 0x3F));
        } else if ((*s & 0xF0) == 0xE0 && *(s + 1) != 0 && *(s + 2) != 0) {
            // 3-byte character
            codepoint = ((static_cast<uint32_t>(*s & 0x0F)) << 12) | 
                        ((static_cast<uint32_t>(*(s + 1) & 0x3F)) << 6) | 
                        (static_cast<uint32_t>(*(s + 2) & 0x3F));
        } else if ((*s & 0xF8) == 0xF0 && *(s + 1) != 0 && *(s + 2) != 0 && *(s + 3) != 0) {
            // 4-byte character
            codepoint = ((static_cast<uint32_t>(*s & 0x07)) << 18) | 
                        ((static_cast<uint32_t>(*(s + 1) & 0x3F)) << 12) |
                        ((static_cast<uint32_t>(*(s + 2) & 0x3F)) << 6) | 
                        (static_cast<uint32_t>(*(s + 3) & 0x3F));
        } else {
            // Invalid UTF-8 sequence
            return false;
        }
        
        // Check if the glyph is available for this code point
        // ImWchar is typically 16 bits, so codepoints > 0xFFFF might need special handling
        if (codepoint <= 0xFFFF) {
            return is_glyph_available(static_cast<ImWchar>(codepoint), type);
        } else {
            // For code points beyond Basic Multilingual Plane (BMP)
            // Most emoji fall in this category
            return false; // ImGui typically doesn't support these by default
        }
    }

    with_font::with_font(FontType type) {
        ImGui::PushFont(get_font(type));
    }

    with_font::~with_font() {
        ImGui::PopFont();
    }
}