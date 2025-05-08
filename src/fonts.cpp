#include <codecvt>
#include <locale>
#include <filesystem>
#include <iostream>
#include <vector>  // Added missing header for std::vector
#include <sstream> // Added for std::stringstream
#include <iomanip> // Added for std::setw and std::setfill
#include <format>  // C++23 std::format

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
    
    // Chess symbols font specifically for chess symbols
    static ImFont* chess_symbols_font = nullptr;
    
    // Setup fonts with explicit chess symbols support
    void setup() {
        // Load font with Cyrillic support and symbols
        // Add default font with Cyrillic character range and geometric symbols
        static const ImWchar ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin Supplement
            0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
            0x2DE0, 0x2DFF, // Cyrillic Extended-A
            0xA640, 0xA69F, // Cyrillic Extended-B
            0x25A0, 0x25FF, // Geometric Shapes (includes triangles)
            0x2000, 0x206F, // General Punctuation (includes special quotes and apostrophes)
            0x2B00, 0x2BFF, // Miscellaneous Symbols and Arrows
            0x2654, 0x265F, // Chess symbols range - ADDED HERE to ensure they're included in all fonts
            0,
        };
        
        // Chess symbols specific range (making sure it's explicitly included)
        static const ImWchar chess_ranges[] = {
            0x2654, 0x265F, // Chess symbols range U+2654 to U+265F
            0,
        };
        
        // Setup Material Design Icons font
        // Use a smaller range that fits within ImWchar limits (unsigned short)
        static const ImWchar icon_ranges[] = { 
            ICON_MIN_MD, 
            static_cast<ImWchar>(0xFFFF), // Limit to what ImWchar can hold
            0 
        };
        
        // Add a dedicated log section for debugging the chess symbols
        std::cout << "---- Chess Symbols Font Setup ----" << std::endl;
        std::cout << "Defining chess symbol range: U+2654 to U+265F" << std::endl;
        
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
        
        // Direct path to the project's Noto Sans Symbols font which we know exists
        std::vector<std::string> symbol_font_search_paths = {
            app_path + "/external/fonts",                // In external/fonts subdirectory 
            app_path + "/../external/fonts",             // One level up, for running from build dir
            std::string(app_path + "/../../external/fonts"), // Two levels up, alternative build layout
        };
        
        // First directly try the bundled Noto Sans Symbols font which we know exists in the project
        std::string symbols_font_path = app_path + "/external/fonts/NotoSansSymbols-Regular.ttf";
        
        // Check if the direct path exists, otherwise search for it
        if (!std::filesystem::exists(symbols_font_path)) {
            symbols_font_path = find_font_path("NotoSansSymbols-Regular.ttf", symbol_font_search_paths);
            
            // If not found in external/fonts, look in broader locations
            if (symbols_font_path.empty()) {
                std::vector<std::string> fallback_paths = {
                    app_path,                              // Current working directory
                    app_path + "/external",                // /external subdirectory
                    app_path + "/../external",             // One level up
                    std::string(app_path + "/../../external"), // Two levels up
                    "/Library/Fonts/",                   // macOS system fonts
                    "/System/Library/Fonts/",            // More macOS system fonts
                    "/Users/ignaciorodriguez/Library/Fonts/", // User fonts
                    "/opt/homebrew/share/fonts/",        // Homebrew fonts location
                    "/usr/share/fonts/",                 // Linux font location
                };
                
                // Try to find Noto Sans Symbols
                symbols_font_path = find_font_path("NotoSansSymbols-Regular.ttf", fallback_paths);
                
                // If Noto Sans Symbols is still not found, try alternatives that have chess symbols
                if (symbols_font_path.empty()) {
                    symbols_font_path = find_font_path("DejaVuSans.ttf", fallback_paths);
                    if (symbols_font_path.empty()) {
                        symbols_font_path = find_font_path("Arial Unicode.ttf", fallback_paths);
                    }
                    if (symbols_font_path.empty()) {
                        symbols_font_path = find_font_path("Arial Unicode MS.ttf", fallback_paths);
                    }
                    if (symbols_font_path.empty()) {
                        symbols_font_path = find_font_path("AppleSymbols.ttf", fallback_paths);
                    }
                }
            }
        }
        
        // Log found font paths
        std::cout << "Default font path: " << default_font_path << std::endl;
        std::cout << "Monospace font path: " << mono_font_path << std::endl;
        std::cout << "Material icons font path: " << material_icons_path << std::endl;
        std::cout << "Symbols font path: " << symbols_font_path << std::endl;
        
        // Check if we found the fonts
        if (default_font_path.empty()) {
            std::cerr << "ERROR: Could not find a suitable default font!" << std::endl;
            // Use a fallback to ImGui's default embedded font
            // This will prevent the assertion failure but won't have all the glyphs
        } else {
            // First load regular fonts without merging chess symbols - note the NULL for ranges
            io.Fonts->AddFontFromFileTTF(default_font_path.c_str(), base_size, NULL, ranges);
        }
        
        // Add specialized font for chess symbols if available - as a separate font, not merged
        if (!symbols_font_path.empty()) {
            // Create a dedicated chess symbols font - NOT merging it
            ImFontConfig chess_config;
            chess_config.MergeMode = false; // We want a standalone font
            chess_config.PixelSnapH = true;
            chess_config.OversampleH = 4; // Higher quality for chess symbols
            chess_config.OversampleV = 4;
            chess_config.GlyphOffset = ImVec2(0, 0); // No offset for chess pieces
            strcpy(chess_config.Name, "Chess Symbols Font"); // Name the config for debugging
            
            // Log that we're loading the chess symbols font
            std::cout << "Loading dedicated chess symbols font: " << symbols_font_path << std::endl;
            
            // Add chess symbols font as a separate, dedicated font (not merged)
            // Using a larger size for better visibility and clarity
            chess_symbols_font = io.Fonts->AddFontFromFileTTF(symbols_font_path.c_str(), 
                                                            base_size * 1.2f, // Slightly larger for clarity
                                                            &chess_config, 
                                                            chess_ranges);
            
            if (chess_symbols_font) {
                std::cout << "Successfully loaded dedicated chess symbols font" << std::endl;
                
                // Debug: verify that the chess symbols are available in the font
                for (ImWchar c = 0x2654; c <= 0x265F; c++) {
                    bool has_glyph = chess_symbols_font->FindGlyphNoFallback(c) != nullptr;
                    std::cout << "Chess symbol U+" << std::hex << c << " available: " 
                              << (has_glyph ? "yes" : "no") << std::endl;
                    
                    // If the chess symbol is not available in the dedicated font, that's a problem
                    if (!has_glyph) {
                        std::cerr << "WARNING: Chess symbol U+" << std::hex << c 
                                  << " not found in dedicated chess font!" << std::endl;
                    }
                }
            } else {
                std::cerr << "ERROR: Failed to load chess symbols font!" << std::endl;
            }
        } else {
            std::cerr << "WARNING: Chess symbols font not found, chess pieces may not display correctly" << std::endl;
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
        
        // Make sure we don't return null fonts
        if (io.Fonts->Fonts.empty()) {
            return io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
        }
        
        switch (type) {
            case FontType::Default:
                return io.Fonts->Fonts[0]; // Default font merged with icons
            case FontType::Mono:
                // Return monospace font if available, otherwise fallback to default
                return (io.Fonts->Fonts.Size > 1) ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0];
            case FontType::ChessSymbols:
                // Return the dedicated chess symbols font if available
                return chess_symbols_font ? chess_symbols_font : io.Fonts->Fonts[0];
            default:
                return io.Fonts->Fonts[0];
        }
    }

    bool is_glyph_available(ImWchar c, FontType type) {
        ImFont* font = get_font(type);
        if (!font) {
            return false;
        }
        
        // Check if this is a chess symbol (U+2654 to U+265F)
        if (c >= 0x2654 && c <= 0x265F) {
            // For chess symbols, first try the dedicated chess font
            if (chess_symbols_font) {
                bool found = chess_symbols_font->FindGlyphNoFallback(c) != nullptr;
                if (found) {
                    return true;
                }
            }
            
            // If not found in chess font or no chess font, try all available fonts
            auto& io = ImGui::GetIO();
            for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
                if (io.Fonts->Fonts[i]->FindGlyphNoFallback(c) != nullptr) {
                    return true;
                }
            }
            return false;
        }
        
        // For non-chess symbols, check in the requested font
        return font->FindGlyphNoFallback(c) != nullptr;
    }
    
    // Helper function to convert UTF-8 string to Unicode codepoint
    char32_t utf8_to_codepoint(const char* utf8_char) {
        if (!utf8_char || *utf8_char == '\0') {
            return 0;
        }
        
        const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8_char);
        char32_t codepoint = 0;
        
        if ((*s & 0x80) == 0) {
            // 1-byte character
            codepoint = static_cast<char32_t>(*s);
        } else if ((*s & 0xE0) == 0xC0 && *(s + 1) != 0) {
            // 2-byte character
            codepoint = ((static_cast<char32_t>(*s & 0x1F)) << 6) | 
                      (static_cast<char32_t>(*(s + 1) & 0x3F));
        } else if ((*s & 0xF0) == 0xE0 && *(s + 1) != 0 && *(s + 2) != 0) {
            // 3-byte character (chess symbols are in this range)
            codepoint = ((static_cast<char32_t>(*s & 0x0F)) << 12) | 
                      ((static_cast<char32_t>(*(s + 1) & 0x3F)) << 6) | 
                      (static_cast<char32_t>(*(s + 2) & 0x3F));
        } else if ((*s & 0xF8) == 0xF0 && *(s + 1) != 0 && *(s + 2) != 0 && *(s + 3) != 0) {
            // 4-byte character
            codepoint = ((static_cast<char32_t>(*s & 0x07)) << 18) | 
                      ((static_cast<char32_t>(*(s + 1) & 0x3F)) << 12) |
                      ((static_cast<char32_t>(*(s + 2) & 0x3F)) << 6) | 
                      (static_cast<char32_t>(*(s + 3) & 0x3F));
        }
        
        return codepoint;
    }
    
    bool is_character_available(const char* utf8_char, FontType type) {
        // Return early if null or empty
        if (!utf8_char || *utf8_char == '\0') {
            return false;
        }
        
        // Use C++23 std::format for more concise debug logging
        SYS_DEBUG_FMT("Checking availability for UTF-8 character: {}", utf8_char);
        
        // Convert the UTF-8 string to a Unicode codepoint
        char32_t codepoint = utf8_to_codepoint(utf8_char);
        
        if (codepoint == 0) {
            SYS_WARN("Invalid or empty UTF-8 sequence");
            return false;
        }
        
        // Check if the codepoint is in the chess symbol range
        if (codepoint >= 0x2654 && codepoint <= 0x265F) {
            SYS_INFO_FMT("Detected chess symbol: U+{:04X}", static_cast<unsigned int>(codepoint));
            
            // Always use the chess symbols font for chess pieces
            if (chess_symbols_font) {
                bool available = chess_symbols_font->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr;
                SYS_INFO_FMT("Chess symbol U+{:04X} is {} available in chess font", 
                        static_cast<unsigned int>(codepoint), 
                        available ? "" : "NOT");
                
                if (available) {
                    return true;
                }
            }
            
            // If not found in chess font or no chess font exists, try the default font
            ImFont* default_font = get_font(FontType::Default);
            if (default_font) {
                bool available = default_font->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr;
                SYS_INFO_FMT("Chess symbol U+{:04X} is {} available in default font", 
                        static_cast<unsigned int>(codepoint), 
                        available ? "" : "NOT");
                
                if (available) {
                    return true;
                }
            }
            
            // As a last resort, check all available fonts
            auto& io = ImGui::GetIO();
            for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
                if (io.Fonts->Fonts[i]->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr) {
                    SYS_INFO_FMT("Chess symbol U+{:04X} found in font #{}", 
                            static_cast<unsigned int>(codepoint), i);
                    return true;
                }
            }
            
            SYS_WARN_FMT("Chess symbol U+{:04X} not available in any font", 
                    static_cast<unsigned int>(codepoint));
            return false;
        }
        
        // For non-chess symbols, check in the requested font
        if (codepoint <= 0xFFFF) {
            bool available = is_glyph_available(static_cast<ImWchar>(codepoint), type);
            SYS_DEBUG_FMT("Codepoint U+{:04X} available in requested font: {}", 
                    static_cast<unsigned int>(codepoint), available ? "yes" : "no");
            return available;
        } else {
            // For code points beyond Basic Multilingual Plane (BMP)
            SYS_INFO_FMT("Codepoint U+{:X} is beyond BMP, not supported by ImGui", 
                    static_cast<unsigned int>(codepoint));
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