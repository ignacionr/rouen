#include "media_player.hpp"
#include "texture_helper.hpp"
#include "image_cache.hpp"
#include "../cards/information/rss.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <set>
#include <thread>

std::shared_ptr<media_player_item> media_player::s_active_fullscreen_item{nullptr};
std::mutex media_player::s_fullscreen_mutex;
std::weak_ptr<card> media_player::s_fullscreen_origin_card;

std::shared_ptr<media_player_item> media_player::s_detached_item{nullptr};
std::mutex media_player::s_detached_mutex;

RouenGPUTexture* media_player::s_unlit_vu_texture{nullptr};
SDL_GPUTransferBuffer* media_player::s_unlit_vu_transfer_buf{nullptr};
std::mutex media_player::s_unlit_vu_mutex;

std::recursive_mutex & media_player::items_mutex() {
    static std::recursive_mutex mtx;
    return mtx;
}

media_player::item_map & media_player::items() {
    static item_map items_;
    return items_;
}

void media_player::stopAll() {
    std::lock_guard<std::recursive_mutex> lock(items_mutex());
    for (auto &[k,v]: items()) {
        if (v) v->stopMedia();
    }
    clear_detached_item();
}

void media_player::stopForOwner(const void* owner) {
    if (!owner) return;
    std::lock_guard<std::recursive_mutex> lock(items_mutex());
    for (auto &[k, v]: items()) {
        if (v && v->owner_card == owner) {
            v->stopMedia();
        }
    }
    std::lock_guard<std::mutex> detached_lock(s_detached_mutex);
    if (s_detached_item && s_detached_item->owner_card == owner) {
        s_detached_item->stopMedia();
        s_detached_item = nullptr;
    }
    std::lock_guard<std::mutex> fs_lock(s_fullscreen_mutex);
    if (s_active_fullscreen_item && s_active_fullscreen_item->owner_card == owner) {
        s_active_fullscreen_item->stopMedia();
        s_active_fullscreen_item = nullptr;
        restore_fullscreen_origin_focus();
    }
}

bool media_player::is_any_playing_non_cast() {
    std::lock_guard<std::recursive_mutex> lock(items_mutex());
    for (auto &[k, v] : items()) {
        if (v && v->is_playing && !v->is_paused.load()) {
            return true;
        }
    }
    return false;
}

std::optional<double>& media_player::get_dummy_watermark() noexcept {
    static thread_local std::optional<double> dummy;
    dummy = std::nullopt;
    return dummy;
}

ImGuiID media_player::get_item_id(std::string_view url) {
    return ImHashData(url.data(), url.size(), 0);
}

std::shared_ptr<media_player_item> media_player::get_item_ptr(ImGuiID id) {
    std::lock_guard<std::recursive_mutex> lock(items_mutex());
    auto& item_ptr = items()[id];
    if (!item_ptr) {
        item_ptr = std::make_shared<media_player_item>();
    }
    return item_ptr;
}

std::shared_ptr<media_player_item> media_player::get_item_ptr(std::string_view url) {
    return get_item_ptr(get_item_id(url));
}

media_player_item& media_player::get_item(ImGuiID id) {
    return *get_item_ptr(id);
}

media_player_item& media_player::get_item(std::string_view url) {
    return *get_item_ptr(url);
}

void media_player::save_fullscreen_origin_card(const std::shared_ptr<media_player_item>& item) {
    std::shared_ptr<card> target_card = nullptr;
    try {
        auto get_cards_fn = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
        if (get_cards_fn) {
            auto cards = (*get_cards_fn)();
            if (item && item->owner_card) {
                for (const auto& c : cards) {
                    if (c && c.get() == item->owner_card) {
                        target_card = c;
                        break;
                    }
                }
            }
            if (!target_card) {
                for (const auto& c : cards) {
                    if (c && c->is_focused) {
                        target_card = c;
                        break;
                    }
                }
            }
            if (!target_card && !cards.empty()) {
                for (const auto& c : cards) {
                    if (c && c->grab_focus) {
                        target_card = c;
                        break;
                    }
                }
            }
        }
    } catch (...) {}

    s_fullscreen_origin_card = target_card;
}

void media_player::restore_fullscreen_origin_focus() {
    auto saved_card = s_fullscreen_origin_card.lock();
    s_fullscreen_origin_card.reset();

    if (!saved_card) return;

    try {
        auto get_cards_fn = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
        if (get_cards_fn) {
            auto cards = (*get_cards_fn)();
            for (const auto& c : cards) {
                if (c == saved_card) {
                    c->grab_focus = true;
                    break;
                }
            }
        }
    } catch (...) {}
}

void media_player::set_detached_item(std::shared_ptr<media_player_item> item) {
    std::lock_guard<std::mutex> lock(s_detached_mutex);
    s_detached_item = item;
}

std::shared_ptr<media_player_item> media_player::get_detached_item() {
    std::lock_guard<std::mutex> lock(s_detached_mutex);
    return s_detached_item;
}

void media_player::clear_detached_item() {
    std::lock_guard<std::mutex> lock(s_detached_mutex);
    s_detached_item = nullptr;
}

bool media_player::has_detached_item() {
    std::lock_guard<std::mutex> lock(s_detached_mutex);
    return s_detached_item != nullptr;
}

void media_player::set_active_fullscreen_item(std::shared_ptr<media_player_item> item) {
    std::lock_guard<std::mutex> lock(s_fullscreen_mutex);
    if (!s_active_fullscreen_item && item) {
        save_fullscreen_origin_card(item);
    }
    s_active_fullscreen_item = item;
    if (get_detached_item() == item) {
        clear_detached_item();
    }
}

std::shared_ptr<media_player_item> media_player::get_active_fullscreen_item() {
    std::lock_guard<std::mutex> lock(s_fullscreen_mutex);
    return s_active_fullscreen_item;
}

void media_player::clear_active_fullscreen_item() {
    std::lock_guard<std::mutex> lock(s_fullscreen_mutex);
    if (s_active_fullscreen_item) {
        s_active_fullscreen_item = nullptr;
        restore_fullscreen_origin_focus();
    }
}

bool media_player::has_active_fullscreen_item() {
    std::lock_guard<std::mutex> lock(s_fullscreen_mutex);
    return s_active_fullscreen_item != nullptr;
}

