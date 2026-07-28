#pragma once

#include "media_player_item.hpp"
#include "media_player_alarm.hpp"
#include "mac_menu_helper.hpp"
#include "./imgui_include.hpp"
#include "../registrar.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>
#include <optional>
#include <string_view>

#include "../cards/interface/card.hpp"

struct media_player {
    using item = media_player_item;
    using item_map = media_player_item_map;

    static std::recursive_mutex & items_mutex();
    static item_map & items();

    static void stopAll();
    static void stopForOwner(const void* owner);
    static bool is_any_playing_non_cast();
    static std::optional<double>& get_dummy_watermark() noexcept;

    static ImGuiID get_item_id(std::string_view url);
    static std::shared_ptr<media_player_item> get_item_ptr(ImGuiID id);
    static std::shared_ptr<media_player_item> get_item_ptr(std::string_view url);
    static media_player_item& get_item(ImGuiID id);
    static media_player_item& get_item(std::string_view url);

    static std::shared_ptr<media_player_item> s_active_fullscreen_item;
    static std::mutex s_fullscreen_mutex;
    static std::weak_ptr<card> s_fullscreen_origin_card;

    static std::shared_ptr<media_player_item> s_detached_item;
    static std::mutex s_detached_mutex;

    static void save_fullscreen_origin_card(const std::shared_ptr<media_player_item>& item);
    static void restore_fullscreen_origin_focus();

    static void set_detached_item(std::shared_ptr<media_player_item> item);
    static std::shared_ptr<media_player_item> get_detached_item();
    static void clear_detached_item();
    static bool has_detached_item();

    static void set_active_fullscreen_item(std::shared_ptr<media_player_item> item);
    static std::shared_ptr<media_player_item> get_active_fullscreen_item();
    static void clear_active_fullscreen_item();
    static bool has_active_fullscreen_item();

    static RouenGPUTexture* s_unlit_vu_texture;
    static SDL_GPUTransferBuffer* s_unlit_vu_transfer_buf;
    static std::mutex s_unlit_vu_mutex;

    static ImTextureID get_unlit_vu_texture_id(SDL_GPUDevice* device = nullptr);

    static void draw_stereo_vu_meter(float level_l, float level_r, float watermark_l, float watermark_r, float width, float height);
    static void draw_vintage_110_vu_meter(float level_l, float level_r, float watermark_l = 0.0f, float watermark_r = 0.0f, float width = 0.0f, float height = 85.0f, bool is_lit = false);
    static void draw_full_window_audio_visualization(media_player_item& item, float win_w, float win_h);
    static void draw_full_window_progress_line(media_player_item& item, float win_w, float win_h);

    static void player(std::string_view url, ImVec4 info_color, std::string_view title = "Media", long long feed_id = -1, std::string_view item_link = "", std::string_view item_title = "", std::optional<double>& initial_watermark = get_dummy_watermark(), bool prefer_tall_layout = false, float max_width = 0.0f, const void* owner_card = nullptr);
};
