#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <format>
#include <ctime>

#include <set>

#include <fstream>
#include <filesystem>
#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../information/rss.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/platform_utils.hpp"
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
            std::string channel_id;
            std::string channel_url;
            std::string uploader_url;
            std::string description;
            std::string published_time;
        };

        struct shared_state {
            std::mutex mutex;
            std::vector<youtube_result> results;
            bool is_searching{false};
            bool is_from_cache{false};
            std::string error_message;
            bool card_alive{true};
        };

        struct YouTubeSearchCacheEntry {
            std::chrono::steady_clock::time_point timestamp;
            std::vector<youtube_result> results;
        };

        static std::recursive_mutex& cache_mutex() {
            static std::recursive_mutex mtx;
            return mtx;
        }

        static std::unordered_map<std::string, YouTubeSearchCacheEntry>& search_cache() {
            static std::unordered_map<std::string, YouTubeSearchCacheEntry> cache;
            return cache;
        }

        static std::string normalize_youtube_query(std::string_view q) {
            std::string s(q);
            size_t start = s.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) return "";
            size_t end = s.find_last_not_of(" \t\n\r");
            s = s.substr(start, end - start + 1);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            return s;
        }

        static void load_youtube_cache_from_disk() {
            static bool loaded = false;
            if (loaded) return;
            loaded = true;
            try {
                std::filesystem::path path = rouen::platform::get_user_data_path("youtube_search_cache.json");
                if (std::filesystem::exists(path)) {
                    std::ifstream f(path);
                    if (f.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        glz::json_t json;
                        if (!glz::read_json(json, content)) {
                            if (json.contains("entries") && json["entries"].is_array()) {
                                std::lock_guard<std::recursive_mutex> lock(cache_mutex());
                                for (const auto& item : json["entries"].get<std::vector<glz::json_t>>()) {
                                    if (item.contains("query") && item["query"].is_string() && item.contains("results") && item["results"].is_array()) {
                                        std::string q = item["query"].get<std::string>();
                                        YouTubeSearchCacheEntry entry;
                                        entry.timestamp = std::chrono::steady_clock::now();
                                        for (const auto& r : item["results"].get<std::vector<glz::json_t>>()) {
                                            youtube_result res;
                                            if (r.contains("id") && r["id"].is_string()) res.id = r["id"].get<std::string>();
                                            if (r.contains("title") && r["title"].is_string()) res.title = r["title"].get<std::string>();
                                            if (r.contains("url") && r["url"].is_string()) res.url = r["url"].get<std::string>();
                                            if (r.contains("duration") && r["duration"].is_number()) res.duration = r["duration"].get<double>();
                                            if (r.contains("duration_string") && r["duration_string"].is_string()) res.duration_string = r["duration_string"].get<std::string>();
                                            if (r.contains("channel") && r["channel"].is_string()) res.channel = r["channel"].get<std::string>();
                                            if (r.contains("channel_id") && r["channel_id"].is_string()) res.channel_id = r["channel_id"].get<std::string>();
                                            if (r.contains("published_time") && r["published_time"].is_string()) res.published_time = r["published_time"].get<std::string>();
                                            if (!res.id.empty() || !res.url.empty()) {
                                                entry.results.push_back(res);
                                            }
                                        }
                                        if (!entry.results.empty()) {
                                            search_cache()[q] = entry;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
        }

        static void save_youtube_cache_to_disk() {
            try {
                std::filesystem::path path = rouen::platform::get_user_data_path("youtube_search_cache.json");
                glz::json_t json;
                std::vector<glz::json_t> entries;
                {
                    std::lock_guard<std::recursive_mutex> lock(cache_mutex());
                    for (const auto& [q, entry] : search_cache()) {
                        glz::json_t item;
                        item["query"] = q;
                        std::vector<glz::json_t> res_arr;
                        for (const auto& r : entry.results) {
                            glz::json_t r_json;
                            r_json["id"] = r.id;
                            r_json["title"] = r.title;
                            r_json["url"] = r.url;
                            r_json["duration"] = r.duration;
                            r_json["duration_string"] = r.duration_string;
                            r_json["channel"] = r.channel;
                            r_json["channel_id"] = r.channel_id;
                            r_json["published_time"] = r.published_time;
                            res_arr.push_back(r_json);
                        }
                        item["results"] = res_arr;
                        entries.push_back(item);
                    }
                }
                json["entries"] = entries;
                std::string out;
                (void)glz::write_json(json, out);
                std::ofstream f(path);
                if (f.is_open()) {
                    f << out;
                }
            } catch (...) {}
        }

        youtube_search(std::string_view initial_query = "") {
            // Set YouTube Red colors
            colors[0] = {0.9f, 0.1f, 0.1f, 1.0f}; // YouTube Red
            colors[1] = {0.6f, 0.05f, 0.05f, 0.7f}; // Dark Red
            
            name("YouTube Search");
            requested_fps = 10;
            width = 450.0f; // Good width for list and video player
            
            state = std::make_shared<shared_state>();
            
            state = std::make_shared<shared_state>();
            
            if (!initial_query.empty()) {
                parse_and_apply_uri(initial_query);
            }
        }

        void parse_and_apply_uri(std::string_view uri) {
            if (uri.empty()) return;
            std::string str(uri);

            auto extract_play_info = [this](const std::string& params) {
                size_t separator = params.find('|');
                if (separator != std::string::npos) {
                    play_url_trigger = ::helpers::StringHelper::url_decode(params.substr(0, separator));
                    play_title_trigger = ::helpers::StringHelper::url_decode(params.substr(separator + 1));
                } else {
                    std::string decoded = ::helpers::StringHelper::url_decode(params);
                    if (decoded.find("youtube.com") != std::string::npos || decoded.find("youtu.be") != std::string::npos || decoded.starts_with("http://") || decoded.starts_with("https://")) {
                        play_url_trigger = decoded;
                    } else if (decoded.starts_with("v=")) {
                        play_url_trigger = "https://www.youtube.com/watch?" + decoded;
                    } else if (decoded.length() == 11 && decoded.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos) {
                        play_url_trigger = "https://www.youtube.com/watch?v=" + decoded;
                    } else {
                        play_url_trigger = decoded;
                    }
                    play_title_trigger = "YouTube Video";
                }
            };

            if (str.starts_with("youtube:play:")) {
                extract_play_info(str.substr(13));
            } else if (str.starts_with("play:")) {
                extract_play_info(str.substr(5));
            } else {
                std::string raw = str;
                if (raw.starts_with("youtube:")) {
                    raw = raw.substr(8);
                } else if (raw == "youtube") {
                    raw = "";
                }

                if (!raw.empty()) {
                    std::string decoded = ::helpers::StringHelper::url_decode(raw);
                    if (decoded.find("youtube.com") != std::string::npos || decoded.find("youtu.be") != std::string::npos || decoded.starts_with("http://") || decoded.starts_with("https://")) {
                        play_url_trigger = decoded;
                        play_title_trigger = "YouTube Video";
                    } else if (decoded.starts_with("v=")) {
                        play_url_trigger = "https://www.youtube.com/watch?" + decoded;
                        play_title_trigger = "YouTube Video";
                    } else if (decoded.length() == 11 && decoded.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos) {
                        play_url_trigger = "https://www.youtube.com/watch?v=" + decoded;
                        play_title_trigger = "YouTube Video";
                    } else {
                        memset(search_buffer, 0, sizeof(search_buffer));
                        strncpy(search_buffer, decoded.c_str(), sizeof(search_buffer) - 1);
                        pending_query = decoded;
                        current_query = decoded;
                        trigger_search(decoded);
                    }
                }
            }
        }

        void on_close() override {
            media_player::stopForOwner(this);
        }

        ~youtube_search() override {
            on_close();
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
            return uri == "youtube" || uri.starts_with("youtube:") || uri.find("youtube.com") != std::string_view::npos || uri.find("youtu.be") != std::string_view::npos;
        }

        void handle_uri(std::string_view uri) override {
            parse_and_apply_uri(uri);
        }

        static std::set<std::string>& failed_queries() {
            static std::set<std::string> set;
            return set;
        }

        void trigger_search(const std::string& query, bool force_refresh = false) {
            std::string norm_query = normalize_youtube_query(query);
            if (norm_query.empty()) return;

            if (norm_query.find("youtube.com") != std::string::npos || norm_query.find("youtu.be") != std::string::npos || norm_query.starts_with("http://") || norm_query.starts_with("https://")) {
                play_url_trigger = norm_query;
                play_title_trigger = "YouTube Video";
                return;
            } else if (norm_query.starts_with("v=")) {
                play_url_trigger = "https://www.youtube.com/watch?" + norm_query;
                play_title_trigger = "YouTube Video";
                return;
            }

            load_youtube_cache_from_disk();

            // 1. Check if query failed previously and force_refresh is not requested -> give up immediately!
            if (!force_refresh) {
                std::lock_guard<std::recursive_mutex> cache_lock(cache_mutex());
                if (failed_queries().contains(norm_query)) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->is_searching = false;
                    state->is_from_cache = false;
                    state->results.clear();
                    state->error_message = "Search failed previously. Click Find to try again.";
                    return;
                }

                auto it = search_cache().find(norm_query);
                if (it != search_cache().end()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->results = it->second.results;
                    state->is_searching = false;
                    state->is_from_cache = true;
                    state->error_message.clear();
                    return;
                }
            } else {
                std::lock_guard<std::recursive_mutex> cache_lock(cache_mutex());
                failed_queries().erase(norm_query);
            }

            // 2. Prevent concurrent yt-dlp executions
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->is_searching) {
                    return;
                }
                state->is_searching = true;
                state->is_from_cache = false;
                state->error_message.clear();
            }

            std::thread([shared_state_ptr = this->state, query, norm_query]() {
                std::string escaped_query;
                for (char c : query) {
                    if (c == '"' || c == '\\' || c == '$' || c == '`') {
                        escaped_query += '\\';
                    }
                    escaped_query += c;
                }

                std::string ytdlp_path = rouen::platform::find_executable("yt-dlp");
                auto config = rouen::helpers::ConfigService::instance();
                std::string cookie_args = config ? config->get_ytdlp_cookie_args() : "";

                auto run_yt_search = [&ytdlp_path, &escaped_query](std::string_view cargs) -> std::vector<youtube_result> {
                    std::string cflags = cargs.empty() ? "" : (" " + std::string(cargs));
                    std::string remote_flag = ProcessHelper::ytdlp_supports_remote_components(ytdlp_path) ? "--remote-components ejs:github " : "";
                    std::string cmd = std::format("\"{}\" --no-warnings --no-call-home {}--socket-timeout 10{} --flat-playlist --extractor-args \"youtubetab:approximate_date\" --dump-json \"ytsearch15:{}\"", ytdlp_path, remote_flag, cflags, escaped_query);
                    std::string output = ProcessHelper::executeCommand(cmd);

                    std::stringstream ss(output);
                    std::string line;
                    std::vector<youtube_result> temp;

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
                                if (!res.id.empty() && res.id.length() == 11 && !res.id.starts_with("UC")) {
                                    res.url = "https://www.youtube.com/watch?v=" + res.id;
                                } else if (resp.contains("url") && resp["url"].is_string()) {
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
                                if (resp.contains("channel_id") && resp["channel_id"].is_string()) {
                                    res.channel_id = resp["channel_id"].get<std::string>();
                                }
                                if (resp.contains("channel_url") && resp["channel_url"].is_string()) {
                                    res.channel_url = resp["channel_url"].get<std::string>();
                                }
                                if (resp.contains("uploader_url") && resp["uploader_url"].is_string()) {
                                    res.uploader_url = resp["uploader_url"].get<std::string>();
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
                                temp.push_back(std::move(res));
                            }
                        } catch (...) {}
                    }
                    return temp;
                };

                std::vector<youtube_result> temp_results = run_yt_search(cookie_args);

                if (temp_results.empty()) {
                    if (config) {
                        config->clear_youtube_cookies();
                        if (config->refresh_youtube_cookies()) {
                            std::string const fresh_cookie_args = config->get_ytdlp_cookie_args();
                            temp_results = run_yt_search(fresh_cookie_args);
                        }
                    }
                }

                if (temp_results.empty()) {
                    static const std::vector<std::string_view> candidate_browsers = {"safari", "chrome", "firefox", "brave", "edge", "vivaldi", "opera", "chromium"};
                    for (const auto& browser : candidate_browsers) {
                        std::string fb_args = std::format("--cookies-from-browser {}", browser);
                        temp_results = run_yt_search(fb_args);
                        if (!temp_results.empty()) {
                            if (config) {
                                config->set_env_value("ROUEN_COOKIES_BROWSER", std::string(browser), true);
                            }
                            const char* home = getenv("HOME");
                            if (home) {
                                std::string const save_cmd = std::format("\"{}\" --no-warnings --cookies-from-browser {} --cookies \"{}/.config/rouen/cookies.txt\" --skip-download --playlist-items 0 \"https://www.youtube.com\" 2>&1", ytdlp_path, browser, home);
                                ProcessHelper::executeCommand(save_cmd);
                            }
                            break;
                        }
                    }
                }

                if (temp_results.empty()) {
                    temp_results = run_yt_search("--no-cookies");
                }

                if (!temp_results.empty()) {
                    std::lock_guard<std::recursive_mutex> cache_lock(cache_mutex());
                    failed_queries().erase(norm_query);
                    search_cache()[norm_query] = YouTubeSearchCacheEntry{
                        .timestamp = std::chrono::steady_clock::now(),
                        .results = temp_results
                    };
                    save_youtube_cache_to_disk();
                } else {
                    std::lock_guard<std::recursive_mutex> cache_lock(cache_mutex());
                    failed_queries().insert(norm_query);
                }

                std::lock_guard<std::mutex> lock(shared_state_ptr->mutex);
                if (shared_state_ptr->card_alive) {
                    shared_state_ptr->results = std::move(temp_results);
                    shared_state_ptr->is_searching = false;
                    shared_state_ptr->is_from_cache = false;
                    if (shared_state_ptr->results.empty()) {
                        shared_state_ptr->error_message = "Search failed or timed out. Giving up until refreshed.";
                    }
                }
            }).detach();
        }

        bool render() override {
            auto t_a = std::chrono::high_resolution_clock::now();
            bool res = render_window([this, &t_a]() {
                // --- Process Play Triggers under the Card's main ID stack ---
                if (!play_url_trigger.empty()) {
                    media_player::stopAll();
                    auto& mp_item = media_player::get_item(play_url_trigger);
                    mp_item.url = play_url_trigger;
                    mp_item.item_title = play_title_trigger;
                    mp_item.start_offset = 0.0;
                    mp_item.playMedia(this);
                    
                    currently_playing_url = play_url_trigger;
                    
                    play_url_trigger.clear();
                    play_title_trigger.clear();
                }
                
                // Header Search Input
                input_changed = false;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
                if (ImGui::InputText("##search", search_buffer, sizeof(search_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    input_changed = true;
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Find", ImVec2(60, 0))) {
                    input_changed = true;
                }
                
                std::string q(search_buffer);
                if (input_changed && !q.empty() && q != current_query) {
                    pending_query = q;
                    last_input_time = std::chrono::steady_clock::now();
                }
                
                if (!pending_query.empty()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - last_input_time
                    ).count();
                    
                    if (elapsed >= 500) {
                        current_query = pending_query;
                        trigger_search(current_query);
                        pending_query.clear();
                    }
                }
                
                if (input_changed && q.empty()) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->results.clear();
                    state->is_searching = false;
                    state->error_message.clear();
                    current_query.clear();
                    input_changed = false;
                }
                
                ImGui::Spacing();
                
                auto t_b = std::chrono::high_resolution_clock::now();

                // --- Now Playing Region ---
                std::string active_url;
                std::string active_title;
                bool yt_playing = false;
                
                for (auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && item_ptr->player_pid > 0 && (item_ptr->url.find("youtube.com") != std::string::npos || item_ptr->url.find("youtu.be") != std::string::npos)) {
                        active_url = item_ptr->url;
                        active_title = item_ptr->item_title;
                        yt_playing = true;
                        break;
                    }
                }
                
                if (yt_playing) {
                    ImGui::PushStyleColor(ImGuiCol_Text, colors[0]);
                    ImGui::Text("%s Now Playing:", ICON_MD_PLAY_CIRCLE_FILLED);
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    
                    ImGui::TextWrapped("%s", active_title.c_str());
                    
                    ImGui::Spacing();
                    
                    // Unified In-Card Media Player Control Area
                    media_player::player(active_url, colors[0], "Now Playing Player", -1, "", active_title, media_player::get_dummy_watermark(), false, 0.0f, this);
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
                
                auto t_c = std::chrono::high_resolution_clock::now();

                // --- Search Status / Results ---
                bool searching = false;
                bool is_cache = false;
                std::string err;
                std::vector<youtube_result> results_copy;
                
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    searching = state->is_searching;
                    is_cache = state->is_from_cache;
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
                    auto it = media_player::items().find(media_player::get_item_id(currently_playing_url));
                    if (it != media_player::items().end() && it->second) {
                        auto& item = *it->second;
                        if (item.player_pid == 0 && item.duration > 0.0 && item.position >= item.duration - 3.0) {
                            for (size_t i = 0; i < results_copy.size(); ++i) {
                                if (results_copy[i].url == currently_playing_url && i + 1 < results_copy.size()) {
                                    play_url_trigger = results_copy[i + 1].url;
                                    play_title_trigger = results_copy[i + 1].title;
                                    currently_playing_url = play_url_trigger;
                                    break;
                                }
                            }
                        }
                    }
                }
                
                if (searching) {
                    std::string loading_text = "Searching YouTube via yt-dlp...";
                    ImGui::TextColored(colors[0], "%s", loading_text.c_str());
                } else if (!err.empty()) {
                    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Error: %s", err.c_str());
                } else if (!results_copy.empty()) {
                    if (is_cache) {
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.4f, 1.0f), "Showing cached results (%zu found)", results_copy.size());
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.9f, 1.0f), "Found %zu results", results_copy.size());
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    
                    float list_height = ImGui::GetContentRegionAvail().y;
                    if (list_height < 100.0f) list_height = 100.0f;
                    
                    auto rss_host = rss::getHost();

                    // Remove outline (third parameter set to false)
                    ImGui::BeginChild("YouTubeResults", ImVec2(0, list_height), false);
                    
                    ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                    ImVec4 channel_color = ImVec4(text_color.x, text_color.y, text_color.z, text_color.w * 0.75f);
                    ImVec4 desc_color = ImVec4(text_color.x, text_color.y, text_color.z, text_color.w * 0.55f);
                    
                    for (size_t i = 0; i < results_copy.size(); ++i) {
                        const auto& item = results_copy[i];
                        ImGui::PushID(static_cast<int>(i));
                        
                        bool is_current = (yt_playing && item.url == active_url);
                        
                        std::string feed_target = get_channel_feed_url(item);
                        long long subscribed_repo_id = rss_host ? rss_host->find_subscribed_youtube_feed_id(item.channel_id, feed_target, item.channel) : -1;
                        bool is_sub = (subscribed_repo_id >= 0);
                        bool is_pending = (!feed_target.empty() && subscribing_urls.contains(feed_target));

                        if (is_sub && is_pending) {
                            subscribing_urls.erase(feed_target);
                            is_pending = false;
                        }

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
                            auto it = media_player::items().find(media_player::get_item_id(item.url));
                            if (it != media_player::items().end() && it->second && !it->second->is_paused.load()) {
                                is_playing_now = true;
                            }
                        }
                        
                        const char* btn_icon = is_playing_now ? ICON_MD_PAUSE : ICON_MD_PLAY_ARROW;
                        if (ImGui::Button(btn_icon, ImVec2(40, 40))) {
                            if (is_current) {
                                auto it = media_player::items().find(media_player::get_item_id(item.url));
                                if (it != media_player::items().end() && it->second) {
                                    it->second->togglePause();
                                }
                            } else {
                                play_url_trigger = item.url;
                                play_title_trigger = item.title;
                            }
                        }
                        ImGui::PopStyleColor(2);
                        
                        ImGui::SameLine(0.0f, 15.0f);
                        
                        ImGui::BeginGroup();
                        
                        float avail_width = ImGui::GetContentRegionAvail().x - 115.0f;
                        int max_chars = static_cast<int>(avail_width / 7.0f);
                        if (max_chars < 15) max_chars = 15;

                        // Title
                        {
                            rouen::fonts::with_font bold(rouen::fonts::FontType::Bold);
                            std::string display_title = item.title;
                            size_t title_len = (max_chars > 3) ? static_cast<size_t>(max_chars - 3) : 0;
                            if (display_title.length() > static_cast<size_t>(max_chars)) {
                                display_title = display_title.substr(0, title_len) + "...";
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
                        std::string channel_line = item.channel + duration_lbl + published_lbl;
                        size_t channel_len = (max_chars > 3) ? static_cast<size_t>(max_chars - 3) : 0;
                        if (channel_line.length() > static_cast<size_t>(max_chars)) {
                            channel_line = channel_line.substr(0, channel_len) + "...";
                        }
                        ImGui::TextColored(channel_color, "%s", channel_line.c_str());
                        
                        // Inline Subscribe / RSS Feed Button next to Channel Name
                        if (!feed_target.empty() && rss_host) {
                            ImGui::SameLine(0.0f, 10.0f);
                            ImGui::PushID("sub_btn");
                            if (is_pending) {
                                ImGui::BeginDisabled(true);
                                std::string btn_label = std::format("{} Subscribing...", ICON_MD_SYNC);
                                ImGui::SmallButton(btn_label.c_str());
                                ImGui::EndDisabled();
                            } else if (is_sub) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.25f, 0.8f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.55f, 0.3f, 0.9f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.2f, 1.0f));
                                
                                std::string btn_label = std::format("{} Subscribed", ICON_MD_CHECK);
                                if (ImGui::SmallButton(btn_label.c_str())) {
                                    if (subscribed_repo_id > 0) {
                                        "create_card"_sfn(std::format("rss-feed:{}", subscribed_repo_id));
                                    } else {
                                        "create_card"_sfn("rss");
                                    }
                                }
                                ImGui::PopStyleColor(3);
                                
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Subscribed! Click to open feed in RSS Reader");
                                }
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[1]);
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
                                
                                std::string btn_label = std::format("{} Subscribe", ICON_MD_RSS_FEED);
                                if (ImGui::SmallButton(btn_label.c_str())) {
                                    subscribing_urls.insert(feed_target);
                                    rss_host->add_feed(feed_target, false);
                                    update_cached_feeds(true);
                                }
                                ImGui::PopStyleColor(3);
                                
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Subscribe to %s RSS feed", item.channel.c_str());
                                }
                            }
                            ImGui::PopID();
                        }
                        
                        // Description snippet (High contrast)
                        if (!item.description.empty()) {
                            std::string desc = item.description;
                            size_t desc_max = static_cast<size_t>(max_chars + 10);
                            size_t desc_len = (max_chars > 0) ? static_cast<size_t>(max_chars + 7) : 0;
                            if (desc.length() > desc_max) {
                                desc = desc.substr(0, desc_len) + "...";
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
                
                auto t_d = std::chrono::high_resolution_clock::now();
                double ms_total = std::chrono::duration<double, std::milli>(t_d - t_a).count();
                if (ms_total > 20.0) {
                    double d1 = std::chrono::duration<double, std::milli>(t_b - t_a).count();
                    double d2 = std::chrono::duration<double, std::milli>(t_c - t_b).count();
                    double d3 = std::chrono::duration<double, std::milli>(t_d - t_c).count();
                    std::cerr << "[YOUTUBE BREAKDOWN] total=" << ms_total << " ms (input=" << d1 << "ms, playing=" << d2 << "ms, results_loop=" << d3 << "ms)\n";
                }
            });
            return res;
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
        
        std::set<std::string> subscribing_urls;

        mutable std::mutex feeds_cache_mutex_;
        std::atomic<bool> updating_feeds_bg_{false};
        std::vector<std::shared_ptr<media::rss::feed>> cached_current_feeds_;
        std::unordered_map<std::string, long long> channel_id_to_feed_id_;
        std::unordered_map<std::string, long long> url_to_feed_id_;
        std::unordered_map<std::string, long long> title_to_feed_id_;
        std::chrono::steady_clock::time_point last_feeds_cache_time_{};

        void update_cached_feeds(bool force = false) {
            auto now = std::chrono::steady_clock::now();
            if (force || std::chrono::duration_cast<std::chrono::seconds>(now - last_feeds_cache_time_).count() >= 5) {
                last_feeds_cache_time_ = now;
                if (updating_feeds_bg_.load()) return;
                updating_feeds_bg_.store(true);
                std::thread([this]() {
                    auto rss_host = rss::getHost();
                    if (rss_host) {
                        auto feeds_list = rss_host->feeds();
                        std::unordered_map<std::string, long long> cid_map;
                        std::unordered_map<std::string, long long> url_map;
                        std::unordered_map<std::string, long long> title_map;
                        for (const auto& f : feeds_list) {
                            if (!f) continue;
                            bool is_yt = (f->feed_link.find("youtube.com") != std::string::npos || 
                                          f->source_link.find("youtube.com") != std::string::npos ||
                                          f->feed_title.find("YouTube") != std::string::npos);
                            if (!is_yt) continue;

                            if (!f->feed_link.empty()) {
                                url_map[f->feed_link] = f->repo_id;
                                size_t cid_pos = f->feed_link.find("channel_id=");
                                if (cid_pos != std::string::npos) {
                                    std::string cid = f->feed_link.substr(cid_pos + 11);
                                    size_t amp = cid.find('&');
                                    if (amp != std::string::npos) cid = cid.substr(0, amp);
                                    if (!cid.empty()) cid_map[cid] = f->repo_id;
                                }
                            }
                            if (!f->source_link.empty()) {
                                url_map[f->source_link] = f->repo_id;
                                size_t cid_pos = f->source_link.find("channel_id=");
                                if (cid_pos != std::string::npos) {
                                    std::string cid = f->source_link.substr(cid_pos + 11);
                                    size_t amp = cid.find('&');
                                    if (amp != std::string::npos) cid = cid.substr(0, amp);
                                    if (!cid.empty()) cid_map[cid] = f->repo_id;
                                }
                            }
                            if (!f->feed_title.empty()) {
                                title_map[f->feed_title] = f->repo_id;
                            }
                        }
                        std::lock_guard<std::mutex> lock(feeds_cache_mutex_);
                        cached_current_feeds_ = std::move(feeds_list);
                        channel_id_to_feed_id_ = std::move(cid_map);
                        url_to_feed_id_ = std::move(url_map);
                        title_to_feed_id_ = std::move(title_map);
                    }
                    updating_feeds_bg_.store(false);
                }).detach();
            }
        }

        static std::string get_channel_feed_url(const youtube_result& res) {
            if (!res.channel_id.empty()) {
                return "https://www.youtube.com/feeds/videos.xml?channel_id=" + res.channel_id;
            }
            if (!res.channel_url.empty()) {
                return res.channel_url;
            }
            if (!res.uploader_url.empty()) {
                return res.uploader_url;
            }
            if (!res.channel.empty()) {
                if (res.channel.starts_with("@")) {
                    return "https://www.youtube.com/" + res.channel;
                }
                return "https://www.youtube.com/@" + res.channel;
            }
            return "";
        }

        long long find_subscribed_feed_id(const youtube_result& item) const {
            std::lock_guard<std::mutex> lock(feeds_cache_mutex_);
            if (!item.channel_id.empty()) {
                auto it = channel_id_to_feed_id_.find(item.channel_id);
                if (it != channel_id_to_feed_id_.end()) return it->second;
            }
            std::string feed_url = get_channel_feed_url(item);
            if (!feed_url.empty()) {
                auto it = url_to_feed_id_.find(feed_url);
                if (it != url_to_feed_id_.end()) return it->second;
            }
            if (!item.channel.empty()) {
                auto it = title_to_feed_id_.find(item.channel);
                if (it != title_to_feed_id_.end()) return it->second;
                it = title_to_feed_id_.find("YouTube Channel: " + item.channel);
                if (it != title_to_feed_id_.end()) return it->second;
            }
            return -1;
        }

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

