#pragma once

#include "media_player_item.hpp"
#include "media_player_alarm.hpp"
#include "./imgui_include.hpp"
#include "../registrar.hpp"
#include "../../external/IconsMaterialDesign.h"

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

    static void player(std::string_view url, auto info_color, std::string_view title = "Media", long long feed_id = -1, std::string_view item_link = "", std::string_view item_title = "", std::optional<double> initial_watermark = std::nullopt) noexcept {
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
