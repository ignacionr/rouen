#pragma once

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <format>
#include <ctime>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../fonts.hpp"
#include "../../external/IconsMaterialDesign.h"

namespace rouen::cards {

    class youtube_search : public card {
    public:
        struct youtube_result {
            std::string id;
            std::string title;
            std::string url;
            double duration{0};
            std::string duration_string;
            std::string channel;
            std::string description;
            std::string published_time;
        };

        struct shared_state {
            std::mutex mutex;
            std::vector<youtube_result> results;
            bool is_searching{false};
            std::string error_message;
            bool card_alive{true};
        };

        youtube_search(std::string_view initial_query = "") {
            // Set YouTube Red colors
            colors[0] = {0.9f, 0.1f, 0.1f, 1.0f}; // YouTube Red
            colors[1] = {0.6f, 0.05f, 0.05f, 0.7f}; // Dark Red
            
            name("YouTube Search");
            requested_fps = 10;
            width = 450.0f; // Good width for list and video player
            
            state = std::make_shared<shared_state>();
            
            if (!initial_query.empty()) {
                if (initial_query.starts_with("play:")) {
                    std::string params = std::string(initial_query.substr(5));
                    size_t separator = params.find('|');
                    if (separator != std::string::npos) {
                        std::string encoded_url = params.substr(0, separator);
                        std::string encoded_title = params.substr(separator + 1);
                        
                        play_url_trigger = ::helpers::StringHelper::url_decode(encoded_url);
                        play_title_trigger = ::helpers::StringHelper::url_decode(encoded_title);
                    }
                } else {
                    std::string decoded_query = ::helpers::StringHelper::url_decode(initial_query);
                    strncpy(search_buffer, decoded_query.c_str(), sizeof(search_buffer) - 1);
                    pending_query = decoded_query;
                    current_query = decoded_query;
                    trigger_search(decoded_query);
                }
            }
        }

        ~youtube_search() override {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->card_alive = false;
            }
        }

        std::string get_uri() const override {
            if (current_query.empty()) {
                return "youtube";
            }
            return "youtube:" + ::helpers::StringHelper::url_encode(current_query);
        }

        bool matches_uri(std::string_view uri) const override {
            return uri == "youtube" || uri.starts_with("youtube:");
        }

        void handle_uri(std::string_view uri) override {
            if (uri.starts_with("youtube:play:")) {
                std::string params = std::string(uri.substr(13));
                size_t separator = params.find('|');
                if (separator != std::string::npos) {
                    std::string encoded_url = params.substr(0, separator);
                    std::string encoded_title = params.substr(separator + 1);
                    
                    play_url_trigger = ::helpers::StringHelper::url_decode(encoded_url);
                    play_title_trigger = ::helpers::StringHelper::url_decode(encoded_title);
                }
            } else if (uri.starts_with("youtube:")) {
                std::string query = std::string(uri.substr(8));
                std::string decoded_query = ::helpers::StringHelper::url_decode(query);
                strncpy(search_buffer, decoded_query.c_str(), sizeof(search_buffer) - 1);
                pending_query = decoded_query;
                current_query = decoded_query;
                input_changed = false;
                trigger_search(decoded_query);
            }
        }

        void trigger_search(const std::string& query) {
            if (query.empty()) return;

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->is_searching = true;
                state->error_message.clear();
            }

