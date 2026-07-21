#pragma once

#include "media_player_item.hpp"
#include "media_player_alarm.hpp"
#include "mac_menu_helper.hpp"
#include "./imgui_include.hpp"
#include "../registrar.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <algorithm>

extern "C" {
struct SDL_Window;
void SDL_GetWindowPosition(SDL_Window* window, int* x, int* y);
int SDL_GetWindowBordersSize(SDL_Window* window, int* top, int* left, int* bottom, int* right);
}

struct media_player {
    using item = media_player_item;
    using item_map = media_player_item_map;

    static std::recursive_mutex & items_mutex() {
        static std::recursive_mutex mtx;
        return mtx;
    }

    static item_map & items() {
        static item_map items_;
        return items_;
    }

    static void stopAll() {
        std::lock_guard<std::recursive_mutex> lock(items_mutex());
        for (auto &[k,v]: items()) {
            if (v) v->stopMedia();
        }
    }

    static std::optional<double>& get_dummy_watermark() noexcept {
        static std::optional<double> dummy = std::nullopt;
        return dummy;
    }

    static media_player_item& get_item(ImGuiID id) {
        std::lock_guard<std::recursive_mutex> lock(items_mutex());
        auto& item_ptr = items()[id];
        if (!item_ptr) {
            item_ptr = std::make_shared<media_player_item>();
        }
        return *item_ptr;
    }

    static void player(std::string_view url, auto info_color, std::string_view title = "Media", long long feed_id = -1, std::string_view item_link = "", std::string_view item_title = "", std::optional<double>& initial_watermark = get_dummy_watermark(), bool prefer_tall_layout = false) {
        (void)info_color;
        (void)prefer_tall_layout;
        ImGui::PushID(url.data(), url.data() + url.size());
        try {
            auto &item = get_item(ImGui::GetID("MediaPlayer"));
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
                initial_watermark = item.watermark;
            }
            if (has_active_media) {
                ImGui::TextUnformatted(title.data(), title.data() + title.size());
                double current_pos = item.position.load();
                double current_dur = item.duration.load();
                if (current_pos > 0 && current_dur > 0) {
                    ImGui::TextColored(info_color, "%s: %s / %s",
                        item.is_paused.load() ? "Paused" : "Playing",
                        item.formatTime(current_pos).c_str(),
                        item.formatTime(current_dur).c_str());
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
                if (current_dur > 0) {
                    float progress = current_pos > 0 && current_dur > 0 ? 
                        static_cast<float>(current_pos / current_dur) : 0.0f;
                    progress = std::max(0.0f, std::min(1.0f, progress));
                    ImVec2 progress_bar_pos = ImGui::GetCursorScreenPos();
                    ImVec2 progress_bar_size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
                    ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
                    if (ImGui::IsItemClicked()) {
                        auto mouse_x = ImGui::GetIO().MousePos.x;
                        auto rel_x = (mouse_x - progress_bar_pos.x) / progress_bar_size.x;
                        rel_x = std::max(0.0f, std::min(1.0f, rel_x));
                        auto target_pos = static_cast<double>(rel_x) * current_dur;
                        item.seekTo(target_pos);
                    }
                } else {
                    ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Loading...");
                }
                // --- Video rendering via FFmpeg Engine ---
                ImTextureID tex = item.get_texture_id();
                if (tex && item.has_video) {
                    ImGui::Spacing();
                    const float dock_width = std::max(ImGui::GetContentRegionAvail().x, 160.0f);
                    const float dock_height = std::clamp(dock_width * 9.0f / 16.0f, 140.0f, 480.0f);
                    ImGui::Image(tex, ImVec2(dock_width, dock_height));
                }
            } else {
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                bool has_bookmark = item.watermark.has_value() && *item.watermark > 0.0;
                
                if (has_bookmark) {
                    std::string formatted_bookmark = item.formatTime(*item.watermark);
                    float restart_btn_w = ImGui::CalcTextSize("Restart").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    float play_btn_w = ImGui::GetContentRegionAvail().x - restart_btn_w - ImGui::GetStyle().ItemSpacing.x;
                    
                    if (ImGui::Button(std::format(" {} Resume ({})", ICON_MD_PLAY_ARROW, formatted_bookmark).c_str(), ImVec2(play_btn_w, 0))) {
                        stopAll();
                        item.start_offset = *item.watermark;
                        item.playMedia();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Restart", ImVec2(restart_btn_w, 0))) {
                        stopAll();
                        item.start_offset = 0.0;
                        item.playMedia();
                    }
                } else {
                    if (ImGui::Button(std::format(" {} {}", ICON_MD_PLAY_ARROW, title).c_str(), ImVec2(-1, 0))) {
                        stopAll();
                        item.start_offset = 0.0;
                        item.playMedia();
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
};
