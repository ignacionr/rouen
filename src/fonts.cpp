#include <SDL3/SDL_video.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <algorithm>

#include "fonts.hpp"
#include "IconsMaterialDesign.h"
#include "helpers/debug.hpp"
#include "helpers/platform_utils.hpp"
#include "registrar.hpp"

namespace rouen::fonts {
    namespace {
        struct font_state {
            float last_dpi_scale = 1.0f;
            bool rebuild_requested = false;
        };

        static font_state g_font_state;

        struct FontPointers {
            ImFont* default_font = nullptr;
            ImFont* mono_font = nullptr;
            ImFont* bold_font = nullptr;
            ImFont* italic_font = nullptr;
        };

        FontPointers g_fonts;

        // Comprehensive glyph ranges including Basic Latin, Latin Supplement, Cyrillic (and extensions),
        // Geometric Shapes, General Punctuation, Misc Symbols & Dingbats, and Arrows.
        static const ImWchar full_glyph_ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin Supplement
            0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
            0x2DE0, 0x2DFF, // Cyrillic Extended-A
            0xA640, 0xA69F, // Cyrillic Extended-B
            0x25A0, 0x25FF, // Geometric Shapes (includes triangles)
            0x2000, 0x206F, // General Punctuation (includes special quotes and apostrophes)
            0x2600, 0x27BF, // Miscellaneous Symbols (Sun, Planets, Zodiac, Comets) & Dingbats
            0x2B00, 0x2BFF, // Miscellaneous Symbols and Arrows
            0,
        };

        // Material Design Icons glyph ranges
        static const ImWchar icon_glyph_ranges[] = {
            ICON_MIN_MD,
            static_cast<ImWchar>(0xFFFF), // Full range up to 0xFFFF
            0
        };

        float get_dpi_scale() {
            auto get_window = registrar::get<std::function<SDL_Window*()>>("get_window");
            if (get_window) {
                SDL_Window* window = (*get_window)();
                if (window) {
                    float content_scale = SDL_GetWindowDisplayScale(window);
                    if (content_scale > 0.0f) {
                        return std::max(1.0f, content_scale);
                    }
                }
            }
            return 1.0f;
        }