ImTextureID media_player::get_unlit_vu_texture_id(SDL_GPUDevice* device) {
    if (!device) {
        try {
            auto r_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (r_ptr && *r_ptr) device = *r_ptr;
        } catch (...) {}
    }
    if (!device) return ImTextureID{};

    std::lock_guard<std::mutex> lock(s_unlit_vu_mutex);
    if (s_unlit_vu_texture) {
        return rouen::helpers::texture_id_cast(s_unlit_vu_texture);
    }

    constexpr int kTexW = 512;
    constexpr int kTexH = 256;
    std::vector<uint8_t> pixels(kTexW * kTexH * 4, 0);

    auto blend_pixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, float a_factor) {
        if (x < 0 || x >= kTexW || y < 0 || y >= kTexH) return;
        size_t idx = static_cast<size_t>((y * kTexW + x) * 4);
        float src_a = a_factor;
        float dst_a = static_cast<float>(pixels[idx + 3]) / 255.0f;
        float out_a = src_a + dst_a * (1.0f - src_a);
        if (out_a <= 0.0f) return;
        float out_r = (static_cast<float>(r) * src_a + static_cast<float>(pixels[idx + 0]) * dst_a * (1.0f - src_a)) / out_a;
        float out_g = (static_cast<float>(g) * src_a + static_cast<float>(pixels[idx + 1]) * dst_a * (1.0f - src_a)) / out_a;
        float out_b = (static_cast<float>(b) * src_a + static_cast<float>(pixels[idx + 2]) * dst_a * (1.0f - src_a)) / out_a;
        pixels[idx + 0] = static_cast<uint8_t>(std::clamp(out_r, 0.0f, 255.0f));
        pixels[idx + 1] = static_cast<uint8_t>(std::clamp(out_g, 0.0f, 255.0f));
        pixels[idx + 2] = static_cast<uint8_t>(std::clamp(out_b, 0.0f, 255.0f));
        pixels[idx + 3] = static_cast<uint8_t>(std::clamp(out_a * 255.0f, 0.0f, 255.0f));
    };

    auto draw_rect = [&](int min_x, int min_y, int max_x, int max_y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        float af = static_cast<float>(a) / 255.0f;
        for (int y = min_y; y < max_y; ++y) {
            for (int x = min_x; x < max_x; ++x) {
                blend_pixel(x, y, r, g, b, af);
            }
        }
    };

    auto dist_to_segment = [](float px, float py, float ax, float ay, float bx, float by) {
        float l2 = (bx - ax)*(bx - ax) + (by - ay)*(by - ay);
        if (l2 == 0.0f) return std::hypot(px - ax, py - ay);
        float t = std::max(0.0f, std::min(1.0f, ((px - ax)*(bx - ax) + (py - ay)*(by - ay)) / l2));
        float proj_x = ax + t * (bx - ax);
        float proj_y = ay + t * (by - ay);
        return std::hypot(px - proj_x, py - proj_y);
    };

    auto draw_aa_line = [&](float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float thick) {
        int min_x = std::clamp(static_cast<int>(std::floor(std::min(x1, x2) - thick - 1.0f)), 0, kTexW - 1);
        int max_x = std::clamp(static_cast<int>(std::ceil(std::max(x1, x2) + thick + 1.0f)), 0, kTexW - 1);
        int min_y = std::clamp(static_cast<int>(std::floor(std::min(y1, y2) - thick - 1.0f)), 0, kTexH - 1);
        int max_y = std::clamp(static_cast<int>(std::ceil(std::max(y1, y2) + thick + 1.0f)), 0, kTexH - 1);
        float half_t = thick * 0.5f;
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                float d = dist_to_segment(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, x1, y1, x2, y2);
                if (d <= half_t + 0.75f) {
                    float cov = std::clamp((half_t + 0.75f - d) / 1.0f, 0.0f, 1.0f);
                    blend_pixel(x, y, r, g, b, cov * (static_cast<float>(a) / 255.0f));
                }
            }
        }
    };

    float gap = 12.0f;
    float meter_w = (512.0f - gap - 8.0f) / 2.0f;
    float meter_h = 248.0f;

    for (int ch = 0; ch < 2; ++ch) {
        float m_x = 4.0f + (meter_w + gap) * static_cast<float>(ch);
        float m_y = 4.0f;

        // Frame Bezel
        draw_rect(static_cast<int>(m_x), static_cast<int>(m_y), static_cast<int>(m_x + meter_w), static_cast<int>(m_y + meter_h), 24, 26, 32, 255);

        // Faceplate Background
        int in_min_x = static_cast<int>(m_x + 3.0f);
        int in_min_y = static_cast<int>(m_y + 3.0f);
        int in_max_x = static_cast<int>(m_x + meter_w - 3.0f);
        int in_max_y = static_cast<int>(m_y + meter_h - 3.0f);

        for (int y = in_min_y; y < in_max_y; ++y) {
            float t = static_cast<float>(y - in_min_y) / static_cast<float>(in_max_y - in_min_y);
            uint8_t r = static_cast<uint8_t>(38.0f * (1.0f - t) + 26.0f * t);
            uint8_t g = static_cast<uint8_t>(42.0f * (1.0f - t) + 28.0f * t);
            uint8_t b = static_cast<uint8_t>(48.0f * (1.0f - t) + 33.0f * t);
            for (int x = in_min_x; x < in_max_x; ++x) {
                blend_pixel(x, y, r, g, b, 1.0f);
            }
        }

        // Inner Border
        draw_aa_line(m_x + 2, m_y + 2, m_x + meter_w - 2, m_y + 2, 45, 50, 60, 255, 1.5f);
        draw_aa_line(m_x + meter_w - 2, m_y + 2, m_x + meter_w - 2, m_y + meter_h - 2, 45, 50, 60, 255, 1.5f);
        draw_aa_line(m_x + meter_w - 2, m_y + meter_h - 2, m_x + 2, m_y + meter_h - 2, 45, 50, 60, 255, 1.5f);
        draw_aa_line(m_x + 2, m_y + meter_h - 2, m_x + 2, m_y + 2, 45, 50, 60, 255, 1.5f);

        // 110-Degree Arc Math
        float cx = m_x + meter_w * 0.5f;
        float cy = m_y + meter_h * 1.08f;
        float r_arc = meter_h * 0.72f;

        constexpr float kStartAngle = -2.530727f; // -145 deg
        constexpr float kSweepAngle = 1.919862f;  // 110 deg

        constexpr int kArcSteps = 32;
        for (int i = 0; i < kArcSteps; ++i) {
            float t1 = static_cast<float>(i) / kArcSteps;
            float t2 = static_cast<float>(i + 1) / kArcSteps;
            float a1 = kStartAngle + t1 * kSweepAngle;
            float a2 = kStartAngle + t2 * kSweepAngle;
            uint8_t cr = (t1 >= 0.75f) ? 130 : 110;
            uint8_t cg = (t1 >= 0.75f) ? 50  : 115;
            uint8_t cb = (t1 >= 0.75f) ? 50  : 125;
            draw_aa_line(cx + std::cos(a1) * r_arc, cy + std::sin(a1) * r_arc, cx + std::cos(a2) * r_arc, cy + std::sin(a2) * r_arc, cr, cg, cb, 200, 2.0f);
        }

        // Ticks
        struct db_tick { float t; bool major; };
        static const db_tick ticks[] = {
            { 0.00f, true }, { 0.28f, true }, { 0.54f, true }, { 0.64f, false },
            { 0.75f, true }, { 0.83f, false }, { 1.00f, true }
        };
        for (const auto& tk : ticks) {
            float a = kStartAngle + tk.t * kSweepAngle;
            float tick_len = tk.major ? 10.0f : 6.0f;
            uint8_t cr = (tk.t >= 0.75f) ? 130 : 110;
            uint8_t cg = (tk.t >= 0.75f) ? 50  : 115;
            uint8_t cb = (tk.t >= 0.75f) ? 50  : 125;
            float px1 = cx + std::cos(a) * (r_arc - tick_len);
            float py1 = cy + std::sin(a) * (r_arc - tick_len);
            float px2 = cx + std::cos(a) * (r_arc + 2.0f);
            float py2 = cy + std::sin(a) * (r_arc + 2.0f);
            draw_aa_line(px1, py1, px2, py2, cr, cg, cb, 200, tk.major ? 2.2f : 1.5f);
        }

        // Needle at Minimum (-145 deg)
        float needle_angle = kStartAngle;
        float r_needle = r_arc + 4.0f;
        draw_aa_line(cx, cy, cx + std::cos(needle_angle) * r_needle, cy + std::sin(needle_angle) * r_needle, 75, 80, 90, 220, 2.5f);

        // Pivot Cap
        for (int py = static_cast<int>(cy - 8); py <= static_cast<int>(cy + 8); ++py) {
            for (int px = static_cast<int>(cx - 8); px <= static_cast<int>(cx + 8); ++px) {
                float d = std::hypot(static_cast<float>(px) - cx, static_cast<float>(py) - cy);
                if (d <= 8.0f) {
                    blend_pixel(px, py, 30, 32, 38, 1.0f);
                }
            }
        }

        // Glass Sheen
        for (int y = in_min_y; y < in_min_y + 80; ++y) {
            for (int x = in_min_x; x < in_min_x + 160; ++x) {
                if ((x - in_min_x) + (y - in_min_y) < 140) {
                    blend_pixel(x, y, 255, 255, 255, 0.04f);
                }
            }
        }
    }

    SDL_GPUTextureCreateInfo texture_info = {};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = kTexW;
    texture_info.height = kTexH;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    SDL_GPUTexture* raw_texture = SDL_CreateGPUTexture(device, &texture_info);

    if (raw_texture) {
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = kTexW * kTexH * 4;
        s_unlit_vu_transfer_buf = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, s_unlit_vu_transfer_buf, false));
        if (map) {
            std::memcpy(map, pixels.data(), kTexW * kTexH * 4);
            SDL_UnmapGPUTransferBuffer(device, s_unlit_vu_transfer_buf);
            SDL_GPUCommandBuffer* cmd_buf = SDL_AcquireGPUCommandBuffer(device);
            if (cmd_buf) {
                SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);
                if (copy_pass) {
                    SDL_GPUTextureRegion region = {};
                    region.texture = raw_texture;
                    region.w = kTexW;
                    region.h = kTexH;
                    region.d = 1;
                    SDL_GPUTextureTransferInfo xfer = {};
                    xfer.transfer_buffer = s_unlit_vu_transfer_buf;
                    xfer.offset = 0;
                    xfer.pixels_per_row = kTexW;
                    xfer.rows_per_layer = kTexH;
                    SDL_UploadToGPUTexture(copy_pass, &xfer, &region, false);
                    SDL_EndGPUCopyPass(copy_pass);
                }
                SDL_SubmitGPUCommandBuffer(cmd_buf);
            }
        }

        s_unlit_vu_texture = new RouenGPUTexture();
        s_unlit_vu_texture->binding.texture = raw_texture;
        s_unlit_vu_texture->binding.sampler = TextureHelper::getDefaultSampler(device);
        s_unlit_vu_texture->width = kTexW;
        s_unlit_vu_texture->height = kTexH;
    }

    return s_unlit_vu_texture ? rouen::helpers::texture_id_cast(s_unlit_vu_texture) : ImTextureID{};
}