            std::thread([shared_state_ptr = this->state, query]() {
                std::string escaped_query;
                for (char c : query) {
                    if (c == '"' || c == '\\' || c == '$' || c == '`') {
                        escaped_query += '\\';
                    }
                    escaped_query += c;
                }

#ifdef __APPLE__
                std::string cmd = std::format("export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH && yt-dlp --flat-playlist --extractor-args \"youtubetab:approximate_date\" --dump-json \"ytsearch15:{}\"", escaped_query);
#else
                std::string cmd = std::format("yt-dlp --flat-playlist --extractor-args \"youtubetab:approximate_date\" --dump-json \"ytsearch15:{}\"", escaped_query);
#endif
                std::string output = ProcessHelper::executeCommand(cmd);

                std::stringstream ss(output);
                std::string line;
                std::vector<youtube_result> temp_results;

                while (std::getline(ss, line)) {
                    if (line.empty()) continue;
                    try {
                        glz::json_t resp;
                        auto ec = glz::read_json(resp, line);
                        if (!ec) {
                            youtube_result res;
                            if (resp.contains("id") && resp["id"].is_string()) {
                                res.id = resp["id"].get<std::string>();
                            }
                            if (resp.contains("title") && resp["title"].is_string()) {
                                res.title = resp["title"].get<std::string>();
                            }
                            if (resp.contains("url") && resp["url"].is_string()) {
                                res.url = resp["url"].get<std::string>();
                            } else if (!res.id.empty()) {
                                res.url = "https://www.youtube.com/watch?v=" + res.id;
                            }
                            if (resp.contains("duration") && resp["duration"].is_number()) {
                                res.duration = resp["duration"].get<double>();
                            }
                            if (resp.contains("duration_string") && resp["duration_string"].is_string()) {
                                res.duration_string = resp["duration_string"].get<std::string>();
                            }
                            if (resp.contains("channel") && resp["channel"].is_string()) {
                                res.channel = resp["channel"].get<std::string>();
                            } else if (resp.contains("uploader") && resp["uploader"].is_string()) {
                                res.channel = resp["uploader"].get<std::string>();
                            }
                            if (resp.contains("description") && resp["description"].is_string()) {
                                res.description = resp["description"].get<std::string>();
                            }
                            if (resp.contains("timestamp") && resp["timestamp"].is_number()) {
                                double ts = resp["timestamp"].get<double>();
                                res.published_time = format_relative_time(ts);
                            } else if (resp.contains("upload_date") && resp["upload_date"].is_string()) {
                                std::string date_str = resp["upload_date"].get<std::string>();
                                if (date_str.length() == 8) {
                                    try {
                                        std::tm tm = {};
                                        tm.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
                                        tm.tm_mon = std::stoi(date_str.substr(4, 2)) - 1;
                                        tm.tm_mday = std::stoi(date_str.substr(6, 2));
                                        tm.tm_isdst = -1;
                                        time_t t = std::mktime(&tm);
                                        if (t != -1) {
                                            res.published_time = format_relative_time(static_cast<double>(t));
                                        }
                                    } catch (...) {}
                                }
                            }
                            temp_results.push_back(std::move(res));
                        }
                    } catch (...) {}
                }

                std::lock_guard<std::mutex> lock(shared_state_ptr->mutex);
                if (shared_state_ptr->card_alive) {
                    shared_state_ptr->results = std::move(temp_results);
                    shared_state_ptr->is_searching = false;
                    if (shared_state_ptr->results.empty()) {
                        shared_state_ptr->error_message = "No results found or search failed.";
                    }
                }
            }).detach();
        }

        bool render() override {
            return render_window([this]() {
                // --- Process Play Triggers under the Card's main ID stack ---
                if (!play_url_trigger.empty()) {
                    ImGui::PushID(play_url_trigger.c_str());
                    ImGuiID item_id = ImGui::GetID("MediaPlayer");
                    ImGui::PopID();
                    
                    media_player::stopAll();
                    auto& mp_item = media_player::items()[item_id];
                    mp_item.url = play_url_trigger;
                    mp_item.item_title = play_title_trigger;
                    mp_item.start_offset = 0.0;
                    mp_item.playMedia();
                    
                    currently_playing_url = play_url_trigger;
                    
                    play_url_trigger.clear();
                    play_title_trigger.clear();
                }

                // --- Delay Search Timing ---
                bool trigger_immediate = false;
                
                ImGui::Text("%s Search YouTube:", ICON_MD_SEARCH);
                ImGui::SameLine();
                
                ImGui::PushItemWidth(-1);
                if (ImGui::InputText("##ytsearch", search_buffer, sizeof(search_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    trigger_immediate = true;
                }
                
                if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                    auto pos = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(pos.x + 5, pos.y + 2),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        "Search videos..."
                    );
                }
                ImGui::PopItemWidth();
                
                std::string current_buffer_str = search_buffer;
                if (current_buffer_str != pending_query) {
                    pending_query = current_buffer_str;
                    last_input_time = std::chrono::steady_clock::now();
                    input_changed = true;
                }
                
                if (input_changed && !pending_query.empty()) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_input_time).count();
                    if (trigger_immediate || elapsed > 800) {
                        input_changed = false;
                        current_query = pending_query;
                        trigger_search(current_query);
                    }
                } else if (trigger_immediate && pending_query.empty()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->results.clear();
                    state->error_message.clear();
                    current_query.clear();
                    input_changed = false;
                }
                
                ImGui::Spacing();
                
                // --- Now Playing Region ---
                std::string active_url;
                std::string active_title;
                bool yt_playing = false;
                
                for (auto& [id, item] : media_player::items()) {
                    if (item.player_pid > 0 && (item.url.find("youtube.com") != std::string::npos || item.url.find("youtu.be") != std::string::npos)) {
                        active_url = item.url;
                        active_title = item.item_title;
                        yt_playing = true;
                        break;
                    }
                }
                
                if (yt_playing) {
                    ImGui::PushStyleColor(ImGuiCol_Text, colors[0]);
                    ImGui::Text("%s Now Playing:", ICON_MD_PLAY_CIRCLE_FILLED);
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    
                    media_player::player(active_url, colors[0], active_title);
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
                
                // --- Search Status / Results ---
                bool searching = false;
                std::string err;
                std::vector<youtube_result> results_copy;
                
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    searching = state->is_searching;
                    err = state->error_message;
                    results_copy = state->results;
                }
                
                if (currently_playing_url.empty() && yt_playing) {
                    for (const auto& item : results_copy) {
                        if (item.url == active_url) {
                            currently_playing_url = active_url;
                            break;
                        }
                    }
                }

                // --- Auto-play next result ---
                if (!currently_playing_url.empty()) {
                    ImGui::PushID(currently_playing_url.c_str());
                    ImGuiID item_id = ImGui::GetID("MediaPlayer");
                    ImGui::PopID();
                    
                    auto it = media_player::items().find(item_id);
                    if (it != media_player::items().end()) {
                        auto& item = it->second;
                        if (item.player_pid == 0 && item.duration > 0.0 && item.position >= item.duration - 3.0) {
                            size_t next_idx = std::string::npos;
                            for (size_t i = 0; i < results_copy.size(); ++i) {
                                if (results_copy[i].url == currently_playing_url) {
                                    if (i + 1 < results_copy.size()) {
                                        next_idx = i + 1;
                                    }
                                    break;
                                }
                            }
                            
                            item.position = 0.0;
                            item.duration = 0.0;
                            
                            if (next_idx != std::string::npos) {
                                play_url_trigger = results_copy[next_idx].url;
                                play_title_trigger = results_copy[next_idx].title;
                            } else {
                                currently_playing_url.clear();
                            }
                        } else if (item.player_pid == 0) {
                            currently_playing_url.clear();
                        }
                    } else {
                        currently_playing_url.clear();
                    }
                }
                
                if (searching) {
                    float time = static_cast<float>(ImGui::GetTime());
                    int dots = static_cast<int>(time * 3.0f) % 4;
                    std::string loading_text = "Searching YouTube";
                    for (int i = 0; i < dots; ++i) {
                        loading_text += ".";
                    }
                    ImGui::TextColored(colors[0], "%s", loading_text.c_str());
                } else if (!err.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", err.c_str());
                }
                
                if (!results_copy.empty()) {
                    ImGui::Text("Results:");
                    ImGui::Separator();
                    
                    float list_height = ImGui::GetContentRegionAvail().y;
                    if (list_height < 100.0f) list_height = 100.0f;
                    
                    // Remove outline (third parameter set to false)
                    ImGui::BeginChild("YouTubeResults", ImVec2(0, list_height), false);
                    
                    ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                    ImVec4 channel_color = ImVec4(text_color.x, text_color.y, text_color.z, text_color.w * 0.75f);
                    ImVec4 desc_color = ImVec4(text_color.x, text_color.y, text_color.z, text_color.w * 0.55f);
                    
                    for (size_t i = 0; i < results_copy.size(); ++i) {
                        const auto& item = results_copy[i];
                        ImGui::PushID(static_cast<int>(i));
                        
                        bool is_current = (yt_playing && item.url == active_url);
                        
                        // Draw background manually to prevent child-scroll conflicts
                        ImVec2 p_min = ImGui::GetCursorScreenPos();
                        float item_height = 80.0f;
                        ImVec2 p_max = ImVec2(p_min.x + ImGui::GetContentRegionAvail().x, p_min.y + item_height);
                        
                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        ImU32 bg_color = is_current ? ImGui::GetColorU32(ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.12f)) : ImGui::GetColorU32(ImGuiCol_FrameBg);
                        ImU32 border_color = is_current ? ImGui::GetColorU32(colors[0]) : ImGui::GetColorU32(ImGuiCol_Border);
                        float border_thickness = is_current ? 2.5f : 1.0f;
                        
                        draw_list->AddRectFilled(p_min, p_max, bg_color, 8.0f);
                        draw_list->AddRect(p_min, p_max, border_color, 8.0f, 0, border_thickness);
                        
                        ImGui::BeginGroup();
                        
                        // Set padding cursor position
                        ImGui::SetCursorScreenPos(ImVec2(p_min.x + 10.0f, p_min.y + 10.0f));
                        
                        // Centered Play/Pause Button
                        ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[1]);
                        
                        bool is_playing_now = false;
                        if (is_current) {
                            ImGui::PushID(item.url.c_str());
                            ImGuiID item_id = ImGui::GetID("MediaPlayer");
                            ImGui::PopID();
                            auto it = media_player::items().find(item_id);
                            if (it != media_player::items().end() && !it->second.is_paused.load()) {
                                is_playing_now = true;
                            }
                        }
                        
                        const char* btn_icon = is_playing_now ? ICON_MD_PAUSE : ICON_MD_PLAY_ARROW;
                        if (ImGui::Button(btn_icon, ImVec2(40, 40))) {
                            if (is_current) {
                                ImGui::PushID(item.url.c_str());
                                ImGuiID item_id = ImGui::GetID("MediaPlayer");
                                ImGui::PopID();
                                auto it = media_player::items().find(item_id);
                                if (it != media_player::items().end()) {
                                    it->second.togglePause();
                                }
                            } else {
                                play_url_trigger = item.url;
                                play_title_trigger = item.title;
                            }
                        }
                        ImGui::PopStyleColor(2);
                        
                        ImGui::SameLine(0.0f, 15.0f);
                        
                        ImGui::BeginGroup();
                        
                        // Title
                        {
                            rouen::fonts::with_font bold(rouen::fonts::FontType::Bold);
                            std::string display_title = item.title;
                            if (display_title.length() > 60) {
                                display_title = display_title.substr(0, 57) + "...";
                            }
                            if (is_current) {
                                ImGui::TextColored(colors[0], "%s %s", ICON_MD_VOLUME_UP, display_title.c_str());
                            } else {
                                ImGui::Text("%s", display_title.c_str());
                            }
                        }
                        
                        // Channel & Duration & Published Time (High contrast)
                        std::string duration_lbl = item.duration_string.empty() ? "" : "  (" + item.duration_string + ")";
                        std::string published_lbl = item.published_time.empty() ? "" : "  •  " + item.published_time;
                        ImGui::TextColored(channel_color, "%s%s%s", item.channel.c_str(), duration_lbl.c_str(), published_lbl.c_str());
                        
                        // Description snippet (High contrast)
                        if (!item.description.empty()) {
                            std::string desc = item.description;
                            if (desc.length() > 70) {
                                desc = desc.substr(0, 67) + "...";
                            }
                            ImGui::TextColored(desc_color, "%s", desc.c_str());
                        }
                        
                        ImGui::EndGroup();
                        ImGui::EndGroup();
                        
                        // Advance cursor below the item card bounds
                        ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_max.y));
                        ImGui::Spacing();
                        
                        ImGui::PopID();
                    }
                    
                    ImGui::EndChild();
                } else if (!current_query.empty() && !searching) {
                    ImGui::Text("No results found for '%s'.", current_query.c_str());
                }
            });
        }

    private:
        std::shared_ptr<shared_state> state;
        std::string current_query;
        std::string pending_query;
        char search_buffer[512] = "";
        
        std::chrono::steady_clock::time_point last_input_time;
        bool input_changed{false};

        std::string play_url_trigger;
        std::string play_title_trigger;
        std::string currently_playing_url;

        static std::string format_relative_time(double timestamp_seconds) {
            if (timestamp_seconds <= 0) return "";
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            double diff = static_cast<double>(static_cast<long long>(now) - static_cast<long long>(timestamp_seconds));
            if (diff < 0) return "just now";
            
            const double minute = 60.0;
            const double hour = minute * 60.0;
            const double day = hour * 24.0;
            const double month = day * 30.44;
            const double year = day * 365.24;
            
            if (diff < minute) {
                return "just now";
            } else if (diff < hour) {
                int mins = static_cast<int>(diff / minute);
                return std::format("{} minute{} ago", mins, mins > 1 ? "s" : "");
            } else if (diff < day) {
                int hrs = static_cast<int>(diff / hour);
                return std::format("{} hour{} ago", hrs, hrs > 1 ? "s" : "");
            } else if (diff < month) {
                int days = static_cast<int>(diff / day);
                return std::format("{} day{} ago", days, days > 1 ? "s" : "");
            } else if (diff < year) {
                int mos = static_cast<int>(diff / month);
                return std::format("{} month{} ago", mos, mos > 1 ? "s" : "");
            } else {
                int yrs = static_cast<int>(diff / year);
                return std::format("{} year{} ago", yrs, yrs > 1 ? "s" : "");
            }
        }
    };

} // namespace rouen::cards