        std::filesystem::path find_font_file(const std::vector<std::string>& possible_names) {
            std::vector<std::filesystem::path> search_paths;
            
            auto exec_dir = rouen::platform::get_executable_directory();
            auto cwd = std::filesystem::current_path();

            search_paths.push_back(exec_dir);
            search_paths.push_back(exec_dir / "Resources");
            search_paths.push_back(exec_dir / "assets" / "fonts");
            search_paths.push_back(exec_dir / "fonts");
            search_paths.push_back(exec_dir / "external");
            search_paths.push_back(exec_dir / "external" / "fonts");
            search_paths.push_back(exec_dir / ".." / "Resources");
            search_paths.push_back(exec_dir / ".." / "external");
            search_paths.push_back(exec_dir / ".." / "external" / "fonts");
            search_paths.push_back(exec_dir / ".." / ".." / "external");

            search_paths.push_back(cwd);
            search_paths.push_back(cwd / "external");
            search_paths.push_back(cwd / "external" / "fonts");
            search_paths.push_back(cwd / "assets" / "fonts");
            search_paths.push_back(cwd / "fonts");
            search_paths.push_back(cwd / "Resources");

#if defined(__APPLE__)
            search_paths.push_back("/System/Library/Fonts");
            search_paths.push_back("/Library/Fonts");
            search_paths.push_back(std::filesystem::path(getenv("HOME") ? getenv("HOME") : "") / "Library/Fonts");
            search_paths.push_back("/System/Library/Fonts/Supplemental");
#elif defined(_WIN32)
            char win_dir[MAX_PATH];
            if (GetWindowsDirectoryA(win_dir, MAX_PATH)) {
                search_paths.push_back(std::filesystem::path(win_dir) / "Fonts");
            }
#else
            search_paths.push_back("/usr/share/fonts");
            search_paths.push_back("/usr/local/share/fonts");
            search_paths.push_back(std::filesystem::path(getenv("HOME") ? getenv("HOME") : "") / ".fonts");
            search_paths.push_back(std::filesystem::path(getenv("HOME") ? getenv("HOME") : "") / ".local/share/fonts");
#endif

            for (const auto& name : possible_names) {
                for (const auto& base_path : search_paths) {
                    if (!std::filesystem::exists(base_path)) continue;

                    std::filesystem::path direct_file = base_path / name;
                    if (std::filesystem::exists(direct_file) && std::filesystem::is_regular_file(direct_file)) {
                        return direct_file;
                    }

                    if (std::filesystem::is_directory(base_path)) {
                        try {
                            for (const auto& entry : std::filesystem::recursive_directory_iterator(base_path, std::filesystem::directory_options::skip_permission_denied)) {
                                if (entry.is_regular_file()) {
                                    std::string filename = entry.path().filename().string();
                                    std::string target_name = name;
                                    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                                    std::transform(target_name.begin(), target_name.end(), target_name.begin(), ::tolower);

                                    if (filename == target_name) {
                                        return entry.path();
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            DEBUG_WARN(std::format("Error searching for font in path {}: {}", base_path.string(), e.what()));
                        }
                    }
                }
            }
            return "";
        }

        ImFont* load_font_with_fallback(ImFontAtlas* io_fonts, const std::vector<std::string>& font_names, float size, const ImFontConfig* config = nullptr, const ImWchar* ranges = full_glyph_ranges) {
            std::filesystem::path font_path = find_font_file(font_names);
            if (!font_path.empty()) {
                DEBUG_INFO(std::format("Loading font from: {}", font_path.string()));
                ImFont* font = io_fonts->AddFontFromFileTTF(font_path.string().c_str(), size, config, ranges);
                if (font) return font;
            }
            DEBUG_WARN(std::format("Failed to load font from list, using default font. Size: {}", size));
            return nullptr;
        }

        void load_icon_font_ranges(ImFontAtlas* io_fonts, ImFont* main_font, float icon_size) {
            if (!main_font) return;

            ImFontConfig icon_config;
            icon_config.MergeMode = true;
            icon_config.PixelSnapH = true;
            icon_config.GlyphMinAdvanceX = icon_size;

            std::vector<std::string> icon_font_names = {
                "MaterialIcons-Regular.ttf",
                "MaterialIcons-Regular.otf",
                "MaterialIconsOutlined-Regular.otf",
                "MaterialIconsRound-Regular.otf",
                "MaterialIconsSharp-Regular.otf",
                "MaterialIconsTwoTone-Regular.otf"
            };

            std::filesystem::path icon_font_path = find_font_file(icon_font_names);
            if (!icon_font_path.empty()) {
                DEBUG_INFO(std::format("Loading Material Icons font from: {}", icon_font_path.string()));
                io_fonts->AddFontFromFileTTF(icon_font_path.string().c_str(), icon_size, &icon_config, icon_glyph_ranges);
            } else {
                DEBUG_WARN("Failed to load Material Icons font file. Icons may not render correctly.");
            }

            // Merge NotoSansSymbols-Regular.ttf if available
            std::vector<std::string> symbol_font_names = {
                "NotoSansSymbols-Regular.ttf",
                "NotoSansSymbols-Regular.otf"
            };
            std::filesystem::path symbol_font_path = find_font_file(symbol_font_names);
            if (!symbol_font_path.empty()) {
                ImFontConfig sym_config;
                sym_config.MergeMode = true;
                sym_config.PixelSnapH = true;
                static const ImWchar sym_ranges[] = { 0x2000, 0x2BFF, 0 };
                DEBUG_INFO(std::format("Loading Noto Sans Symbols font from: {}", symbol_font_path.string()));
                io_fonts->AddFontFromFileTTF(symbol_font_path.string().c_str(), icon_size, &sym_config, sym_ranges);
            }
        }
    }

    void setup() {
        DEBUG_INFO("Initializing fonts setup with DPI awareness, Cyrillic glyphs, and Material Icons...");

        ImGuiIO& io = ImGui::GetIO();
        ImFontAtlas* io_fonts = io.Fonts;

        io_fonts->Clear();

        float dpi_scale = get_dpi_scale();
        g_font_state.last_dpi_scale = dpi_scale;
        DEBUG_INFO(std::format("Current DPI scale: {}", dpi_scale));

        float font_size = base_size * dpi_scale;
        float icon_size = font_size * 0.9f;

        DEBUG_INFO(std::format("Base font size: {}, Scaled size: {}", base_size, font_size));

        ImFontConfig font_config;
        font_config.OversampleH = 3;
        font_config.OversampleV = 2;
        font_config.PixelSnapH = false;

        std::vector<std::string> default_font_names = {
            "Inter-Regular.ttf", "Inter-Regular.otf",
            "Roboto-Regular.ttf", "Roboto-Regular.otf",
            "SF-Pro-Text-Regular.otf", "SFProText-Regular.otf",
            "SegoeUI.ttf", "segoeui.ttf",
            "DejaVuSans.ttf", "LiberationSans-Regular.ttf",
            "Arial.ttf", "arial.ttf"
        };

        g_fonts.default_font = load_font_with_fallback(io_fonts, default_font_names, font_size, &font_config, full_glyph_ranges);

        if (!g_fonts.default_font) {
            DEBUG_INFO("Loading default ImGui font");
            ImFontConfig default_config;
            default_config.SizePixels = font_size;
            g_fonts.default_font = io_fonts->AddFontDefault(&default_config);
        }

        load_icon_font_ranges(io_fonts, g_fonts.default_font, icon_size);

        std::vector<std::string> mono_font_names = {
            "FiraCode-Regular.ttf", "FiraCode-Regular.otf",
            "JetBrainsMono-Regular.ttf", "JetBrainsMono-Regular.otf",
            "RobotoMono-Regular.ttf", "RobotoMono-Regular.otf",
            "SF-Mono-Regular.otf", "SFMono-Regular.otf",
            "CascadiaCode.ttf", "cascadiacode.ttf",
            "Consolas.ttf", "consolas.ttf",
            "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf",
            "Courier New.ttf", "cour.ttf"
        };

        g_fonts.mono_font = load_font_with_fallback(io_fonts, mono_font_names, font_size, &font_config, full_glyph_ranges);
        if (g_fonts.mono_font) {
            load_icon_font_ranges(io_fonts, g_fonts.mono_font, icon_size);
        } else {
            g_fonts.mono_font = g_fonts.default_font;
        }

        std::vector<std::string> bold_font_names = {
            "Inter-Bold.ttf", "Inter-Bold.otf",
            "Roboto-Bold.ttf", "Roboto-Bold.otf",
            "SF-Pro-Text-Bold.otf", "SFProText-Bold.otf",
            "SegoeUI-Bold.ttf", "segoeuib.ttf",
            "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf",
            "Arial-Bold.ttf", "arialbd.ttf"
        };

        g_fonts.bold_font = load_font_with_fallback(io_fonts, bold_font_names, font_size, &font_config, full_glyph_ranges);
        if (g_fonts.bold_font) {
            load_icon_font_ranges(io_fonts, g_fonts.bold_font, icon_size);
        } else {
            g_fonts.bold_font = g_fonts.default_font;
        }

        std::vector<std::string> italic_font_names = {
            "Inter-Italic.ttf", "Inter-Italic.otf",
            "Roboto-Italic.ttf", "Roboto-Italic.otf",
            "SF-Pro-Text-Italic.otf", "SFProText-Italic.otf",
            "SegoeUI-Italic.ttf", "segoeuii.ttf",
            "DejaVuSans-Oblique.ttf", "LiberationSans-Italic.ttf",
            "Arial-Italic.ttf", "ariali.ttf"
        };

        g_fonts.italic_font = load_font_with_fallback(io_fonts, italic_font_names, font_size, &font_config, full_glyph_ranges);
        if (g_fonts.italic_font) {
            load_icon_font_ranges(io_fonts, g_fonts.italic_font, icon_size);
        } else {
            g_fonts.italic_font = g_fonts.default_font;
        }

        io_fonts->Build();

        if (g_fonts.default_font) {
            io.FontDefault = g_fonts.default_font;
        }

        g_font_state.rebuild_requested = false;
        DEBUG_INFO("Font initialization complete");
    }

    void refresh_dpi() {
        float current_dpi_scale = get_dpi_scale();
        if (std::abs(current_dpi_scale - g_font_state.last_dpi_scale) > 0.05f) {
            DEBUG_INFO(std::format("DPI scale changed from {} to {}. Flagging for rebuild...", g_font_state.last_dpi_scale, current_dpi_scale));
            g_font_state.rebuild_requested = true;
        }
    }

    bool needs_font_rebuild() {
        return g_font_state.rebuild_requested;
    }

    void clear_font_rebuild_flag() {
        g_font_state.rebuild_requested = false;
    }

    ImFont* get_font(FontType type) {
        switch (type) {
            case FontType::Default: return g_fonts.default_font;
            case FontType::Mono:    return g_fonts.mono_font ? g_fonts.mono_font : g_fonts.default_font;
            case FontType::Bold:    return g_fonts.bold_font ? g_fonts.bold_font : g_fonts.default_font;
            case FontType::Italic:  return g_fonts.italic_font ? g_fonts.italic_font : g_fonts.default_font;
            default:                return g_fonts.default_font;
        }
    }

    bool is_glyph_available(ImWchar c, FontType type) {
        ImFont* font = get_font(type);
        if (!font) return false;
        return font->FindGlyphNoFallback(c) != nullptr;
    }

    bool is_character_available(const char* utf8_char, FontType type) {
        if (!utf8_char || !*utf8_char) return false;
        unsigned int codepoint = 0;
        int bytes = ImTextCharFromUtf8(&codepoint, utf8_char, nullptr);
        if (bytes <= 0 || codepoint == 0) return false;
        if (codepoint <= 0xFFFF) {
            bool const available = is_glyph_available(static_cast<ImWchar>(codepoint), type);
            if (!available) {
                DEBUG_WARN(std::format("Glyph U+{:04X} ({}) NOT available in font type {}", codepoint, std::string(utf8_char, static_cast<size_t>(bytes)), static_cast<int>(type)));
            }
            return available;
        }
        DEBUG_WARN(std::format("Codepoint U+{:06X} outside 16-bit BMP range", codepoint));
        return false;
    }

    with_font::with_font(FontType type) {
        ImGui::PushFont(get_font(type));
    }

    with_font::~with_font() {
        ImGui::PopFont();
    }
}