void media_player::draw_stereo_vu_meter(float level_l, float level_r, float watermark_l, float watermark_r, float width, float height) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    float bar_w = (width - 4.0f) / 2.0f;
    if (bar_w < 3.0f) bar_w = 3.0f;

    level_l = std::clamp(level_l, 0.0f, 1.0f);
    level_r = std::clamp(level_r, 0.0f, 1.0f);
    watermark_l = std::clamp(watermark_l, 0.0f, 1.0f);
    watermark_r = std::clamp(watermark_r, 0.0f, 1.0f);

    // Left Channel Container
    ImVec2 l_min = pos;
    ImVec2 l_max = ImVec2(pos.x + bar_w, pos.y + height);
    draw_list->AddRectFilled(l_min, l_max, IM_COL32(20, 24, 30, 255), 2.0f);
    draw_list->AddRect(l_min, l_max, IM_COL32(45, 55, 65, 255), 2.0f);

    if (level_l > 0.01f) {
        float fill_h = (height - 2.0f) * level_l;
        ImVec2 f_min = ImVec2(l_min.x + 1.0f, l_max.y - 1.0f - fill_h);
        ImVec2 f_max = ImVec2(l_max.x - 1.0f, l_max.y - 1.0f);
        ImU32 col = (level_l > 0.85f) ? IM_COL32(231, 76, 60, 255) :
                    (level_l > 0.70f) ? IM_COL32(241, 196, 15, 255) : IM_COL32(46, 204, 113, 255);
        draw_list->AddRectFilled(f_min, f_max, col, 1.0f);
    }

    if (watermark_l > 0.01f) {
        float wm_y = l_max.y - 1.0f - ((height - 2.0f) * watermark_l);
        ImU32 wm_col = (watermark_l > 0.85f) ? IM_COL32(255, 90, 90, 255) :
                       (watermark_l > 0.70f) ? IM_COL32(255, 220, 80, 255) : IM_COL32(220, 245, 220, 255);
        draw_list->AddLine(ImVec2(l_min.x + 1.0f, wm_y), ImVec2(l_max.x - 1.0f, wm_y), wm_col, 2.0f);
    }

    // Right Channel Container
    ImVec2 r_min = ImVec2(pos.x + bar_w + 4.0f, pos.y);
    ImVec2 r_max = ImVec2(r_min.x + bar_w, pos.y + height);
    draw_list->AddRectFilled(r_min, r_max, IM_COL32(20, 24, 30, 255), 2.0f);
    draw_list->AddRect(r_min, r_max, IM_COL32(45, 55, 65, 255), 2.0f);

    if (level_r > 0.01f) {
        float fill_h = (height - 2.0f) * level_r;
        ImVec2 f_min = ImVec2(r_min.x + 1.0f, r_max.y - 1.0f - fill_h);
        ImVec2 f_max = ImVec2(r_max.x - 1.0f, r_max.y - 1.0f);
        ImU32 col = (level_r > 0.85f) ? IM_COL32(231, 76, 60, 255) :
                    (level_r > 0.70f) ? IM_COL32(241, 196, 15, 255) : IM_COL32(46, 204, 113, 255);
        draw_list->AddRectFilled(f_min, f_max, col, 1.0f);
    }

    if (watermark_r > 0.01f) {
        float wm_y = r_max.y - 1.0f - ((height - 2.0f) * watermark_r);
        ImU32 wm_col = (watermark_r > 0.85f) ? IM_COL32(255, 90, 90, 255) :
                       (watermark_r > 0.70f) ? IM_COL32(255, 220, 80, 255) : IM_COL32(220, 245, 220, 255);
        draw_list->AddLine(ImVec2(r_min.x + 1.0f, wm_y), ImVec2(r_max.x - 1.0f, wm_y), wm_col, 2.0f);
    }

    ImGui::Dummy(ImVec2(width, height));
}

