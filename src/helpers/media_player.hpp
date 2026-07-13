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

    static item_map & items() {
        static item_map items_;
        return items_;
    }

    static void stopAll() {
        for (auto &[k,v]: items()) {
            v.stopMedia();
        }
    }

    static std::optional<double>& get_dummy_watermark() noexcept {
        static std::optional<double> dummy = std::nullopt;
        return dummy;
    }

    static void player(std::string_view url, auto info_color, std::string_view title = "Media", long long feed_id = -1, std::string_view item_link = "", std::string_view item_title = "", std::optional<double>& initial_watermark = get_dummy_watermark(), bool prefer_tall_layout = false) {
        (void)info_color;
        ImGui::PushID(url.data());
        try {
            auto &item {items()[ImGui::GetID("MediaPlayer")]};
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
                ImGui::TextUnformatted(title.data());
                double current_pos, current_dur;
                {
                    std::lock_guard<std::mutex> lock(item.data_mutex);
                    current_pos = item.position;
                    current_dur = item.duration;
                }
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
                // --- Video support ---
                if (item.has_video) {
                    ImGui::Spacing();
                    if (ImGui::Button("Show Video Window")) {
                        // Send MPV command to show video window (if hidden)
                        std::string show_cmd = "{\"command\":[\"set_property\",\"vid\",1]}\n";
                        item.mpv_socket.send_command(show_cmd);
                        item.last_docked_video_rect.reset();
                    }
                    ImGui::SameLine();
                    
                    bool is_tall = item.user_tall_layout_set ? item.user_tall_layout : prefer_tall_layout;
                    if (ImGui::Button(is_tall ? "Make Shorter" : "Make Taller")) {
                        item.user_tall_layout = !is_tall;
                        item.user_tall_layout_set = true;
                        item.last_docked_video_rect.reset(); // Reset to force MPV window resync on new height
                    }

                    ImGui::Spacing();
                    const float dock_width = std::max(ImGui::GetContentRegionAvail().x, 160.0f);
                    float dock_height = 0.0f;
                    if (is_tall) {
                        // Expand all the way to the lower available border
                        dock_height = std::max(140.0f, ImGui::GetContentRegionAvail().y);
                    } else {
                        const float max_dock_height = 360.0f;
                        dock_height = std::clamp(dock_width * 9.0f / 16.0f, 140.0f, max_dock_height);
                    }
                    const ImVec2 dock_size{dock_width, dock_height};
                    const ImVec2 dock_min = ImGui::GetCursorScreenPos();

                    ImGui::Dummy(dock_size);

                    const ImVec2 dock_max = ImGui::GetItemRectMax();
                    auto* draw_list = ImGui::GetWindowDrawList();
                    const auto fill_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
                    const auto border_color = ImGui::GetColorU32(ImGuiCol_Border);
                    const auto text_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    draw_list->AddRectFilled(dock_min, dock_max, fill_color, 10.0f);
                    draw_list->AddRect(dock_min, dock_max, border_color, 10.0f, 0, 2.0f);

                    constexpr const char* dock_label = "Docked MPV video window";
                    const ImVec2 label_size = ImGui::CalcTextSize(dock_label);
                    draw_list->AddText(
                        ImVec2(
                            dock_min.x + std::max(0.0f, (dock_size.x - label_size.x) * 0.5f),
                            dock_min.y + std::max(0.0f, (dock_size.y - label_size.y) * 0.5f)
                        ),
                        text_color,
                        dock_label
                    );

                    auto get_window_service = registrar::get<std::function<SDL_Window*()>>("get_window");
                    if (get_window_service) {
                        if (SDL_Window* window = (*get_window_service)()) {
                            int window_x = 0;
                            int window_y = 0;
                            SDL_GetWindowPosition(window, &window_x, &window_y);

                            int content_origin_x = window_x;
                            int content_origin_y = window_y;
#ifdef __APPLE__
                            content_origin_y -= rouen::platform::get_mac_titlebar_height(window);
#else
                            int border_top = 0;
                            int border_left = 0;
                            int border_bottom = 0;
                            int border_right = 0;
                            if (SDL_GetWindowBordersSize(window, &border_top, &border_left, &border_bottom, &border_right) == 0) {
                                content_origin_x += border_left;
                                content_origin_y += border_top;
                            }
#endif

                            float scale = 1.0f;
#ifdef __APPLE__
                            scale = rouen::platform::get_mac_backing_scale_factor(window);
#endif

                            item.syncVideoWindowRect({
                                static_cast<int>(static_cast<float>(content_origin_x) * scale + dock_min.x),
                                static_cast<int>(static_cast<float>(content_origin_y) * scale + dock_min.y - 3.0f * scale),
                                std::max(1, static_cast<int>(dock_size.x)),
                                std::max(1, static_cast<int>(dock_size.y))
                            });
                        }
                    }
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
