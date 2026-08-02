#include "vu_meter.hpp"
#include "registrar.hpp"
#include "texture_helper.hpp"
#include "texture_utils.hpp"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <imgui.h>
#include <map>
#include <mutex>
#include <cmath>
#include <vector>

namespace rouen::helpers::vu_meter {

struct TextureKey {
    VUMeterTheme theme;
    int width;
    int height;
    bool operator<(const TextureKey& o) const {
        if (theme != o.theme) return theme < o.theme;
        if (width != o.width) return width < o.width;
        return height < o.height;
    }
};

static std::mutex s_bg_cache_mutex;
static std::map<TextureKey, RouenGPUTexture*> s_bg_texture_cache;

static RouenGPUTexture* get_cached_faceplate_texture(VUMeterTheme theme, int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;

    // Round width and height to nearest multiple of 16 to avoid cache thrashing/recreation from sub-pixel jitter
    int const rounded_w = ((w + 15) / 16) * 16;
    int const rounded_h = ((h + 15) / 16) * 16;

    SDL_GPUDevice* device = TextureHelper::g_gpu_device;
    if (!device) {
        try {
            auto r_ptr = ::registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (r_ptr && *r_ptr) device = *r_ptr;
        } catch (...) {}
    }
    if (!device) return nullptr;

    std::lock_guard<std::mutex> const lock(s_bg_cache_mutex);
    TextureKey const key{ theme, rounded_w, rounded_h };
    auto it = s_bg_texture_cache.find(key);
    if (it != s_bg_texture_cache.end()) {
        return it->second;
    }

    // Generate procedural faceplate background surface
    SDL_Surface* surface = SDL_CreateSurface(rounded_w, rounded_h, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;

    Uint32* pixels = static_cast<Uint32*>(surface->pixels);
    int const pitch_px = surface->pitch / 4;

    for (int y = 0; y < rounded_h; ++y) {
        float const ny = static_cast<float>(y) / static_cast<float>(rounded_h);
        for (int x = 0; x < rounded_w; ++x) {
            float const nx = static_cast<float>(x) / static_cast<float>(rounded_w);

            Uint8 r = 240, g = 230, b = 200, a = 255;
            switch (theme) {
                case VUMeterTheme::VintageLitAmber: {
                    float const dist = std::sqrt((nx - 0.5f) * (nx - 0.5f) + (ny - 0.35f) * (ny - 0.35f));
                    float const vig = std::clamp(1.0f - dist * 0.40f, 0.0f, 1.0f);
                    r = static_cast<Uint8>(std::clamp((255.0f * (1.0f - ny * 0.12f)) * vig, 0.0f, 255.0f));
                    g = static_cast<Uint8>(std::clamp((242.0f * (1.0f - ny * 0.18f)) * vig, 0.0f, 255.0f));
                    b = static_cast<Uint8>(std::clamp((200.0f * (1.0f - ny * 0.25f)) * vig, 0.0f, 255.0f));
                    break;
                }
                case VUMeterTheme::VintageUnlitSlate: {
                    r = static_cast<Uint8>(std::clamp(38.0f * (1.0f - ny * 0.3f), 0.0f, 255.0f));
                    g = static_cast<Uint8>(std::clamp(42.0f * (1.0f - ny * 0.3f), 0.0f, 255.0f));
                    b = static_cast<Uint8>(std::clamp(48.0f * (1.0f - ny * 0.3f), 0.0f, 255.0f));
                    break;
                }
                case VUMeterTheme::ModernDark: {
                    float const dist = std::sqrt((nx - 0.5f) * (nx - 0.5f) + (ny - 0.5f) * (ny - 0.5f));
                    float const vig = std::clamp(1.0f - dist * 0.55f, 0.0f, 1.0f);
                    r = static_cast<Uint8>(std::clamp(18.0f * vig, 0.0f, 255.0f));
                    g = static_cast<Uint8>(std::clamp(20.0f * vig, 0.0f, 255.0f));
                    b = static_cast<Uint8>(std::clamp(26.0f * vig, 0.0f, 255.0f));
                    break;
                }
                case VUMeterTheme::SilverVintage: {
                    r = static_cast<Uint8>(std::clamp(240.0f - ny * 35.0f, 0.0f, 255.0f));
                    g = static_cast<Uint8>(std::clamp(242.0f - ny * 35.0f, 0.0f, 255.0f));
                    b = static_cast<Uint8>(std::clamp(245.0f - ny * 35.0f, 0.0f, 255.0f));
                    break;
                }
                case VUMeterTheme::Custom:
                default:
                    r = 220; g = 220; b = 220;
                    break;
            }
            pixels[y * pitch_px + x] = static_cast<Uint32>((a << 24) | (b << 16) | (g << 8) | r);
        }
    }

    RouenGPUTexture* tex = TextureHelper::createTextureFromSurface(device, surface);
    SDL_DestroySurface(surface);

    if (tex) {
        s_bg_texture_cache[key] = tex;
    }
    return tex;
}

std::vector<VUMeterTick> get_preset_ticks(VUMeterScaleType type) {
    switch (type) {
        case VUMeterScaleType::AudioVU:
            return {
                { 0.00f, "-20", true },
                { 0.28f, "-10", true },
                { 0.54f, "-5",  true },
                { 0.64f, "-3",  false },
                { 0.75f, "0",   true },
                { 0.83f, "+1",  false },
                { 1.00f, "+3",  true }
            };

        case VUMeterScaleType::SignalStrength:
            return {
                { 0.0f, "0",  true },
                { 0.2f, "2",  true },
                { 0.4f, "4",  true },
                { 0.6f, "6",  true },
                { 0.8f, "8",  true },
                { 1.0f, "10", true }
            };

        case VUMeterScaleType::LinearPercent:
            return {
                { 0.00f, "0%",   true },
                { 0.25f, "25",   false },
                { 0.50f, "50%",  true },
                { 0.75f, "75",   false },
                { 1.00f, "100%", true }
            };

        case VUMeterScaleType::Decibel:
            return {
                { 0.00f, "-60", true },
                { 0.33f, "-40", true },
                { 0.66f, "-20", true },
                { 0.83f, "-6",  true },
                { 1.00f, "0",   true }
            };

        case VUMeterScaleType::Custom:
        default:
            return {};
    }
}

void draw_analog_dial(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size,
                      float value, float watermark,
                      const std::string& ch_label,
                      const VUMeterConfig& config) {
    if (!draw_list || size.x <= 0.0f || size.y <= 0.0f) return;

    ImVec2 const min_p = pos;
    ImVec2 const max_p = ImVec2(pos.x + size.x, pos.y + size.y);

    // Determine colors based on theme & style overrides
    ImU32 bg_top, bg_bottom, bezel_col, scale_norm_col, scale_overload_col, label_col, needle_col, pivot_col;

    switch (config.style.theme) {
        case VUMeterTheme::VintageLitAmber:
            bg_top             = IM_COL32(255, 244, 205, 255);
            bg_bottom          = IM_COL32(235, 212, 150, 255);
            bezel_col          = IM_COL32(24, 26, 32, 255);
            scale_norm_col     = IM_COL32(25, 25, 25, 240);
            scale_overload_col = IM_COL32(215, 38, 38, 255);
            label_col          = IM_COL32(40, 40, 40, 220);
            needle_col         = IM_COL32(15, 15, 15, 255);
            pivot_col          = IM_COL32(30, 32, 38, 255);
            break;

        case VUMeterTheme::VintageUnlitSlate:
            bg_top             = IM_COL32(38, 42, 48, 255);
            bg_bottom          = IM_COL32(26, 28, 33, 255);
            bezel_col          = IM_COL32(20, 22, 26, 255);
            scale_norm_col     = IM_COL32(110, 115, 125, 180);
            scale_overload_col = IM_COL32(130, 50, 50, 180);
            label_col          = IM_COL32(110, 115, 125, 160);
            needle_col         = IM_COL32(75, 80, 90, 220);
            pivot_col          = IM_COL32(30, 32, 38, 255);
            break;

        case VUMeterTheme::ModernDark:
            bg_top             = IM_COL32(18, 20, 26, 255);
            bg_bottom          = IM_COL32(12, 14, 18, 255);
            bezel_col          = IM_COL32(40, 44, 52, 255);
            scale_norm_col     = IM_COL32(0, 210, 255, 220);
            scale_overload_col = IM_COL32(255, 50, 80, 240);
            label_col          = IM_COL32(200, 210, 225, 220);
            needle_col         = IM_COL32(255, 220, 0, 255);
            pivot_col          = IM_COL32(20, 22, 28, 255);
            break;

        case VUMeterTheme::SilverVintage:
            bg_top             = IM_COL32(240, 242, 245, 255);
            bg_bottom          = IM_COL32(205, 210, 218, 255);
            bezel_col          = IM_COL32(60, 65, 75, 255);
            scale_norm_col     = IM_COL32(20, 25, 30, 240);
            scale_overload_col = IM_COL32(200, 30, 30, 255);
            label_col          = IM_COL32(30, 35, 45, 230);
            needle_col         = IM_COL32(10, 10, 10, 255);
            pivot_col          = IM_COL32(40, 45, 55, 255);
            break;

        case VUMeterTheme::Custom:
        default:
            bg_top             = IM_COL32(240, 240, 240, 255);
            bg_bottom          = IM_COL32(200, 200, 200, 255);
            bezel_col          = IM_COL32(30, 30, 30, 255);
            scale_norm_col     = IM_COL32(30, 30, 30, 240);
            scale_overload_col = IM_COL32(220, 30, 30, 255);
            label_col          = IM_COL32(50, 50, 50, 220);
            needle_col         = IM_COL32(20, 20, 20, 255);
            pivot_col          = IM_COL32(30, 30, 30, 255);
            break;
    }

    if (config.style.bg_color_top) bg_top = *config.style.bg_color_top;
    if (config.style.bg_color_bottom) bg_bottom = *config.style.bg_color_bottom;
    if (config.style.bezel_color) bezel_col = *config.style.bezel_color;
    if (config.style.scale_norm_color) scale_norm_col = *config.style.scale_norm_color;
    if (config.style.scale_overload_color) scale_overload_col = *config.style.scale_overload_color;
    if (config.style.label_color) label_col = *config.style.label_color;
    if (config.style.needle_color) needle_col = *config.style.needle_color;
    if (config.style.pivot_color) pivot_col = *config.style.pivot_color;

    // 1. Frame Bezel
    draw_list->AddRectFilled(min_p, max_p, bezel_col, 4.0f);

    // 2. Faceplate Background: Use cached background texture if available
    ImVec2 const inner_min(min_p.x + 2.0f, min_p.y + 2.0f);
    ImVec2 const inner_max(max_p.x - 2.0f, max_p.y - 2.0f);
    draw_list->PushClipRect(inner_min, inner_max, true);

    int const inner_w = static_cast<int>(inner_max.x - inner_min.x);
    int const inner_h = static_cast<int>(inner_max.y - inner_min.y);

    RouenGPUTexture* bg_tex = get_cached_faceplate_texture(config.style.theme, inner_w, inner_h);
    if (bg_tex) {
        ImTextureID const tex_id = rouen::helpers::texture_id_cast(bg_tex);
        draw_list->AddImage(tex_id, inner_min, inner_max);
    } else {
        draw_list->AddRectFilledMultiColor(
            inner_min, inner_max,
            bg_top, bg_top, bg_bottom, bg_bottom
        );
    }

    // Dynamic Lighting Effect (Sinusoidal + Exponential response)
    if (config.style.enable_dynamic_lighting && config.style.theme != VUMeterTheme::VintageUnlitSlate) {
        float const time_sec = static_cast<float>(ImGui::GetTime());

        // Sinusoidal ambient pulse (subtle vintage incandescent bulb flicker / power oscillation)
        float const sine_wave = (std::sin(time_sec * 2.8f) * 0.5f + 0.5f);
        float const sine_flicker = (std::sin(time_sec * 7.5f) * 0.5f + 0.5f);

        // Exponential light response based on signal level
        float const exp_signal = std::pow(std::clamp(value, 0.0f, 1.0f), 1.8f);

        // Dynamic light intensity factor [0.35 .. 1.0]
        float const light_intensity = std::clamp(0.35f + 0.45f * exp_signal + 0.15f * sine_wave + 0.05f * sine_flicker, 0.0f, 1.0f);

        if (config.style.theme == VUMeterTheme::VintageLitAmber) {
            ImVec2 const spot(pos.x + size.x * 0.5f, pos.y + size.y * 0.35f);
            float const spot_r = size.y * (0.32f + 0.08f * light_intensity);
            Uint8 const alpha_outer = static_cast<Uint8>(std::clamp(35.0f + 65.0f * light_intensity, 0.0f, 255.0f));
            draw_list->AddCircleFilled(spot, spot_r, IM_COL32(255, 245, 190, alpha_outer), 28);

            // Intense inner white-warm filament core glow
            Uint8 const alpha_core = static_cast<Uint8>(std::clamp(20.0f + 75.0f * exp_signal, 0.0f, 255.0f));
            draw_list->AddCircleFilled(spot, spot_r * 0.45f, IM_COL32(255, 255, 235, alpha_core), 20);
        } else if (config.style.theme == VUMeterTheme::ModernDark) {
            ImVec2 const spot(pos.x + size.x * 0.5f, pos.y + size.y * 0.35f);
            float const spot_r = size.y * (0.30f + 0.06f * light_intensity);
            Uint8 const alpha_glow = static_cast<Uint8>(std::clamp(30.0f + 60.0f * light_intensity, 0.0f, 255.0f));
            draw_list->AddCircleFilled(spot, spot_r, IM_COL32(0, 210, 255, alpha_glow), 24);
        }
    }

    // 3. Scale Arc & Ticks
    float const cx = pos.x + size.x * 0.5f;
    float const cy = pos.y + size.y * 1.08f;
    float const r_arc = size.y * 0.72f;

    float const start_angle = config.start_angle_rad;
    float const sweep_angle = config.sweep_angle_rad;

    constexpr int kArcSteps = 24;
    for (int i = 0; i < kArcSteps; ++i) {
        float const t1 = static_cast<float>(i) / kArcSteps;
        float const t2 = static_cast<float>(i + 1) / kArcSteps;
        float const a1 = start_angle + t1 * sweep_angle;
        float const a2 = start_angle + t2 * sweep_angle;
        ImU32 const seg_col = (t1 >= config.style.overload_threshold) ? scale_overload_col : scale_norm_col;
        ImVec2 const p1(cx + std::cos(a1) * r_arc, cy + std::sin(a1) * r_arc);
        ImVec2 const p2(cx + std::cos(a2) * r_arc, cy + std::sin(a2) * r_arc);
        draw_list->AddLine(p1, p2, seg_col, 1.5f);
    }

    // Get Ticks
    std::vector<VUMeterTick> ticks = config.custom_ticks;
    if (ticks.empty() && config.scale_type != VUMeterScaleType::Custom) {
        ticks = get_preset_ticks(config.scale_type);
    }

    for (const auto& tk : ticks) {
        float const a = start_angle + tk.norm_val * sweep_angle;
        float const tick_len = tk.major ? 5.0f : 3.0f;
        ImU32 t_col = scale_norm_col;
        if (tk.color) {
            t_col = *tk.color;
        } else if (tk.norm_val >= config.style.overload_threshold) {
            t_col = scale_overload_col;
        }

        ImVec2 const p_in(cx + std::cos(a) * (r_arc - tick_len), cy + std::sin(a) * (r_arc - tick_len));
        ImVec2 const p_out(cx + std::cos(a) * (r_arc + 1.0f), cy + std::sin(a) * (r_arc + 1.0f));
        draw_list->AddLine(p_in, p_out, t_col, tk.major ? 1.5f : 1.0f);

        if (tk.major && !tk.label.empty()) {
            ImVec2 const t_pos(cx + std::cos(a) * (r_arc - tick_len - 7.0f), cy + std::sin(a) * (r_arc - tick_len - 7.0f));
            ImVec2 const sz = ImGui::CalcTextSize(tk.label.c_str());
            draw_list->AddText(ImVec2(t_pos.x - sz.x * 0.5f, t_pos.y - sz.y * 0.5f), t_col, tk.label.c_str());
        }
    }

    // 4. Badges / Titles
    if (config.show_titles) {
        if (!config.title.empty()) {
            ImVec2 const vu_sz = ImGui::CalcTextSize(config.title.c_str());
            draw_list->AddText(ImVec2(cx - vu_sz.x * 0.5f, pos.y + size.y * 0.22f), label_col, config.title.c_str());
        }

        if (!ch_label.empty()) {
            ImVec2 const ch_sz = ImGui::CalcTextSize(ch_label.c_str());
            draw_list->AddText(ImVec2(cx - ch_sz.x * 0.5f, pos.y + size.y * 0.76f), label_col, ch_label.c_str());
        }
    }

    // 5. Watermark Peak Tick
    if (config.style.show_peak_watermark && watermark > 0.01f) {
        float const act_wm = std::clamp(watermark, 0.0f, 1.0f);
        float const wm_angle = start_angle + act_wm * sweep_angle;
        ImVec2 const wm_p1(cx + std::cos(wm_angle) * (r_arc - 5.0f), cy + std::sin(wm_angle) * (r_arc - 5.0f));
        ImVec2 const wm_p2(cx + std::cos(wm_angle) * (r_arc + 3.0f), cy + std::sin(wm_angle) * (r_arc + 3.0f));
        ImU32 const wm_col = IM_COL32(235, 40, 40, 240);
        draw_list->AddLine(wm_p1, wm_p2, wm_col, 2.0f);
    }

    // 6. Needle & Pivot Cap
    float const act_level = std::clamp(value, 0.0f, 1.0f);
    float const needle_angle = start_angle + act_level * sweep_angle;
    float const r_needle = r_arc + 2.0f;

    ImVec2 const needle_tip(cx + std::cos(needle_angle) * r_needle, cy + std::sin(needle_angle) * r_needle);
    draw_list->AddLine(ImVec2(cx, cy), needle_tip, needle_col, 1.8f);

    if (act_level >= config.style.overload_threshold || config.style.theme == VUMeterTheme::VintageLitAmber) {
        // Red accent on needle tip
        ImVec2 const sub_tip(cx + std::cos(needle_angle) * (r_needle - 6.0f), cy + std::sin(needle_angle) * (r_needle - 6.0f));
        draw_list->AddLine(sub_tip, needle_tip, scale_overload_col, 2.0f);
    }

    // Pivot Cap
    draw_list->AddCircleFilled(ImVec2(cx, cy), 4.5f, pivot_col);
    draw_list->AddCircle(ImVec2(cx, cy), 4.5f, (config.style.theme == VUMeterTheme::VintageLitAmber) ? IM_COL32(160, 165, 175, 255) : IM_COL32(70, 75, 85, 255), 12, 1.0f);

    // 7. Glass Reflection Overlay
    if (config.style.draw_glass_reflection) {
        ImVec2 const g1(min_p.x + 3.0f, min_p.y + 3.0f);
        ImVec2 const g2(min_p.x + size.x * 0.6f, min_p.y + 3.0f);
        ImVec2 const g3(min_p.x + 3.0f, min_p.y + size.y * 0.6f);
        ImU32 const glass_alpha = (config.style.theme == VUMeterTheme::VintageLitAmber) ? 22 : 10;
        draw_list->AddTriangleFilled(g1, g2, g3, IM_COL32(255, 255, 255, glass_alpha));
    }

    draw_list->PopClipRect();

    // Outer border outline
    draw_list->AddRect(min_p, max_p, bezel_col, 4.0f, 0, 1.5f);
}

void draw_stereo_analog_dial(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size,
                             float level_l, float level_r,
                             float watermark_l, float watermark_r,
                             const VUMeterConfig& config) {
    float const gap = 6.0f;
    float meter_w = std::max((size.x - gap) / 2.0f, 20.0f);

    ImVec2 const l_pos = pos;
    ImVec2 const l_size(meter_w, size.y);
    draw_analog_dial(draw_list, l_pos, l_size, level_l, watermark_l, config.left_channel_title, config);

    ImVec2 const r_pos(pos.x + meter_w + gap, pos.y);
    ImVec2 const r_size(meter_w, size.y);
    draw_analog_dial(draw_list, r_pos, r_size, level_r, watermark_r, config.right_channel_title, config);
}

void draw_bar_meter(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size,
                    float level_l, float level_r,
                    float watermark_l, float watermark_r,
                    const VUMeterConfig& config) {
    if (!draw_list || size.x <= 0.0f || size.y <= 0.0f) return;

    bool const is_stereo = (level_r >= 0.0f);
    int const num_channels = is_stereo ? 2 : 1;

    float bar_w = std::max(is_stereo ? (size.x - 4.0f) / 2.0f : size.x, 3.0f);

    for (int ch = 0; ch < num_channels; ++ch) {
        float const level = (ch == 0) ? std::clamp(level_l, 0.0f, 1.0f) : std::clamp(level_r, 0.0f, 1.0f);
        float const watermark = (ch == 0) ? std::clamp(watermark_l, 0.0f, 1.0f) : std::clamp(watermark_r, 0.0f, 1.0f);

        float const ch_x = pos.x + (is_stereo ? static_cast<float>(ch) * (bar_w + 4.0f) : 0.0f);
        ImVec2 const ch_min(ch_x, pos.y);
        ImVec2 const ch_max(ch_x + bar_w, pos.y + size.y);

        // Container background
        draw_list->AddRectFilled(ch_min, ch_max, IM_COL32(20, 24, 30, 255), 2.0f);
        draw_list->AddRect(ch_min, ch_max, IM_COL32(45, 55, 65, 255), 2.0f);

        // Level fill
        if (level > 0.005f) {
            float const fill_h = (size.y - 2.0f) * level;
            ImVec2 const f_min(ch_min.x + 1.0f, ch_max.y - 1.0f - fill_h);
            ImVec2 const f_max(ch_max.x - 1.0f, ch_max.y - 1.0f);
            ImU32 col = IM_COL32(46, 204, 113, 255);
            if (level > config.style.overload_threshold) {
                col = IM_COL32(231, 76, 60, 255);
            } else if (level > 0.70f) {
                col = IM_COL32(241, 196, 15, 255);
            }
            draw_list->AddRectFilled(f_min, f_max, col, 1.0f);
        }

        // Peak Watermark Line
        if (config.style.show_peak_watermark && watermark > 0.005f) {
            float const wm_y = ch_max.y - 1.0f - ((size.y - 2.0f) * watermark);
            ImU32 wm_col = IM_COL32(220, 245, 220, 255);
            if (watermark > config.style.overload_threshold) {
                wm_col = IM_COL32(255, 90, 90, 255);
            } else if (watermark > 0.70f) {
                wm_col = IM_COL32(255, 220, 80, 255);
            }
            draw_list->AddLine(ImVec2(ch_min.x + 1.0f, wm_y), ImVec2(ch_max.x - 1.0f, wm_y), wm_col, 2.0f);
        }
    }
}

void render_analog_dial(const ImVec2& size, float value, float watermark,
                        const std::string& ch_label,
                        const VUMeterConfig& config) {
    ImVec2 const pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_analog_dial(draw_list, pos, size, value, watermark, ch_label, config);
    ImGui::Dummy(size);
}

void render_stereo_analog_dial(const ImVec2& size, float level_l, float level_r,
                               float watermark_l, float watermark_r,
                               const VUMeterConfig& config) {
    ImVec2 const pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_stereo_analog_dial(draw_list, pos, size, level_l, level_r, watermark_l, watermark_r, config);
    ImGui::Dummy(size);
}

void render_bar_meter(const ImVec2& size, float level_l, float level_r,
                      float watermark_l, float watermark_r,
                      const VUMeterConfig& config) {
    ImVec2 const pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_bar_meter(draw_list, pos, size, level_l, level_r, watermark_l, watermark_r, config);
    ImGui::Dummy(size);
}

} // namespace rouen::helpers::vu_meter