void media_player::draw_vintage_110_vu_meter(float level_l, float level_r, float watermark_l, float watermark_r, float width, float height, bool is_lit) {
    if (width <= 0.0f) width = ImGui::GetContentRegionAvail().x;
    if (width < 60.0f) width = 60.0f;
    if (height <= 0.0f) height = 85.0f;

    if (!is_lit) {
        ImTextureID unlit_id = get_unlit_vu_texture_id();
        if (unlit_id) {
            ImGui::Image(unlit_id, ImVec2(width, height));
            return;
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    level_l = std::clamp(level_l, 0.0f, 1.0f);
    level_r = std::clamp(level_r, 0.0f, 1.0f);

    float gap = 6.0f;
    float meter_w = (width - gap) / 2.0f;
    if (meter_w < 20.0f) meter_w = 20.0f;

    for (int ch = 0; ch < 2; ++ch) {
        float level = (ch == 0) ? level_l : level_r;
        float watermark = (ch == 0) ? watermark_l : watermark_r;
        float m_x = pos.x + (meter_w + gap) * static_cast<float>(ch);
        float m_y = pos.y;

        ImVec2 min_p = ImVec2(m_x, m_y);
        ImVec2 max_p = ImVec2(m_x + meter_w, m_y + height);

        // 1. Frame Bezel
        draw_list->AddRectFilled(min_p, max_p, IM_COL32(24, 26, 32, 255), 4.0f);

        // 2. Faceplate Background with PushClipRect to strictly constrain all elements inside box
        ImVec2 inner_min = ImVec2(min_p.x + 2.0f, min_p.y + 2.0f);
        ImVec2 inner_max = ImVec2(max_p.x - 2.0f, max_p.y - 2.0f);
        draw_list->PushClipRect(inner_min, inner_max, true);

        if (is_lit) {
            // Lit Warm Amber Incandescent Glow
            draw_list->AddRectFilledMultiColor(
                inner_min, inner_max,
                IM_COL32(255, 244, 205, 255), // Top Left (warm yellow-amber)
                IM_COL32(255, 244, 205, 255), // Top Right
                IM_COL32(235, 212, 150, 255), // Bottom Right (golden warm)
                IM_COL32(235, 212, 150, 255)  // Bottom Left
            );
            // Incandescent bulb warm spot behind upper faceplate (radius constrained to height * 0.35f)
            ImVec2 spot = ImVec2(m_x + meter_w * 0.5f, m_y + height * 0.35f);
            draw_list->AddCircleFilled(spot, height * 0.35f, IM_COL32(255, 255, 230, 45), 24);
        } else {
            // Unlit Dark Slate Faceplate
            draw_list->AddRectFilledMultiColor(
                inner_min, inner_max,
                IM_COL32(38, 42, 48, 255), // Top Left
                IM_COL32(38, 42, 48, 255), // Top Right
                IM_COL32(26, 28, 33, 255), // Bottom Right
                IM_COL32(26, 28, 33, 255)  // Bottom Left
            );
        }

        // 3. 110-Degree Arc Math
        float cx = m_x + meter_w * 0.5f;
        float cy = m_y + height * 1.08f;
        float r_arc = height * 0.72f;

        constexpr float kStartAngle = -2.530727f; // -145 degrees
        constexpr float kSweepAngle = 1.919862f;  // 110 degrees

        // Scale colors
        ImU32 norm_col = is_lit ? IM_COL32(25, 25, 25, 240) : IM_COL32(110, 115, 125, 180);
        ImU32 red_col  = is_lit ? IM_COL32(215, 38, 38, 255) : IM_COL32(130, 50, 50, 180);

        // Draw Scale Arc
        constexpr int kArcSteps = 24;
        for (int i = 0; i < kArcSteps; ++i) {
            float t1 = static_cast<float>(i) / kArcSteps;
            float t2 = static_cast<float>(i + 1) / kArcSteps;
            float a1 = kStartAngle + t1 * kSweepAngle;
            float a2 = kStartAngle + t2 * kSweepAngle;
            ImU32 seg_col = (t1 >= 0.75f) ? red_col : norm_col;
            ImVec2 p1 = ImVec2(cx + std::cos(a1) * r_arc, cy + std::sin(a1) * r_arc);
            ImVec2 p2 = ImVec2(cx + std::cos(a2) * r_arc, cy + std::sin(a2) * r_arc);
            draw_list->AddLine(p1, p2, seg_col, 1.5f);
        }

        // Major & Minor Ticks and Text
        struct db_tick { float t; const char* label; bool major; };
        static const db_tick ticks[] = {
            { 0.00f, "-20", true },
            { 0.28f, "-10", true },
            { 0.54f, "-5",  true },
            { 0.64f, "-3",  false },
            { 0.75f, "0",   true },
            { 0.83f, "+1",  false },
            { 1.00f, "+3",  true }
        };

        for (const auto& tk : ticks) {
            float a = kStartAngle + tk.t * kSweepAngle;
            float tick_len = tk.major ? 5.0f : 3.0f;
            ImU32 t_col = (tk.t >= 0.75f) ? red_col : norm_col;

            ImVec2 p_in  = ImVec2(cx + std::cos(a) * (r_arc - tick_len), cy + std::sin(a) * (r_arc - tick_len));
            ImVec2 p_out = ImVec2(cx + std::cos(a) * (r_arc + 1.0f), cy + std::sin(a) * (r_arc + 1.0f));
            draw_list->AddLine(p_in, p_out, t_col, tk.major ? 1.5f : 1.0f);

            if (tk.major && tk.label) {
                ImVec2 t_pos = ImVec2(cx + std::cos(a) * (r_arc - tick_len - 7.0f), cy + std::sin(a) * (r_arc - tick_len - 7.0f));
                ImVec2 sz = ImGui::CalcTextSize(tk.label);
                draw_list->AddText(ImVec2(t_pos.x - sz.x * 0.5f, t_pos.y - sz.y * 0.5f), t_col, tk.label);
            }
        }

        // Labels: "VU" & "LEFT" / "RIGHT"
        ImU32 badge_col = is_lit ? IM_COL32(40, 40, 40, 220) : IM_COL32(110, 115, 125, 160);
        ImVec2 vu_sz = ImGui::CalcTextSize("VU");
        draw_list->AddText(ImVec2(cx - vu_sz.x * 0.5f, m_y + height * 0.22f), badge_col, "VU");

        const char* ch_label = (ch == 0) ? "LEFT" : "RIGHT";
        ImVec2 ch_sz = ImGui::CalcTextSize(ch_label);
        draw_list->AddText(ImVec2(cx - ch_sz.x * 0.5f, m_y + height * 0.76f), badge_col, ch_label);

        // Watermark Peak Tick Line
        if (is_lit && watermark > 0.01f) {
            float act_wm = std::clamp(watermark, 0.0f, 1.0f);
            float wm_angle = kStartAngle + act_wm * kSweepAngle;
            ImVec2 wm_p1 = ImVec2(cx + std::cos(wm_angle) * (r_arc - 5.0f), cy + std::sin(wm_angle) * (r_arc - 5.0f));
            ImVec2 wm_p2 = ImVec2(cx + std::cos(wm_angle) * (r_arc + 3.0f), cy + std::sin(wm_angle) * (r_arc + 3.0f));
            ImU32 wm_col = (act_wm >= 0.75f) ? IM_COL32(255, 45, 45, 240) : IM_COL32(235, 175, 30, 240);
            draw_list->AddLine(wm_p1, wm_p2, wm_col, 2.0f);
        }

        // 4. Needle & Pivot Cap
        float act_level = is_lit ? level : 0.0f;
        float needle_angle = kStartAngle + act_level * kSweepAngle;
        float r_needle = r_arc + 2.0f;

        ImVec2 needle_tip = ImVec2(cx + std::cos(needle_angle) * r_needle, cy + std::sin(needle_angle) * r_needle);
        ImU32 needle_col = is_lit ? IM_COL32(15, 15, 15, 255) : IM_COL32(75, 80, 90, 220);

        draw_list->AddLine(ImVec2(cx, cy), needle_tip, needle_col, 1.8f);
        if (is_lit) {
            // Red accent on needle tip
            ImVec2 sub_tip = ImVec2(cx + std::cos(needle_angle) * (r_needle - 6.0f), cy + std::sin(needle_angle) * (r_needle - 6.0f));
            draw_list->AddLine(sub_tip, needle_tip, red_col, 2.0f);
        }

        // Pivot Cap
        draw_list->AddCircleFilled(ImVec2(cx, cy), 4.5f, IM_COL32(30, 32, 38, 255));
        draw_list->AddCircle(ImVec2(cx, cy), 4.5f, is_lit ? IM_COL32(160, 165, 175, 255) : IM_COL32(70, 75, 85, 255), 12, 1.0f);

        // 5. Glass Reflection Overlay
        ImVec2 g1 = ImVec2(min_p.x + 3.0f, min_p.y + 3.0f);
        ImVec2 g2 = ImVec2(min_p.x + meter_w * 0.6f, min_p.y + 3.0f);
        ImVec2 g3 = ImVec2(min_p.x + 3.0f, min_p.y + height * 0.6f);
        draw_list->AddTriangleFilled(g1, g2, g3, IM_COL32(255, 255, 255, is_lit ? 22 : 10));

        // Pop Clip Rect
        draw_list->PopClipRect();

        // Inner Border Frame
        draw_list->AddRect(min_p, max_p, is_lit ? IM_COL32(70, 75, 85, 255) : IM_COL32(45, 50, 60, 255), 4.0f, 0, 1.5f);
    }

    ImGui::Dummy(ImVec2(width, height));
}

static std::shared_ptr<::helpers::ImageCache> get_media_player_image_cache() {
    static std::mutex cache_mutex;
    static std::shared_ptr<::helpers::ImageCache> instance;
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (!instance) {
        auto db_path = rouen::platform::get_user_data_path("rss_images.db").string();
        auto cache_dir = rouen::platform::get_user_data_path("cache/rss_images").string();
        instance = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
    }
    return instance;
}

void media_player::draw_full_window_audio_visualization(media_player_item& item, float win_w, float win_h) {
    // 0. Resolve/load RSS cover art if applicable
    if (item.feed_id >= 0 && item.rss_image_url.empty()) {
        auto rss_host = rouen::cards::rss::getHost();
        if (rss_host) {
            auto feed_item = rss_host->get_feed_item(item.feed_id, item.item_link, item.item_title);
            if (feed_item && !feed_item->image_url.empty()) {
                item.rss_image_url = feed_item->image_url;
            } else {
                auto feed_info = rss_host->get_feed_info(item.feed_id);
                if (feed_info && !feed_info->image_url.empty()) {
                    item.rss_image_url = feed_info->image_url;
                }
            }
        }
        if (item.rss_image_url.empty()) {
            item.rss_image_url = "__none__";
        }
    }

    if (!item.rss_image_url.empty() && item.rss_image_url != "__none__" && !item.rss_image_texture) {
        auto cache = get_media_player_image_cache();
        if (cache) {
            int cached_w = 0, cached_h = 0;
            if (cache->isCached(item.rss_image_url, cached_w, cached_h)) {
                item.rss_image_texture = cache->getTexture(TextureHelper::g_gpu_device, item.rss_image_url, item.rss_image_width, item.rss_image_height);
            } else {
                // Request background download
                static std::set<std::string> downloading_urls;
                static std::mutex downloading_mutex;
                
                bool already_downloading = false;
                {
                    std::lock_guard<std::mutex> lock(downloading_mutex);
                    if (downloading_urls.contains(item.rss_image_url)) {
                        already_downloading = true;
                    } else {
                        downloading_urls.insert(item.rss_image_url);
                    }
                }
                
                if (!already_downloading) {
                    std::thread([cache, url = item.rss_image_url]() {
                        try {
                            cache->downloadAndCache(url);
                        } catch (...) {}
                        
                        std::lock_guard<std::mutex> lock(downloading_mutex);
                        downloading_urls.erase(url);
                    }).detach();
                }
            }
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float lvl_l = item.get_vu_level_l();
    float lvl_r = item.get_vu_level_r();
    float avg_lvl = (lvl_l + lvl_r) * 0.5f;

    static float anim_time = 0.0f;
    anim_time += ImGui::GetIO().DeltaTime;

    // 1. Ambient radial background glow pulse
    ImVec2 center(win_w * 0.5f, win_h * 0.38f);
    float glow_radius = std::min(win_w, win_h) * (0.35f + avg_lvl * 0.15f);

    for (int ring = 5; ring >= 1; --ring) {
        float r = glow_radius * (static_cast<float>(ring) / 5.0f);
        uint8_t alpha = static_cast<uint8_t>((1.0f - static_cast<float>(ring) / 6.0f) * (20.0f + avg_lvl * 40.0f));
        draw_list->AddCircleFilled(center, r, IM_COL32(40, 90, 180, alpha), 32);
    }

    // 2. Frequency Spectrum Bars (Centered Waveform / Bars)
    constexpr int kBarCount = 48;
    float bar_gap = 4.0f;
    float total_bars_w = std::min(win_w * 0.85f, 900.0f);
    float bar_w = (total_bars_w - (static_cast<float>(kBarCount) - 1.0f) * bar_gap) / static_cast<float>(kBarCount);
    float start_x = (win_w - total_bars_w) * 0.5f;
    float base_y = win_h * 0.44f;
    float max_bar_h = win_h * 0.26f;

    for (int i = 0; i < kBarCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kBarCount - 1);
        float dist_from_center = std::abs(t - 0.5f) * 2.0f;

        float freq_val = std::sin(t * 3.14159f * 3.0f + anim_time * 4.0f) * 0.3f
                       + std::cos(t * 3.14159f * 7.0f - anim_time * 2.5f) * 0.2f
                       + 0.5f;

        float ch_lvl = (i < kBarCount / 2) ? lvl_l : lvl_r;
        float height_factor = (0.08f + ch_lvl * 0.85f * (1.0f - dist_from_center * 0.4f)) * freq_val;
        height_factor = std::clamp(height_factor, 0.04f, 1.0f);

        float bar_h = max_bar_h * height_factor;
        float bx = start_x + static_cast<float>(i) * (bar_w + bar_gap);
        float by_top = base_y - bar_h * 0.5f;
        float by_bot = base_y + bar_h * 0.5f;

        ImU32 bar_col;
        if (height_factor > 0.75f) {
            bar_col = IM_COL32(235, 70, 70, 230);
        } else if (height_factor > 0.45f) {
            bar_col = IM_COL32(245, 180, 50, 220);
        } else {
            bar_col = IM_COL32(60, 160, 240, 200);
        }

        draw_list->AddRectFilled(ImVec2(bx, by_top), ImVec2(bx + bar_w, by_bot), bar_col, 2.0f);
    }

    // 2.5. RSS Cover Art / Fallback Art (Centered in glowing visualizer)
    float base_art_size = std::min(win_w, win_h) * 0.28f;
    float art_size = base_art_size + avg_lvl * 30.0f;
    art_size = std::clamp(art_size, 160.0f, 320.0f);
    ImVec2 p_min(center.x - art_size * 0.5f, center.y - art_size * 0.5f);
    ImVec2 p_max(center.x + art_size * 0.5f, center.y + art_size * 0.5f);

    // Subtle drop shadow behind art
    for (int shadow_ring = 3; shadow_ring >= 1; --shadow_ring) {
        float offset = static_cast<float>(shadow_ring) * 2.0f;
        draw_list->AddRect(
            ImVec2(p_min.x - offset, p_min.y - offset),
            ImVec2(p_max.x + offset, p_max.y + offset),
            IM_COL32(0, 0, 0, static_cast<uint8_t>(40 / shadow_ring)),
            18.0f,
            0,
            2.0f
        );
    }

    // Background card (solid dark color)
    draw_list->AddRectFilled(p_min, p_max, IM_COL32(15, 18, 24, 255), 16.0f);

    if (item.rss_image_texture) {
        ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
        if (item.rss_image_width > 0 && item.rss_image_height > 0) {
            float tex_w = static_cast<float>(item.rss_image_width);
            float tex_h = static_cast<float>(item.rss_image_height);
            float target_aspect = 1.0f;
            float tex_aspect = tex_w / tex_h;
            if (tex_aspect > target_aspect) {
                float f = target_aspect / tex_aspect;
                float c = (1.0f - f) * 0.5f;
                uv0 = ImVec2(c, 0.0f);
                uv1 = ImVec2(1.0f - c, 1.0f);
            } else {
                float f = tex_aspect / target_aspect;
                float c = (1.0f - f) * 0.5f;
                uv0 = ImVec2(0.0f, c);
                uv1 = ImVec2(1.0f, 1.0f - c);
            }
        }
        draw_list->AddImageRounded(
            rouen::helpers::texture_id_cast(item.rss_image_texture),
            p_min,
            p_max,
            uv0,
            uv1,
            IM_COL32(255, 255, 255, 255),
            16.0f
        );
    } else {
        // Fallback: gradient background card with RSS or Music Icon
        draw_list->AddRectFilledMultiColor(
            p_min,
            p_max,
            IM_COL32(32, 44, 72, 255),
            IM_COL32(20, 24, 36, 255),
            IM_COL32(16, 20, 28, 255),
            IM_COL32(28, 38, 60, 255)
        );
        
        const char* icon = (item.feed_id >= 0) ? ICON_MD_RSS_FEED : ICON_MD_MUSIC_NOTE;
        ImVec2 icon_size = ImGui::CalcTextSize(icon);
        draw_list->AddText(
            ImVec2(center.x - icon_size.x * 0.5f, center.y - icon_size.y * 0.5f),
            IM_COL32(255, 255, 255, 100),
            icon
        );
    }

    // Outer frame/border highlighting the cover art
    draw_list->AddRect(p_min, p_max, IM_COL32(255, 255, 255, 45), 16.0f, 0, 2.5f);

    // 3. Glowing Oscilloscope Wave Line
    constexpr int kWavePoints = 80;
    ImVec2 wave_pts[kWavePoints];
    for (int i = 0; i < kWavePoints; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kWavePoints - 1);
        float wx = start_x + t * total_bars_w;
        float wave_amp = (lvl_l * 0.5f + lvl_r * 0.5f) * 45.0f + 5.0f;
        float wy = base_y + std::sin(t * 3.14159f * 6.0f + anim_time * 6.0f) * wave_amp
                          + std::cos(t * 3.14159f * 12.0f - anim_time * 8.0f) * (wave_amp * 0.3f);
        wave_pts[i] = ImVec2(wx, wy);
    }
    draw_list->AddPolyline(wave_pts, kWavePoints, IM_COL32(255, 255, 255, 180), 0, 2.5f);

    // 4. Centered Media Title & Status Info (Upper Center)
    std::string display_title = item.item_title.empty() ? (item.url.empty() ? "Audio Stream" : item.url) : item.item_title;
    ImVec2 title_sz = ImGui::CalcTextSize(display_title.c_str());
    float title_x = std::max(20.0f, (win_w - title_sz.x) * 0.5f);
    float title_y = win_h * 0.12f;

    draw_list->AddRectFilled(
        ImVec2(title_x - 16.0f, title_y - 8.0f),
        ImVec2(title_x + title_sz.x + 16.0f, title_y + title_sz.y + 8.0f),
        IM_COL32(15, 18, 24, 200),
        8.0f
    );
    draw_list->AddRect(
        ImVec2(title_x - 16.0f, title_y - 8.0f),
        ImVec2(title_x + title_sz.x + 16.0f, title_y + title_sz.y + 8.0f),
        IM_COL32(255, 255, 255, 30),
        8.0f
    );
    draw_list->AddText(ImVec2(title_x, title_y), IM_COL32(240, 245, 255, 255), display_title.c_str());

    std::string status_str = item.is_paused.load() ? ICON_MD_PAUSE " PAUSED" : ICON_MD_MUSIC_NOTE " NOW PLAYING";
    ImVec2 stat_sz = ImGui::CalcTextSize(status_str.c_str());
    float stat_x = (win_w - stat_sz.x) * 0.5f;
    float stat_y = title_y + title_sz.y + 14.0f;
    draw_list->AddText(ImVec2(stat_x, stat_y), item.is_paused.load() ? IM_COL32(240, 180, 60, 240) : IM_COL32(80, 220, 120, 240), status_str.c_str());
}

void media_player::draw_full_window_progress_line(media_player_item& item, float win_w, float win_h) {
    double current_pos = item.get_current_position();
    double current_dur = item.duration.load();
    if (current_dur <= 0.0) return;

    float progress = static_cast<float>(std::clamp(current_pos / current_dur, 0.0, 1.0));

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    float hit_height = 24.0f;
    bool is_bottom_hovered = (mouse_pos.x >= 0.0f && mouse_pos.x <= win_w &&
                              mouse_pos.y >= win_h - hit_height && mouse_pos.y <= win_h + 10.0f);

    static bool s_is_dragging = false;
    if (is_bottom_hovered && io.MouseDown[0] && !io.MouseDownOwned[0]) {
        s_is_dragging = true;
    }
    if (!io.MouseDown[0]) {
        s_is_dragging = false;
    }

    if (s_is_dragging) {
        double target_time = std::clamp(static_cast<double>(mouse_pos.x / win_w) * current_dur, 0.0, current_dur);
        item.seekTo(target_time);
        current_pos = target_time;
        progress = static_cast<float>(std::clamp(current_pos / current_dur, 0.0, 1.0));
    }

    float line_h = (is_bottom_hovered || s_is_dragging) ? 8.0f : 4.0f;
    float y1 = win_h - line_h;
    float y2 = win_h;

    // Background track
    draw_list->AddRectFilled(ImVec2(0.0f, y1), ImVec2(win_w, y2), IM_COL32(0, 0, 0, 160));
    draw_list->AddRectFilled(ImVec2(0.0f, y1), ImVec2(win_w, y2), IM_COL32(255, 255, 255, 50));

    // Played progress fill
    float filled_w = progress * win_w;
    if (filled_w > 0.0f) {
        draw_list->AddRectFilled(ImVec2(0.0f, y1), ImVec2(filled_w, y2), IM_COL32(235, 55, 55, 255));
    }

    // Scrubber handle (knob)
    if (is_bottom_hovered || s_is_dragging) {
        float knob_x = std::clamp(filled_w, 6.0f, win_w - 6.0f);
        float knob_y = y1 + line_h * 0.5f;
        draw_list->AddCircleFilled(ImVec2(knob_x, knob_y), 6.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircle(ImVec2(knob_x, knob_y), 6.0f, IM_COL32(235, 55, 55, 255), 12, 1.5f);

        // Hover time preview tooltip
        double hover_target_time = std::clamp(static_cast<double>(mouse_pos.x / win_w) * current_dur, 0.0, current_dur);
        std::string hover_str = item.formatTime(hover_target_time);

        ImVec2 txt_sz = ImGui::CalcTextSize(hover_str.c_str());
        float tt_x = std::clamp(mouse_pos.x - txt_sz.x * 0.5f, 10.0f, win_w - txt_sz.x - 10.0f);
        float tt_y = y1 - txt_sz.y - 12.0f;

        draw_list->AddRectFilled(
            ImVec2(tt_x - 6.0f, tt_y - 4.0f),
            ImVec2(tt_x + txt_sz.x + 6.0f, tt_y + txt_sz.y + 4.0f),
            IM_COL32(20, 20, 25, 220),
            4.0f
        );
        draw_list->AddRect(
            ImVec2(tt_x - 6.0f, tt_y - 4.0f),
            ImVec2(tt_x + txt_sz.x + 6.0f, tt_y + txt_sz.y + 4.0f),
            IM_COL32(255, 255, 255, 50),
            4.0f
        );
        draw_list->AddText(ImVec2(tt_x, tt_y), IM_COL32(255, 255, 255, 255), hover_str.c_str());
    }

    // Time overlay badge (Current / Total)
    if (is_bottom_hovered || s_is_dragging || item.is_paused.load()) {
        std::string status_text = std::format("{} / {}", item.formatTime(current_pos), item.formatTime(current_dur));
        if (item.is_paused.load()) {
            status_text = ICON_MD_PAUSE " " + status_text;
        }
        ImVec2 status_size = ImGui::CalcTextSize(status_text.c_str());
        float badge_x = 12.0f;
        float badge_y = y1 - status_size.y - 12.0f;
        draw_list->AddRectFilled(
            ImVec2(badge_x - 6.0f, badge_y - 4.0f),
            ImVec2(badge_x + status_size.x + 6.0f, badge_y + status_size.y + 4.0f),
            IM_COL32(20, 20, 25, 200),
            4.0f
        );
        draw_list->AddRect(
            ImVec2(badge_x - 6.0f, badge_y - 4.0f),
            ImVec2(badge_x + status_size.x + 6.0f, badge_y + status_size.y + 4.0f),
            IM_COL32(255, 255, 255, 40),
            4.0f
        );
        draw_list->AddText(ImVec2(badge_x, badge_y), IM_COL32(230, 230, 230, 255), status_text.c_str());
    }
}

void media_player::player(std::string_view url, ImVec4 info_color, std::string_view title, long long feed_id, std::string_view item_link, std::string_view item_title, std::optional<double>& initial_watermark, bool prefer_tall_layout, float max_width, const void* owner_card) {
    (void)prefer_tall_layout;
    float const player_width = (max_width > 0.0f) ? max_width : ImGui::GetContentRegionAvail().x;
    ImGui::PushID(url.data(), url.data() + url.size());
    try {
        ImGuiID item_id = get_item_id(url);
        auto item_ptr = get_item_ptr(item_id);
        auto &item = *item_ptr;
        if (owner_card) {
            item.owner_card = owner_card;
        }
        item.url = url;
        if (feed_id != -1) {
            item.feed_id = feed_id;
            item.item_link = item_link;
            item.item_title = item_title;
        }
        bool has_active_media = false;
        if (item.player_pid > 0) {
            has_active_media = item.checkMediaStatus();
        }
        if (!has_active_media) {
            item.watermark = initial_watermark;
        } else {
            item.update_watermark();
            initial_watermark = item.watermark;
        }
        if (has_active_media) {
            ImGui::TextUnformatted(title.data(), title.data() + title.size());
            double current_pos = item.get_current_position();
            double current_dur = item.duration.load();
            if (current_dur > 0 && current_dur > current_pos) {
                ImGui::TextColored(info_color, "%s: %s / %s",
                    item.is_paused.load() ? "Paused" : "Playing",
                    item.formatTime(current_pos).c_str(),
                    item.formatTime(current_dur).c_str());
            } else if (current_pos >= 0) {
                ImGui::TextColored(info_color, "%s: %s",
                    item.is_paused.load() ? "Paused" : "Playing",
                    item.formatTime(current_pos).c_str());
            }
            int vol = item.volume.load();
            ImGui::Text("Volume");
            ImGui::SameLine();
            if (ImGui::SliderInt("##VolumeSlider", &vol, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                item.setVolume(vol);
            }
            if (ImGui::Button(std::format(" {} ", item.is_paused.load() ? ICON_MD_PLAY_ARROW : ICON_MD_PAUSE).c_str())) {
                item.togglePause();
            }
            ImGui::SameLine();
            if (ImGui::Button(std::format(" {} ", ICON_MD_STOP).c_str())) {
                item.stopMedia();
            }
            ImGui::SameLine();
            if (current_dur > 0 && current_dur > current_pos) {
                float progress = current_pos > 0 ? 
                    static_cast<float>(current_pos / current_dur) : 0.0f;
                progress = std::max(0.0f, std::min(1.0f, progress));
                float remaining_w = std::max(50.0f, ImGui::GetContentRegionAvail().x);
                ImVec2 progress_bar_pos = ImGui::GetCursorScreenPos();
                ImGui::ProgressBar(progress, ImVec2(remaining_w, 0), "");
                if (ImGui::IsItemClicked()) {
                    auto mouse_x = ImGui::GetIO().MousePos.x;
                    auto rel_x = (mouse_x - progress_bar_pos.x) / remaining_w;
                    rel_x = std::max(0.0f, std::min(1.0f, rel_x));
                    auto target_pos = static_cast<double>(rel_x) * current_dur;
                    item.seekTo(target_pos);
                }
            } else {
                float remaining_w = std::max(50.0f, ImGui::GetContentRegionAvail().x);
                ImGui::ProgressBar(0.0f, ImVec2(remaining_w, 0), item.is_playing ? "Streaming..." : "Loading...");
            }

            ImTextureID tex = item.get_texture_id();
            bool cast_active = media_player_item::is_cast_active.load();
            bool detached_active = has_detached_item();

            if (tex && item.has_video.load()) {
                float thumb_w = 120.0f;
                float thumb_h = 67.5f;
                if (!cast_active && !detached_active) {
                    float avail_w = player_width;
                    thumb_w = std::max(120.0f, avail_w - 26.0f);
                    thumb_h = thumb_w * 9.0f / 16.0f;
                }
                ImGui::Spacing();
                ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
                ImGui::Image(tex, ImVec2(thumb_w, thumb_h));
                ImGui::SetCursorScreenPos(img_screen_pos);
                ImGui::InvisibleButton("##video_fullscreen_surface", ImVec2(thumb_w, thumb_h));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Double-click for Fullscreen");
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        set_active_fullscreen_item(item_ptr);
                    }
                }
                ImGui::SameLine();
                draw_stereo_vu_meter(item.get_vu_level_l(), item.get_vu_level_r(), item.get_vu_watermark_l(), item.get_vu_watermark_r(), 18.0f, thumb_h);
            } else {
                ImGui::Spacing();
                ImVec2 vu_screen_pos = ImGui::GetCursorScreenPos();
                draw_vintage_110_vu_meter(item.get_vu_level_l(), item.get_vu_level_r(), item.get_vu_watermark_l(), item.get_vu_watermark_r(), player_width, 85.0f, /*is_lit=*/true);
                ImGui::SetCursorScreenPos(vu_screen_pos);
                ImGui::InvisibleButton("##audio_fullscreen_surface", ImVec2(player_width, 85.0f));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Double-click for Fullscreen");
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        set_active_fullscreen_item(item_ptr);
                    }
                }
            }
        } else {
            // Stopped / Idle state: render clean Play/Resume button without premature VU meter
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            bool has_bookmark = item.watermark.has_value() && *item.watermark > 0.0;
            
            if (has_bookmark) {
                std::string formatted_bookmark = item.formatTime(*item.watermark);
                float restart_btn_w = ImGui::CalcTextSize("Restart").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float play_btn_w = player_width - restart_btn_w - ImGui::GetStyle().ItemSpacing.x;
                
                if (ImGui::Button(std::format(" {} Resume ({})", ICON_MD_PLAY_ARROW, formatted_bookmark).c_str(), ImVec2(play_btn_w, 0))) {
                    stopAll();
                    item.start_offset = *item.watermark;
                    item.playMedia(owner_card);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restart", ImVec2(restart_btn_w, 0))) {
                    stopAll();
                    item.start_offset = 0.0;
                    item.playMedia(owner_card);
                }
            } else {
                if (ImGui::Button(std::format(" {} {}", ICON_MD_PLAY_ARROW, title).c_str(), ImVec2(player_width, 0))) {
                    stopAll();
                    item.start_offset = 0.0;
                    item.playMedia(owner_card);
                }
            }
            ImGui::PopStyleVar();
        }
    }
    catch (const std::exception& e) {
        "notify"_sfn(e.what());
    }
    ImGui::PopID();
}
