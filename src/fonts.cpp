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
#include "helpers/platform_utils.hpp"  // For resource path utilities
#include "registrar.hpp"  // For accessing registered services
#include <SDL.h>  // For SDL DPI functions

namespace rouen::fonts {
    // Static variables to track DPI state
    static float last_dpi_scale = 1.0f;
    static bool fonts_need_rebuild = false;

    // Loaded font pointers — null means "not found / use default as fallback".
    static ImFont* s_default_font = nullptr;
    static ImFont* s_mono_font    = nullptr;
    static ImFont* s_bold_font    = nullptr;
    static ImFont* s_italic_font  = nullptr;
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
        paths.push_back("/System/Library/Fonts/Supplemental/");  // Arial, Georgia, Verdana variants
        paths.push_back("/Library/Fonts/");
        const char* home = std::getenv("HOME");
        if (home) {
            paths.push_back(std::format("{}/Library/Fonts/", home));
        }
        paths.push_back("/opt/homebrew/share/fonts/");              // Homebrew fonts location
        #elif defined(_WIN32)
        // Windows font paths
        paths.push_back("C:/Windows/Fonts/");
        // User fonts directory
        std::string userprofile = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
        if (!userprofile.empty()) {
            paths.push_back(userprofile + "/AppData/Local/Microsoft/Windows/Fonts/");
        }
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
            std::cerr << "Error getting current path: " << e.what() << '\n';
            return ".";
        }
    }
    
    // Setup fonts
    void setup() {
        auto & io = ImGui::GetIO();
        
        // Get DPI scale from ImGui's already configured display scale
        float dpi_scale = io.DisplayFramebufferScale.x;
        
        std::cout << "Font setup using DPI scale: " << dpi_scale << '\n';
        
        // Scale base font size for high-DPI displays
        float scaled_base_size = base_size * dpi_scale;
        
        // Scale ImGui style for consistent UI scaling
        ImGuiStyle& style = ImGui::GetStyle();
        // Only scale if we haven't already scaled before
        static bool style_scaled = false;
        if (!style_scaled && dpi_scale > 1.0f) {
            style.ScaleAllSizes(dpi_scale);
            style_scaled = true;
            std::cout << "Scaled ImGui style by factor: " << dpi_scale << '\n';
        }
        
        std::cout << "Using DPI scale: " << dpi_scale << ", Scaled font size: " << scaled_base_size << '\n';
        
        // Store the current DPI scale for change detection
        last_dpi_scale = dpi_scale;
        
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
             0,
        };
        
        // Setup Material Design Icons font
        // Use a smaller range that fits within ImWchar limits (unsigned short)
        static const ImWchar icon_ranges[] = { 
            ICON_MIN_MD, 
            static_cast<ImWchar>(0xFFFF), // Limit to what ImWchar can hold
            0 
        };
        
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
            if (mono_font_path.empty()) {
                mono_font_path = find_font_path("SFNSMono.ttf", font_paths);
            }
            #else
            // Additional Linux fallbacks if needed
            mono_font_path = find_font_path("FreeMono.ttf", font_paths);
            #endif
        }

        // Find bold font
        std::string bold_font_path = find_font_path("DejaVuSans-Bold.ttf", font_paths);
        if (bold_font_path.empty()) {
            #ifdef __APPLE__
            bold_font_path = find_font_path("Arial Bold.ttf", font_paths);
            if (bold_font_path.empty()) {
                bold_font_path = find_font_path("ArialBD.ttf", font_paths);
            }
            if (bold_font_path.empty()) {
                bold_font_path = find_font_path("Georgia Bold.ttf", font_paths);
            }
            #else
            bold_font_path = find_font_path("FreeSansBold.ttf", font_paths);
            if (bold_font_path.empty()) {
                bold_font_path = find_font_path("LiberationSans-Bold.ttf", font_paths);
            }
            #endif
        }

        // Find italic font
        std::string italic_font_path = find_font_path("DejaVuSans-Oblique.ttf", font_paths);
        if (italic_font_path.empty()) {
            #ifdef __APPLE__
            // SF NS Italic is available on all modern macOS versions.
            italic_font_path = find_font_path("SFNSItalic.ttf", font_paths);
            if (italic_font_path.empty()) {
                italic_font_path = find_font_path("Arial Italic.ttf", font_paths);
            }
            if (italic_font_path.empty()) {
                italic_font_path = find_font_path("ArialI.ttf", font_paths);
            }
            #else
            italic_font_path = find_font_path("FreeSansOblique.ttf", font_paths);
            if (italic_font_path.empty()) {
                italic_font_path = find_font_path("LiberationSans-Italic.ttf", font_paths);
            }
            #endif
        }
        
        // Get path to Material Icons font using the resource path utility
        std::filesystem::path material_icons_path = platform::get_resource_path("MaterialIcons-Regular.ttf", "");
        
        // Fallback to old method if not found
        if (!std::filesystem::exists(material_icons_path)) {
            std::vector<std::string> icon_search_paths = {
                app_path,                              // Current working directory
                app_path + "/external",                // /external subdirectory 
                app_path + "/../external",             // One level up, for running from build dir
                std::string(app_path + "/../../external") // Two levels up, alternative build layout
            };
            
            std::string fallback_path = find_font_path("MaterialIcons-Regular.ttf", icon_search_paths);
            if (!fallback_path.empty()) {
                material_icons_path = fallback_path;
            }
        }
        
        // Log found font paths
        std::cout << "Default font path: " << default_font_path << '\n';
        std::cout << "Monospace font path: " << mono_font_path << '\n';
        std::cout << "Bold font path: " << bold_font_path << '\n';
        std::cout << "Italic font path: " << italic_font_path << '\n';
        std::cout << "Material icons font path: " << material_icons_path << '\n';
        
        // Check if we found the fonts
        if (default_font_path.empty()) {
            std::cerr << "ERROR: Could not find a suitable default font!" << '\n';
            // Use a fallback to ImGui's default embedded font
            // This will prevent the assertion failure but won't have all the glyphs
        } else {
            // Load the default font first
            s_default_font = io.Fonts->AddFontFromFileTTF(default_font_path.c_str(), scaled_base_size, nullptr, ranges);
            
            // Then merge Material Design Icons with the default font
            if (!material_icons_path.empty()) {
                ImFontConfig icons_config;
                icons_config.MergeMode = true;  // Make sure merge mode is true
                icons_config.PixelSnapH = true;
                // Add vertical offset for better alignment with text
                icons_config.GlyphOffset = ImVec2(0, 2.5f * dpi_scale);
                icons_config.OversampleH = 3;
                icons_config.OversampleV = 3;
                strcpy(icons_config.Name, "Material Icons");
                
                io.Fonts->AddFontFromFileTTF(material_icons_path.string().c_str(), scaled_base_size, &icons_config, icon_ranges);
                std::cout << "Successfully merged Material Icons with default font" << '\n';
            } else {
                std::cerr << "WARNING: Could not find Material Icons font!" << '\n';
            }
        }
        
        // Add monospace font if found
        if (!mono_font_path.empty()) {
            s_mono_font = io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), scaled_base_size, nullptr, ranges);
            
            // Also merge Material Design Icons with the monospace font if found
            if (!material_icons_path.empty()) {
                ImFontConfig icons_config;
                icons_config.MergeMode = true;
                icons_config.PixelSnapH = true;
                icons_config.GlyphOffset = ImVec2(0, 2.5f * dpi_scale);
                icons_config.OversampleH = 3;
                icons_config.OversampleV = 3;
                strcpy(icons_config.Name, "Material Icons (Mono)");
                
                io.Fonts->AddFontFromFileTTF(material_icons_path.string().c_str(), scaled_base_size, &icons_config, icon_ranges);
                std::cout << "Successfully merged Material Icons with monospace font" << '\n';
            }
        } else {
            std::cerr << "WARNING: Could not find a suitable monospace font!" << '\n';
        }

        // Add bold font if found; null s_bold_font means callers fall back to default.
        if (!bold_font_path.empty()) {
            s_bold_font = io.Fonts->AddFontFromFileTTF(bold_font_path.c_str(), scaled_base_size, nullptr, ranges);
            std::cout << "Loaded bold font: " << bold_font_path << '\n';
        } else {
            std::cerr << "WARNING: Could not find a suitable bold font — markdown bold will use the default font.\n";
        }

        // Add italic font if found; null s_italic_font means callers fall back to default.
        if (!italic_font_path.empty()) {
            s_italic_font = io.Fonts->AddFontFromFileTTF(italic_font_path.c_str(), scaled_base_size, nullptr, ranges);
            std::cout << "Loaded italic font: " << italic_font_path << '\n';
        } else {
            std::cerr << "WARNING: Could not find a suitable italic font — markdown italic will use the default font.\n";
        }
        
        // Build the font atlas after loading all fonts
        io.Fonts->Build();
        
        // Clear any pending rebuild flag since we just rebuilt
        clear_font_rebuild_flag();
    }

    // Refresh DPI settings (useful when display configuration changes)
    void refresh_dpi() {
        auto & io = ImGui::GetIO();
        
        // Get the current renderer to query actual DPI information
        auto renderer_ptr = registrar::get<SDL_Renderer*>("main_renderer");
        SDL_Renderer* renderer = renderer_ptr ? *renderer_ptr : nullptr;
        
        float dpi_scale = 1.0f;
        
        if (renderer) {
            // Get the window from the renderer
            SDL_Window* window = SDL_RenderGetWindow(renderer);
            if (window) {
                // Get the window size in points (logical size)
                int window_w = 0, window_h = 0;
                SDL_GetWindowSize(window, &window_w, &window_h);
                
                // Get the drawable size in pixels (actual framebuffer size)
                int drawable_w = 0, drawable_h = 0;
                SDL_GetRendererOutputSize(renderer, &drawable_w, &drawable_h);
                
                // Calculate the actual DPI scale factor
                if (window_w > 0 && drawable_w > 0) {
                    dpi_scale = static_cast<float>(drawable_w) / static_cast<float>(window_w);
                    
                    // Update ImGui's display scale
                    io.DisplayFramebufferScale = ImVec2(dpi_scale, dpi_scale);
                    
                    std::cout << "Refreshed DPI scale: " << dpi_scale << 
                                 " (window: " << window_w << "x" << window_h << 
                                 ", drawable: " << drawable_w << "x" << drawable_h << ")" << '\n';
                    
                    // Check if DPI scale has changed significantly
                    if (std::abs(dpi_scale - last_dpi_scale) > 0.1f) {
                        fonts_need_rebuild = true;
                        std::cout << "DPI scale changed from " << last_dpi_scale << " to " << dpi_scale << 
                                     ", fonts need rebuild" << '\n';
                    }
                }
            }
        }
    }

    // Check if fonts need to be rebuilt due to DPI changes
    bool needs_font_rebuild() {
        return fonts_need_rebuild;
    }

    // Clear the font rebuild flag (called after fonts are rebuilt)
    void clear_font_rebuild_flag() {
        fonts_need_rebuild = false;
    }

    ImFont* get_font(FontType type) {
        auto & io = ImGui::GetIO();
        
        if (io.Fonts->Fonts.empty()) {
            return nullptr;
        }

        // Helper: return the font pointer if non-null, else the default, else Fonts[0].
        auto fallback = [&](ImFont* f) -> ImFont* {
            if (f) return f;
            if (s_default_font) return s_default_font;
            return io.Fonts->Fonts[0];
        };
        
        switch (type) {
            case FontType::Default: return fallback(s_default_font);
            case FontType::Mono:    return fallback(s_mono_font);
            case FontType::Bold:    return fallback(s_bold_font);
            case FontType::Italic:  return fallback(s_italic_font);
            default:                return fallback(s_default_font);
        }
    }

    bool is_glyph_available(ImWchar c, FontType type) {
        ImFont* font = get_font(type);
        if (!font) {
            return false;
        }
        
        // Check in the requested font
        return font->FindGlyphNoFallback(c) != nullptr;
    }
    
    // Helper function to convert UTF-8 string to Unicode codepoint
    char32_t utf8_to_codepoint(const char* utf8_char) {
        if (!utf8_char || *utf8_char == '\0') {
            return 0;
        }
        
        const auto* s = reinterpret_cast<const unsigned char*>(utf8_char);
        char32_t codepoint = 0;
        
        if ((*s & 0x80) == 0) {
            // 1-byte character
            codepoint = static_cast<char32_t>(*s);
        } else if ((*s & 0xE0) == 0xC0 && *(s + 1) != 0) {
            // 2-byte character
            codepoint = ((static_cast<char32_t>(*s & 0x1F)) << 6) | 
                      (static_cast<char32_t>(*(s + 1) & 0x3F));
        } else if ((*s & 0xF0) == 0xE0 && *(s + 1) != 0 && *(s + 2) != 0) {
            // 3-byte character
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
        
        // For standard codepoints, check in the requested font
        if (codepoint <= 0xFFFF) {
            bool available = is_glyph_available(static_cast<ImWchar>(codepoint), type);
            SYS_DEBUG_FMT("Codepoint U+{:04X} available in requested font: {}", 
                    static_cast<unsigned int>(codepoint), available ? "yes" : "no");
            return available;
        }             // For code points beyond Basic Multilingual Plane (BMP)
            SYS_INFO_FMT("Codepoint U+{:X} is beyond BMP, not supported by ImGui", 
                    static_cast<unsigned int>(codepoint));
            return false;
       
    }

    with_font::with_font(FontType type) {
        ImGui::PushFont(get_font(type));
    }

    with_font::~with_font() {
        ImGui::PopFont();
    }
}
