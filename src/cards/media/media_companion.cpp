#include "media_companion.hpp"

#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <vector>

#include "../../external/IconsMaterialDesign.h"
#include "../../fonts.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/process_helper.hpp"
#include "cards/interface/card.hpp"
#include "config_service.hpp"
#include "llm_host.hpp"
#include "media_player_item.hpp"
#include "ui_context.hpp"

namespace rouen::cards {

    namespace {

        double parse_time_str_to_seconds(const std::string& ts_str) {
            std::string clean;
            for (char const c : ts_str) {
                if (std::isdigit(c) || c == ':' || c == '.' || c == ',') {
                    if (c == ',') clean += '.';
                    else clean += c;
                }
            }
            if (clean.empty()) return 0.0;

            int h = 0, m = 0;
            double s = 0.0;
            if (sscanf(clean.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) {
                return h * 3600.0 + m * 60.0 + s;
            }
            if (sscanf(clean.c_str(), "%d:%lf", &m, &s) == 2) {
                return m * 60.0 + s;
            }
            if (sscanf(clean.c_str(), "%lf", &s) == 1) {
                return s;
            }
            return 0.0;
        }

        std::string format_seconds_to_timestamp(double sec) {
            int const total_s = static_cast<int>(sec);
            int h = total_s / 3600;
            int m = (total_s % 3600) / 60;
            int s = total_s % 60;
            if (h > 0) {
                return std::format("{:02d}:{:02d}:{:02d}", h, m, s);
            }
            return std::format("{:02d}:{:02d}", m, s);
        }

        double parse_srt_timestamp(const std::string& ts_str, std::string& formatted_str) {
            int h = 0, m = 0, s = 0, ms = 0;
            if (sscanf(ts_str.c_str(), "%d:%d:%d,%d", &h, &m, &s, &ms) == 4 ||
                sscanf(ts_str.c_str(), "%d:%d:%d.%d", &h, &m, &s, &ms) == 4) {
                double const total_sec = h * 3600.0 + m * 60.0 + s + (ms / 1000.0);
                if (h > 0) {
                    formatted_str = std::format("{:02d}:{:02d}:{:02d}", h, m, s);
                } else {
                    formatted_str = std::format("{:02d}:{:02d}", m, s);
                }
                return total_sec;
            }
            if (sscanf(ts_str.c_str(), "%d:%d,%d", &m, &s, &ms) == 3 ||
                sscanf(ts_str.c_str(), "%d:%d.%d", &m, &s, &ms) == 3) {
                double const total_sec = m * 60.0 + s + (ms / 1000.0);
                formatted_str = std::format("{:02d}:{:02d}", m, s);
                return total_sec;
            }
            formatted_str = ts_str;
            return 0.0;
        }

        std::vector<media_companion::transcript_entry> parse_srt_entries(const std::string& srt_content) {
            std::istringstream stream(srt_content);
            std::string line;
            std::vector<media_companion::transcript_entry> entries;

            double current_ts = 0.0;
            std::string current_ts_str;
            std::string current_block_text;
            std::string last_added_text;

            auto push_current_entry = [&]() {
                if (!current_block_text.empty() && !current_ts_str.empty()) {
                    size_t const end = current_block_text.find_last_not_of(" \t\r\n");
                    if (end != std::string::npos) {
                        current_block_text = current_block_text.substr(0, end + 1);
                    }
                    if (!current_block_text.empty() && current_block_text != last_added_text) {
                        entries.push_back({current_ts, current_ts_str, current_block_text});
                        last_added_text = current_block_text;
                    }
                    current_block_text.clear();
                }
            };

            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) continue;
                if (std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
                if (line.starts_with("WEBVTT") || line.starts_with("Kind:") || line.starts_with("Language:")) continue;

                auto arrow_pos = line.find("-->");
                if (arrow_pos != std::string::npos) {
                    push_current_entry();
                    std::string start_ts_raw = line.substr(0, arrow_pos);
                    size_t const s_first = start_ts_raw.find_first_not_of(" \t");
                    size_t const s_last = start_ts_raw.find_last_not_of(" \t");
                    if (s_first != std::string::npos && s_last != std::string::npos) {
                        start_ts_raw = start_ts_raw.substr(s_first, s_last - s_first + 1);
                    }
                    current_ts = parse_srt_timestamp(start_ts_raw, current_ts_str);
                    continue;
                }

                std::string clean_line;
                bool in_tag = false;
                for (char const c : line) {
                    if (c == '<') in_tag = true;
                    else if (c == '>') in_tag = false;
                    else if (!in_tag) clean_line += c;
                }

                size_t const start = clean_line.find_first_not_of(" \t");
                if (start == std::string::npos) continue;
                clean_line = clean_line.substr(start);
                size_t const end = clean_line.find_last_not_of(" \t");
                if (end != std::string::npos) clean_line = clean_line.substr(0, end + 1);

                if (!clean_line.empty()) {
                    if (!current_block_text.empty()) current_block_text += " ";
                    current_block_text += clean_line;
                }
            }
            push_current_entry();
            return entries;
        }

        std::vector<media_companion::dynamic_commentary_point> parse_ai_commentary_response(const std::string& ai_response) {
            std::vector<media_companion::dynamic_commentary_point> points;

            auto extract_field_val = [](const std::string& obj_str, const std::string& key) -> std::string {
                size_t const pos = obj_str.find("\"" + key + "\"");
                if (pos == std::string::npos) return "";
                size_t const colon = obj_str.find(':', pos);
                if (colon == std::string::npos) return "";
                size_t const q1 = obj_str.find('"', colon);
                if (q1 == std::string::npos) return "";
                std::string val;
                bool escaped = false;
                for (size_t k = q1 + 1; k < obj_str.size(); ++k) {
                    char const ch = obj_str[k];
                    if (escaped) {
                        if (ch == 'n') val += '\n';
                        else if (ch == 'r') val += '\r';
                        else if (ch == 't') val += '\t';
                        else val += ch;
                        escaped = false;
                    } else if (ch == '\\') {
                        escaped = true;
                    } else if (ch == '"') {
                        break;
                    } else {
                        val += ch;
                    }
                }
                return val;
            };

            auto extract_field_num = [](const std::string& obj_str, const std::string& key) -> double {
                size_t const pos = obj_str.find("\"" + key + "\"");
                if (pos == std::string::npos) return 0.0;
                size_t const colon = obj_str.find(':', pos);
                if (colon == std::string::npos) return 0.0;
                size_t const start = obj_str.find_first_of("-0123456789.", colon + 1);
                if (start == std::string::npos) return 0.0;
                double val = 0.0;
                if (sscanf(obj_str.c_str() + start, "%lf", &val) == 1) {
                    return val;
                }
                return 0.0;
            };

            auto process_entry = [&](std::string raw_ts_in, std::string comment, const std::vector<media_companion::fact_check_assertion>& assertions) {
                std::string raw_ts = raw_ts_in;
                while (!raw_ts.empty() && (raw_ts.front() == '[' || raw_ts.front() == '"' || raw_ts.front() == ' ' || raw_ts.front() == '#')) {
                    raw_ts.erase(raw_ts.begin());
                }
                while (!raw_ts.empty() && (raw_ts.back() == ']' || raw_ts.back() == '"' || raw_ts.back() == ' ' || raw_ts.back() == ':')) {
                    raw_ts.pop_back();
                }

                size_t const c_start = comment.find_first_not_of(" \t\r\n");
                size_t const c_end = comment.find_last_not_of(" \t\r\n");
                if (c_start != std::string::npos && c_end != std::string::npos) {
                    comment = comment.substr(c_start, c_end - c_start + 1);
                }
                if (raw_ts.empty() || comment.empty()) return;

                double const sec = parse_time_str_to_seconds(raw_ts);
                std::string const formatted_ts = format_seconds_to_timestamp(sec);
                points.push_back({sec, formatted_ts, comment, assertions});
            };

            // 1. Try parsing JSON array if present
            std::string json_str = ai_response;
            size_t code_fence = json_str.find("```json");
            if (code_fence != std::string::npos) {
                size_t const fence_start = code_fence + 7;
                size_t const fence_end = json_str.find("```", fence_start);
                if (fence_end != std::string::npos) {
                    json_str = json_str.substr(fence_start, fence_end - fence_start);
                } else {
                    json_str = json_str.substr(fence_start);
                }
            } else {
                code_fence = json_str.find("```");
                if (code_fence != std::string::npos) {
                    size_t const fence_start = code_fence + 3;
                    size_t const fence_end = json_str.find("```", fence_start);
                    if (fence_end != std::string::npos) {
                        json_str = json_str.substr(fence_start, fence_end - fence_start);
                    }
                }
            }

            size_t const arr_start = json_str.find('[');
            size_t const arr_end = json_str.rfind(']');
            if (arr_start != std::string::npos && arr_end != std::string::npos && arr_end > arr_start) {
                std::string array_content = json_str.substr(arr_start, arr_end - arr_start + 1);
                bool inside_obj = false;
                int brace_depth = 0;
                size_t obj_start = 0;
                for (size_t i = 0; i < array_content.size(); ++i) {
                    char const c = array_content[i];
                    if (c == '{') {
                        if (brace_depth == 0) {
                            obj_start = i;
                            inside_obj = true;
                        }
                        brace_depth++;
                    } else if (c == '}') {
                        brace_depth--;
                        if (brace_depth == 0 && inside_obj) {
                            std::string const obj_str = array_content.substr(obj_start, i - obj_start + 1);

                            std::string ts_val = extract_field_val(obj_str, "timestamp");
                            if (ts_val.empty()) ts_val = extract_field_val(obj_str, "time");
                            if (ts_val.empty()) ts_val = extract_field_val(obj_str, "point");

                            std::string comment_val = extract_field_val(obj_str, "comment");
                            if (comment_val.empty()) comment_val = extract_field_val(obj_str, "text");
                            if (comment_val.empty()) comment_val = extract_field_val(obj_str, "commentary");

                            std::vector<media_companion::fact_check_assertion> assertions;
                            size_t const assertions_pos = obj_str.find("\"assertions\"");
                            if (assertions_pos != std::string::npos) {
                                size_t const a_start = obj_str.find('[', assertions_pos);
                                size_t const a_end = (a_start != std::string::npos) ? obj_str.find(']', a_start) : std::string::npos;
                                if (a_start != std::string::npos && a_end != std::string::npos) {
                                    std::string a_arr = obj_str.substr(a_start, a_end - a_start + 1);
                                    int a_depth = 0;
                                    size_t sub_obj_start = 0;
                                    for (size_t k = 0; k < a_arr.size(); ++k) {
                                        char const ch = a_arr[k];
                                        if (ch == '{') {
                                            if (a_depth == 0) sub_obj_start = k;
                                            a_depth++;
                                        } else if (ch == '}') {
                                            a_depth--;
                                            if (a_depth == 0) {
                                                std::string const a_obj = a_arr.substr(sub_obj_start, k - sub_obj_start + 1);
                                                std::string claim = extract_field_val(a_obj, "claim");
                                                if (claim.empty()) claim = extract_field_val(a_obj, "statement");

                                                double score = extract_field_num(a_obj, "truth_score");
                                                if (a_obj.find("\"truth_score\"") == std::string::npos && a_obj.find("\"score\"") != std::string::npos) {
                                                    score = extract_field_num(a_obj, "score");
                                                }
                                                score = std::clamp(score, -1.0, 1.0);

                                                std::string explanation = extract_field_val(a_obj, "explanation");
                                                if (explanation.empty()) explanation = extract_field_val(a_obj, "reason");

                                                if (!claim.empty()) {
                                                    assertions.push_back({claim, score, explanation});
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            if (!ts_val.empty() && !comment_val.empty()) {
                                process_entry(ts_val, comment_val, assertions);
                            }
                            inside_obj = false;
                        }
                    }
                }
            }

            // 2. Fallback: Parse line headers e.g. [01:25] or ### 01:25 or **[01:25]**
            if (points.empty()) {
                std::istringstream iss(ai_response);
                std::string line;
                std::string current_ts;
                std::string current_comment;

                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    size_t const b_open = line.find('[');
                    size_t const b_close = line.find(']', b_open != std::string::npos ? b_open : 0);
                    bool found_header = false;
                    std::string found_ts;

                    if (b_open != std::string::npos && b_close != std::string::npos && b_close > b_open) {
                        std::string const inside = line.substr(b_open + 1, b_close - b_open - 1);
                        if (inside.find(':') != std::string::npos) {
                            found_ts = inside;
                            found_header = true;
                        }
                    }

                    if (!found_header) {
                        std::string trimmed = line;
                        size_t const s = trimmed.find_first_not_of(" \t#*");
                        if (s != std::string::npos) trimmed = trimmed.substr(s);
                        int h = 0, m = 0, sec = 0;
                        if (sscanf(trimmed.c_str(), "%d:%d:%d", &h, &m, &sec) == 3 ||
                            sscanf(trimmed.c_str(), "%d:%d", &m, &sec) == 2) {
                            found_ts = trimmed.substr(0, trimmed.find_first_of(" \t\r\n*#)]}"));
                            found_header = true;
                        }
                    }

                    if (found_header && !found_ts.empty()) {
                        if (!current_ts.empty() && !current_comment.empty()) {
                            process_entry(current_ts, current_comment, {});
                        }
                        current_ts = found_ts;
                        current_comment.clear();
                        size_t const remainder_pos = (b_close != std::string::npos && b_close > b_open) ? b_close + 1 : 0;
                        if (remainder_pos < line.size()) {
                            std::string const rem = line.substr(remainder_pos);
                            size_t const rs = rem.find_first_not_of(" \t:-*#");
                            if (rs != std::string::npos) {
                                current_comment = rem.substr(rs);
                            }
                        }
                    } else if (!current_ts.empty()) {
                        if (!current_comment.empty()) current_comment += "\n";
                        current_comment += line;
                    }
                }
                if (!current_ts.empty() && !current_comment.empty()) {
                    process_entry(current_ts, current_comment, {});
                }
            }

            std::sort(points.begin(), points.end(), [](const media_companion::dynamic_commentary_point& a, const media_companion::dynamic_commentary_point& b) {
                return a.timestamp_seconds < b.timestamp_seconds;
            });

            return points;
        }

        std::string wrap_text_soft_breaks(const std::string& input, size_t max_chars_per_line = 58) {
            std::stringstream in(input);
            std::string line;
            std::stringstream out;

            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) {
                    out << "\n";
                    continue;
                }

                size_t current_len = 0;
                std::stringstream words(line);
                std::string word;
                bool first_word = true;

                while (words >> word) {
                    if (!first_word && current_len + 1 + word.length() > max_chars_per_line) {
                        out << "\n";
                        current_len = 0;
                        first_word = true;
                    }
                    if (!first_word) {
                        out << " ";
                        current_len += 1;
                    }
                    out << word;
                    current_len += word.length();
                    first_word = false;
                }
                out << "\n";
            }
            return out.str();
        }

    } // anonymous namespace

    const std::vector<media_companion::language_option> media_companion::supported_languages = {
        {"English", "en"},
        {"Spanish", "es"},
        {"French", "fr"},
        {"German", "de"},
        {"Italian", "it"},
        {"Portuguese", "pt"},
        {"Japanese", "ja"},
        {"Chinese", "zh"},
        {"Russian", "ru"},
        {"Original / Auto-detect", "auto"}
    };

    media_companion::truth_rating_visual media_companion::get_truth_rating_visual(double score) {
        score = std::clamp(score, -1.0, 1.0);
        if (score >= 0.7) {
            return {ICON_MD_VERIFIED, "Certain Truth", ImVec4(0.25f, 0.90f, 0.45f, 1.0f)};
        }
        if (score >= 0.3) {
            return {ICON_MD_GPP_GOOD, "Mostly True", ImVec4(0.40f, 0.85f, 0.60f, 1.0f)};
        }
        if (score > -0.3) {
            return {ICON_MD_GPP_MAYBE, "Uncertain / Mixed", ImVec4(0.95f, 0.75f, 0.25f, 1.0f)};
        }
        if (score > -0.7) {
            return {ICON_MD_WARNING, "Mostly False", ImVec4(0.95f, 0.50f, 0.20f, 1.0f)};
        }
        return {ICON_MD_GPP_BAD, "Complete Lie", ImVec4(0.95f, 0.25f, 0.25f, 1.0f)};
    }

    media_companion::media_companion() {
        // High contrast aesthetic: Cyan accent on deep dark slate canvas
        colors[0] = ImVec4{0.22f, 0.62f, 0.80f, 1.0f}; // Primary accent
        colors[1] = ImVec4{0.14f, 0.42f, 0.55f, 0.8f}; // Secondary accent
        colors[2] = ImVec4{0.14f, 0.16f, 0.20f, 0.95f}; // Surface card bg
        colors[3] = ImVec4{0.07f, 0.08f, 0.11f, 1.0f}; // High contrast text area bg (deep charcoal)

        name("Media Companion");
        requested_fps = 10;
        width = 640.0f;

        state = std::make_shared<shared_state>();
        selected_llm_config = rouen::helpers::LLMConfigManager::instance().get_default_config_name();
        selected_language = "English";
        enable_fact_check = false;
        enable_web_search = true;
    }

    media_companion::~media_companion() {
        if (state) {
            std::lock_guard<std::mutex> const lock(state->mutex);
            state->card_alive = false;
        }
    }

    std::string media_companion::get_uri() const {
        return "media-companion";
    }

    bool media_companion::matches_uri(std::string_view uri) const {
        return uri == "media-companion" || uri == "media_companion";
    }

    void media_companion::seek_to(double seconds) {
        std::lock_guard<std::recursive_mutex> const lock(media_player::items_mutex());
        for (auto& [id, item_ptr] : media_player::items()) {
            if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                item_ptr->seekTo(seconds);
                break;
            }
        }
    }

    void media_companion::request_transcript() {
        std::shared_ptr<media_player_item> active_item = nullptr;
        std::string detected_url;
        std::string detected_title;
        double detected_duration = 0.0;

        {
            std::lock_guard<std::recursive_mutex> const lock(media_player::items_mutex());
            for (auto& [id, item_ptr] : media_player::items()) {
                if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                    std::string const u = item_ptr->url;
                    std::string const l = item_ptr->item_link;
                    if (u.find("youtube.com") != std::string::npos || u.find("youtu.be") != std::string::npos) {
                        detected_url = u;
                        detected_title = item_ptr->item_title;
                        detected_duration = item_ptr->duration.load();
                        active_item = item_ptr;
                    } else if (l.find("youtube.com") != std::string::npos || l.find("youtu.be") != std::string::npos) {
                        detected_url = l;
                        detected_title = item_ptr->item_title;
                        detected_duration = item_ptr->duration.load();
                        active_item = item_ptr;
                    }
                    if (active_item && !item_ptr->is_paused.load()) {
                        break;
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> const lock(state->mutex);
            if (state->is_fetching) return;

            if (!active_item || detected_url.empty()) {
                state->status_message = "No active YouTube video detected in Media Player.";
                return;
            }

            state->is_fetching = true;
            state->status_message = "Requesting transcript via yt-dlp...";
            state->video_url = detected_url;
            state->video_title = detected_title.empty() ? detected_url : detected_title;
            state->video_duration = detected_duration;
            state->plain_transcript.clear();
            state->timestamped_transcript.clear();
            state->entries.clear();
            state->commentary_points.clear();
        }

        auto st = state;
        std::thread([st, detected_url]() {
            auto pid = static_cast<unsigned long long>(getpid());
            auto timestamp = static_cast<unsigned long long>(
                std::chrono::system_clock::now().time_since_epoch().count());
            std::string out_prefix = std::format("/tmp/rouen_trans_{}_{}", pid, timestamp);

            std::string ytdlp_path = rouen::platform::find_executable("yt-dlp");
            auto config = rouen::helpers::ConfigService::instance();
            std::string const cookie_args = config ? config->get_ytdlp_cookie_args() : "";

            auto fetch_sub_file = [&ytdlp_path, &out_prefix, &detected_url](std::string_view cargs, std::string_view extra_ext_args = "") -> std::filesystem::path {
                std::string extra_flags = cargs.empty() ? "" : (" " + std::string(cargs));
                std::string ext_flags = extra_ext_args.empty() ? "" : (" " + std::string(extra_ext_args));
                std::string remote_flag = ProcessHelper::ytdlp_supports_remote_components(ytdlp_path) ? "--remote-components ejs:github" : "";
                std::string const cmd = std::format("\"{}\" -q --no-warnings {}{}{} --skip-download --write-sub --write-auto-sub "
                                              "--sub-lang \"en,es,en-US,en-GB,es-419,es-ES,.*\" --sub-format srt -o \"{}.%(ext)s\" \"{}\"",
                                              ytdlp_path, remote_flag, extra_flags, ext_flags, out_prefix, detected_url);
                ProcessHelper::executeCommand(cmd);
                try {
                    for (const auto& entry : std::filesystem::directory_iterator("/tmp")) {
                        std::string const fname = entry.path().string();
                        if (fname.starts_with(out_prefix)) {
                            return entry.path();
                        }
                    }
                } catch (...) {}
                return {};
            };

            std::filesystem::path found_file = fetch_sub_file(cookie_args);

            if (found_file.empty()) {
                if (config) {
                    config->clear_youtube_cookies();
                    if (config->refresh_youtube_cookies()) {
                        std::string const fresh_cookie_args = config->get_ytdlp_cookie_args();
                        found_file = fetch_sub_file(fresh_cookie_args);
                    }
                }
            }

            if (found_file.empty()) {
                static const std::vector<std::string_view> candidate_browsers = {"safari", "chrome", "firefox", "brave", "edge", "vivaldi", "opera", "chromium"};
                for (const auto& browser : candidate_browsers) {
                    std::string const fb_args = std::format("--cookies-from-browser {}", browser);
                    found_file = fetch_sub_file(fb_args);
                    if (found_file.empty()) {
                        found_file = fetch_sub_file(fb_args, "--extractor-args \"youtube:player_client=android_vr,android,tv\"");
                    }
                    if (!found_file.empty()) {
                        if (config) {
                            config->set_env_value("ROUEN_COOKIES_BROWSER", std::string(browser), true);
                        }
                        const char* home = getenv("HOME");
                        if (home) {
                            std::string const save_cmd = std::format("\"{}\" -q --no-warnings --cookies-from-browser {} --cookies \"{}/.config/rouen/cookies.txt\" --skip-download --playlist-items 0 \"https://www.youtube.com\" 2>&1", ytdlp_path, browser, home);
                            ProcessHelper::executeCommand(save_cmd);
                        }
                        break;
                    }
                }
            }

            if (found_file.empty()) {
                static const std::vector<std::string_view> client_specs = {
                    "--extractor-args \"youtube:player_client=android_vr,android,tv\"",
                    "--extractor-args \"youtube:player_client=android,tv\"",
                    "--extractor-args \"youtube:player_client=tv_embedded,android\""
                };
                for (const auto& cspec : client_specs) {
                    found_file = fetch_sub_file("--no-cookies", cspec);
                    if (!found_file.empty()) break;
                }
            }

            std::string raw_content;
            if (!found_file.empty() && std::filesystem::exists(found_file)) {
                std::ifstream const ifs(found_file);
                if (ifs.is_open()) {
                    std::stringstream ss;
                    ss << ifs.rdbuf();
                    raw_content = ss.str();
                }
                std::error_code ec;
                std::filesystem::remove(found_file, ec);
            }

            std::lock_guard<std::mutex> const lock(st->mutex);
            if (!st->card_alive) return;

            st->is_fetching = false;
            if (!raw_content.empty()) {
                st->entries = parse_srt_entries(raw_content);
                if (!st->entries.empty()) {
                    std::stringstream ss_plain;
                    std::stringstream ss_ts;
                    for (size_t i = 0; i < st->entries.size(); ++i) {
                        if (i > 0) {
                            ss_plain << "\n";
                            ss_ts << "\n";
                        }
                        ss_plain << st->entries[i].text;
                        ss_ts << "[" << st->entries[i].timestamp_str << "] " << st->entries[i].text;
                    }
                    st->plain_transcript = ss_plain.str();
                    st->timestamped_transcript = ss_ts.str();
                    st->status_message = std::format("Transcript obtained ({} entries). Click 'Generate AI Commentary' to analyze.", st->entries.size());
                } else {
                    st->status_message = "Downloaded subtitle file was empty.";
                }
            } else {
                st->status_message = "No YouTube transcript/subtitles found for this video.";
            }
        }).detach();
    }

    void media_companion::generate_commentary_for_full_video() {
        std::string transcript_text;
        std::string title;
        std::string const config_name = selected_llm_config;
        std::string lang_name = selected_language;
        bool const do_fact_check = enable_fact_check;
        bool const do_web_search = enable_web_search;

        {
            std::lock_guard<std::mutex> const lock(state->mutex);
            if (state->is_generating || state->timestamped_transcript.empty()) return;
            state->is_generating = true;
            transcript_text = state->timestamped_transcript;
            title = state->video_title;
            state->status_message = std::format("Generating time-marked AI commentaries for full video ({}, {}, fact-check: {})...",
                config_name.empty() ? "Default" : config_name, lang_name, do_fact_check ? "ON" : "OFF");
        }

        auto st = state;
        std::thread([st, transcript_text, title, config_name, lang_name, do_fact_check, do_web_search]() {
            std::string raw_response;
            if (transcript_text.empty()) {
                raw_response = "*No transcript available.*";
            } else if (!rouen::helpers::LLMConfig::is_configured(config_name)) {
                raw_response = std::format("*AI Model configuration '{}' is not fully configured. Please set API key in Settings -> LLM Configuration.*",
                    config_name.empty() ? "Default" : config_name);
            } else {
                auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance(config_name);
                if (!llm_instance) {
                    raw_response = std::format("*Failed to initialize LLM instance for configuration '{}'.*", config_name);
                } else {
                    auto settings = rouen::helpers::LLMConfig::get_current_config(config_name);
                    auto fetcher = std::make_shared<http::fetch>(300);

                    bool const adapter_allows_search = (settings.provider == rouen::helpers::LLMConfig::Provider::GROK ||
                                                  settings.provider == rouen::helpers::LLMConfig::Provider::GEMINI ||
                                                  settings.provider == rouen::helpers::LLMConfig::Provider::OPENAI);

                    bool const use_search = adapter_allows_search && (do_web_search || do_fact_check);
                    std::string const search_mode = use_search ? "on" : "";

                    std::string lang_directive = (lang_name == "Original / Auto-detect")
                        ? "Write all comments in the primary language of the transcript."
                        : std::format("You MUST write all comments and text in {}.", lang_name);

                    std::string fact_check_directive = do_fact_check
                        ? "FACT-CHECKING MODE (STRICT NUMERIC TRUTH SCORES):\n"
                          "Evaluate key claims, statements, and statistics made at each point in the video.\n"
                          "For each evaluated claim, assign a precise numeric 'truth_score' float between -1.0 (a complete lie / false statement) and +1.0 (a certain, verified truth).\n"
                          "- Score +1.0 = Certain, verified truth (e.g. established scientific fact, confirmed data).\n"
                          "- Score +0.5 = Mostly true / minor context missing.\n"
                          "- Score  0.0 = Uncertain, unverified, mixed, or subjective opinion.\n"
                          "- Score -0.5 = Mostly false, misleading, or deceptive statement.\n"
                          "- Score -1.0 = Complete lie, demonstrably false, or fabricated statement.\n"
                          "For each commentary item in your output array, include an 'assertions' array containing all fact check evaluations for that moment. Each assertion object MUST have:\n"
                          "  * \"claim\": string (the claim/statement evaluated)\n"
                          "  * \"truth_score\": float value between -1.0 and 1.0\n"
                          "  * \"explanation\": string (brief verification context/reasoning)\n"
                        : "";

                    llm_instance->add_instructions(
                        std::format(
                            "You are an insightful, engaging AI Media Companion analyzing complete video transcripts with time markers.\n"
                            "Your task is to analyze the entire video transcript and generate structured, time-marked comments and insights throughout the video timeline.\n\n"
                            "STRICT OUTPUT FORMAT:\n"
                            "You MUST respond strictly with a valid JSON array of objects. Each object in the array represents a point-in-time commentary with the following JSON keys:\n"
                            "- \"timestamp\": A string representing the timestamp in \"MM:SS\" or \"HH:MM:SS\" format where the commentary applies (e.g., \"01:25\", \"04:10\").\n"
                            "- \"comment\": A markdown-formatted commentary/insight for that specific moment in the video.\n"
                            "- \"assertions\": (Optional array of fact-check assertion objects):\n"
                            "    [ {{\"claim\": \"...\", \"truth_score\": 0.85, \"explanation\": \"...\"}} ]\n\n"
                            "STRICT RULES:\n"
                            "1. Identify key moments, main arguments, shifts in topic, key takeaways, facts, or interesting observations throughout the entire video.\n"
                            "2. Ensure timestamps correspond to actual moments occurring in the transcript.\n"
                            "3. Start IMMEDIATELY with the raw JSON array (or inside a ```json ``` block). Do NOT include conversational intros or sign-offs outside the JSON.\n"
                            "4. LANGUAGE: {}\n"
                            "{}\n"
                            "Keep each comment engaging, clear, and well-structured.",
                            lang_directive, fact_check_directive
                        )
                    );

                    std::string const prompt = std::format(
                        "Video Title: {}\n"
                        "Target Language: {}\n"
                        "Fact-Checking Requested: {}\n\n"
                        "Full Transcript with Time Markers:\n{}\n\n"
                        "Analyze the entire transcript and output the structured JSON array of time-marked comments.",
                        title, lang_name,
                        do_fact_check ? "YES - verify key claims, assign truth_scores (-1.0 to +1.0) and include assertion objects" : "NO",
                        transcript_text
                    );

                    try {
                        auto response = llm_instance->sendMessage(
                            prompt,
                            [fetcher](const std::string& url, const std::string& data, auto header_client) {
                                return fetcher->post(url, data, header_client);
                            },
                            "user",
                            settings.model_name,
                            search_mode
                        );
                        if (!response.choices.empty()) {
                            raw_response = response.choices[0].message.content;
                        } else {
                            raw_response = "*No text returned by AI model.*";
                        }
                    } catch (const std::exception& e) {
                        raw_response = std::format("*Error generating commentary: {}*", e.what());
                    }
                }
            }

            auto parsed_points = parse_ai_commentary_response(raw_response);

            std::lock_guard<std::mutex> const lock(st->mutex);
            if (!st->card_alive) return;

            st->is_generating = false;
            st->commentary_points = parsed_points;
            if (!parsed_points.empty()) {
                st->status_message = std::format("Generated {} time-marked AI commentaries.", parsed_points.size());
            } else if (!raw_response.empty() && raw_response.front() == '*') {
                st->status_message = raw_response;
            } else {
                st->status_message = "Failed to parse structured time-marked commentaries from AI model.";
            }
        }).detach();
    }

    bool media_companion::render(rouen::ui::ui_context& ui) {
        return render_window([this, &ui]() {
            bool fetching = false;
            bool is_generating = false;
            std::string status;
            std::string plain_text;
            std::string ts_text;
            std::vector<transcript_entry> entries_copy;
            std::vector<dynamic_commentary_point> points_copy;
            std::string title;
            std::string url;

            {
                std::lock_guard<std::mutex> const lock(state->mutex);
                fetching = state->is_fetching;
                is_generating = state->is_generating;
                status = state->status_message;
                plain_text = state->plain_transcript;
                ts_text = state->timestamped_transcript;
                entries_copy = state->entries;
                points_copy = state->commentary_points;
                title = state->video_title;
                url = state->video_url;
            }

            // Query active player position in real time
            std::string active_yt_title;
            bool yt_active = false;
            double current_pos = 0.0;
            double total_duration = 0.0;

            {
                std::lock_guard<std::recursive_mutex> const lock(media_player::items_mutex());
                for (auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                        std::string const u = item_ptr->url;
                        std::string const l = item_ptr->item_link;
                        if (u.find("youtube.com") != std::string::npos || u.find("youtu.be") != std::string::npos ||
                            l.find("youtube.com") != std::string::npos || l.find("youtu.be") != std::string::npos) {
                            yt_active = true;
                            active_yt_title = item_ptr->item_title.empty() ? u : item_ptr->item_title;
                            current_pos = item_ptr->position.load();
                            total_duration = item_ptr->duration.load();
                            if (!item_ptr->is_paused.load()) {
                                break;
                            }
                        }
                    }
                }
            }

            // Find active dynamic commentary point for current playback position
            int current_active_point = -1;
            if (yt_active && !points_copy.empty()) {
                for (size_t i = 0; i < points_copy.size(); ++i) {
                    double const start_t = points_copy[i].timestamp_seconds;
                    double const next_t = (i + 1 < points_copy.size()) ? points_copy[i + 1].timestamp_seconds : (start_t + 30.0);
                    double const dur = std::min(30.0, std::max(12.0, next_t - start_t));

                    if (current_pos >= start_t && current_pos < (start_t + dur)) {
                        current_active_point = static_cast<int>(i);
                        break;
                    }
                }
            }

            // Auto-sync position tracking
            if (auto_sync_enabled && yt_active && !points_copy.empty() && current_active_point >= 0) {
                if (current_active_point != last_synced_point_index) {
                    selected_point_index = current_active_point;
                    last_synced_point_index = current_active_point;
                }
            }

            // Ensure selected point index is within valid bounds
            if (!points_copy.empty()) {
                selected_point_index = std::clamp(selected_point_index, 0, static_cast<int>(points_copy.size()) - 1);
            }

            // Header Banner
            ui.text_colored(colors[0], std::format("{} Media Companion", ICON_MD_SUBTITLES));
            ui.separator();
            ui.spacing();

            // LLM Config & Language Dropdowns
            auto& lcm = rouen::helpers::LLMConfigManager::instance();
            const auto& llm_configs = lcm.get_configs();
            std::string current_config_label = selected_llm_config;
            if (current_config_label.empty()) {
                current_config_label = lcm.get_default_config_name();
            }

            ui.text_colored(ImVec4(0.8f, 0.8f, 0.95f, 1.0f), std::format("{} Model:", ICON_MD_SETTINGS));
            ui.same_line();
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##media_companion_llm_config", current_config_label.c_str())) {
                for (const auto& cfg : llm_configs) {
                    bool const is_selected = (selected_llm_config == cfg.name);
                    if (ImGui::Selectable(cfg.name.c_str(), is_selected)) {
                        selected_llm_config = cfg.name;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ui.same_line();
            ui.text_colored(ImVec4(0.8f, 0.8f, 0.95f, 1.0f), std::format("{} Lang:", ICON_MD_LANGUAGE));
            ui.same_line();
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##media_companion_language", selected_language.c_str())) {
                for (const auto& lang : supported_languages) {
                    bool const is_selected = (selected_language == lang.name);
                    if (ImGui::Selectable(lang.name.c_str(), is_selected)) {
                        selected_language = lang.name;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ui.spacing();

            // Active Player Info & Live Position
            if (yt_active) {
                int pos_m = static_cast<int>(current_pos) / 60;
                int pos_s = static_cast<int>(current_pos) % 60;
                int dur_m = static_cast<int>(total_duration) / 60;
                int dur_s = static_cast<int>(total_duration) % 60;

                if (current_active_point >= 0 && current_active_point < static_cast<int>(points_copy.size())) {
                    ui.text_colored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                        std::format("{} Active YouTube Media [{:02d}:{:02d} / {:02d}:{:02d}] (Point [{}]):",
                            ICON_MD_PLAY_CIRCLE_FILLED, pos_m, pos_s, dur_m, dur_s, points_copy[static_cast<size_t>(current_active_point)].timestamp_str));
                } else {
                    ui.text_colored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                        std::format("{} Active YouTube Media [{:02d}:{:02d} / {:02d}:{:02d}]:",
                            ICON_MD_PLAY_CIRCLE_FILLED, pos_m, pos_s, dur_m, dur_s));
                }
                ui.text_wrapped(active_yt_title);
            } else {
                ui.text_colored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), std::format("{} No YouTube video currently playing.", ICON_MD_INFO));
            }

            ui.spacing();

            // Request Transcript Button
            if (fetching) {
                ImGui::BeginDisabled();
                ui.button(std::format("{} Fetching Subtitles...", ICON_MD_HOURGLASS_EMPTY), ImVec2(-1, 36));
                ImGui::EndDisabled();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[0].x * 1.15f, colors[0].y * 1.15f, colors[0].z * 1.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[1]);

                if (ui.button(std::format("{} Request Transcript", ICON_MD_DESCRIPTION), ImVec2(-1, 36))) {
                    request_transcript();
                }

                ImGui::PopStyleColor(3);
            }

            if (!status.empty()) {
                ui.spacing();
                if (status.find("successfully") != std::string::npos || status.find("Generated") != std::string::npos || status.find("obtained") != std::string::npos) {
                    ui.text_colored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), std::format("{} {}", ICON_MD_CHECK_CIRCLE, status));
                } else if (status.find("No") != std::string::npos || status.find("empty") != std::string::npos || status.find("Failed") != std::string::npos) {
                    ui.text_colored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), std::format("{} {}", ICON_MD_WARNING, status));
                } else {
                    ui.text_colored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), std::format("{} {}", ICON_MD_SYNC, status));
                }
            }

            ui.spacing();
            ui.separator();
            ui.spacing();

            // Main Card View Content
            if (!entries_copy.empty()) {
                // View Mode Tabs: 0 = Dynamic AI Commentary, 1 = Timestamped Subtitles, 2 = Plain Text
                static int view_tab = 0;
                if (ui.button(std::format("{} AI Commentary ({})", ICON_MD_AUTO_AWESOME, points_copy.size()))) {
                    view_tab = 0;
                }
                ui.same_line();
                if (ui.button(std::format("{} Timestamped", ICON_MD_ACCESS_TIME))) {
                    view_tab = 1;
                }
                ui.same_line();
                if (ui.button(std::format("{} Plain Text", ICON_MD_NOTES))) {
                    view_tab = 2;
                }

                ui.spacing();

                if (view_tab == 0) {
                    // --- Dynamic AI Commentary View Mode ---
                    ui.checkbox("Auto-Sync", &auto_sync_enabled);
                    ui.same_line();
                    ui.checkbox("Fact-Check", &enable_fact_check);

                    auto current_cfg_settings = rouen::helpers::LLMConfig::get_current_config(selected_llm_config);
                    bool const adapter_allows_search = (current_cfg_settings.provider == rouen::helpers::LLMConfig::Provider::GROK ||
                                                  current_cfg_settings.provider == rouen::helpers::LLMConfig::Provider::GEMINI ||
                                                  current_cfg_settings.provider == rouen::helpers::LLMConfig::Provider::OPENAI);

                    if (adapter_allows_search) {
                        ui.same_line();
                        ui.checkbox("Web Search", &enable_web_search);
                    }

                    ui.same_line();
                    if (is_generating) {
                        ImGui::BeginDisabled();
                        ui.button(std::format("{} Analyzing Full Video...", ICON_MD_HOURGLASS_EMPTY));
                        ImGui::EndDisabled();
                    } else {
                        std::string const gen_btn_label = points_copy.empty()
                            ? std::format("{} Generate AI Commentary", ICON_MD_AUTO_AWESOME)
                            : std::format("{} Regenerate AI Commentary", ICON_MD_REFRESH);

                        if (ui.button(gen_btn_label)) {
                            generate_commentary_for_full_video();
                        }
                    }

                    ui.spacing();

                    if (!points_copy.empty()) {
                        // Horizontal Dynamic Points Selection Pills Bar
                        ui.text_colored(colors[0], std::format("{} Time-Marked Commentary Points ({}):", ICON_MD_VIEW_TIMELINE, points_copy.size()));
                        if (ui.begin_child("PointSelectorBar", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar)) {
                            for (size_t p = 0; p < points_copy.size(); ++p) {
                                if (p > 0) ui.same_line();

                                bool const is_current_playing = (yt_active && static_cast<int>(p) == current_active_point);
                                bool const is_selected = (static_cast<int>(p) == selected_point_index);

                                std::string label = points_copy[p].timestamp_str;
                                if (!points_copy[p].assertions.empty()) {
                                    label += std::format(" {}", ICON_MD_FACT_CHECK);
                                }

                                if (is_selected) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[0]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[1]);
                                } else if (is_current_playing) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.4f, 0.95f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.45f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.35f, 1.0f));
                                } else {
                                    ImGui::PushStyleColor(ImGuiCol_Button, colors[1]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[0]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[0]);
                                }

                                if (ui.button(label)) {
                                    selected_point_index = static_cast<int>(p);
                                    seek_to(points_copy[p].timestamp_seconds);
                                }

                                ImGui::PopStyleColor(3);
                            }
                        }
                        ui.end_child();

                        ui.spacing();
                        ui.separator();
                        ui.spacing();

                        // Selected Point Banner & Content
                        if (selected_point_index >= 0 && selected_point_index < static_cast<int>(points_copy.size())) {
                            const auto& pt = points_copy[static_cast<size_t>(selected_point_index)];

                            ui.text_colored(colors[0], std::format("{} Point [{}] (Point {} of {})",
                                ICON_MD_AUTO_AWESOME, pt.timestamp_str, selected_point_index + 1, points_copy.size()));

                            ui.same_line();
                            if (ui.button(std::format("{} Jump to {}", ICON_MD_PLAY_ARROW, pt.timestamp_str))) {
                                seek_to(pt.timestamp_seconds);
                            }

                            ui.spacing();

                            // Markdown Commentary Display Pane
                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.11f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.96f, 1.0f));

                            float avail_h = ImGui::GetContentRegionAvail().y;
                            if (avail_h < 150.0f) avail_h = 240.0f;

                            if (ui.begin_child("CommentaryMarkdownRegion", ImVec2(0, avail_h), true)) {
                                const rouen::helpers::markdown_render_config md_config{
                                    .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                                    .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                                    .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                                };

                                rouen::helpers::render_markdown_block(
                                    pt.commentary_md,
                                    md_config,
                                    [](const std::string& target_url) {
                                        rouen::platform::open_url(target_url);
                                    }
                                );

                                // Structured Fact-Check Assertions Display Section
                                if (!pt.assertions.empty()) {
                                    ui.spacing();
                                    ui.separator();
                                    ui.spacing();

                                    ui.text_colored(colors[0], std::format("{} Fact-Check Assertions ({})", ICON_MD_FACT_CHECK, pt.assertions.size()));
                                    ui.spacing();

                                    for (size_t a_idx = 0; a_idx < pt.assertions.size(); ++a_idx) {
                                        const auto& ass = pt.assertions[a_idx];
                                        truth_rating_visual vis = get_truth_rating_visual(ass.truth_score);

                                        ImGui::PushID(static_cast<int>(a_idx));
                                        ui.begin_group();

                                        // Rating Icon + Rating Label + Numeric Truth Score Badge
                                        ui.text_colored(vis.color, std::format("{} {} [{:+.2f}]", vis.icon, vis.label, ass.truth_score));
                                        ui.same_line();
                                        ui.text_colored(ImVec4(0.88f, 0.90f, 0.94f, 1.0f), std::format("Claim: {}", ass.claim));

                                        if (!ass.explanation.empty()) {
                                            ui.text_colored(ImVec4(0.70f, 0.72f, 0.76f, 1.0f), std::format("   Verification: {}", ass.explanation));
                                        }

                                        ui.end_group();
                                        ImGui::PopID();
                                        ui.spacing();
                                    }
                                }
                            }
                            ui.end_child();
                            ImGui::PopStyleColor(2);
                        }
                    } else {
                        ui.text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "No AI commentary points generated yet.");
                        ui.spacing();
                        if (!is_generating) {
                            if (ui.button(std::format("{} Generate AI Commentary for Full Video", ICON_MD_AUTO_AWESOME))) {
                                generate_commentary_for_full_video();
                            }
                        } else {
                            ui.text_colored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f),
                                std::format("{} Generating time-marked AI commentaries for full video transcript...", ICON_MD_HOURGLASS_EMPTY));
                        }
                    }
                } else if (view_tab == 1) {
                    // --- Timestamped Subtitles View Mode ---
                    if (ui.button(std::format("{} Copy Timestamped Subtitles", ICON_MD_CONTENT_COPY))) {
                        ImGui::SetClipboardText(ts_text.c_str());
                    }

                    ui.spacing();

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.11f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.96f, 1.0f));
                    float avail_h = ImGui::GetContentRegionAvail().y;
                    if (avail_h < 150.0f) avail_h = 240.0f;

                    if (ui.begin_child("TranscriptScrollRegion", ImVec2(0, avail_h), true)) {
                        for (size_t i = 0; i < entries_copy.size(); ++i) {
                            const auto& entry = entries_copy[i];
                            ui.begin_group();

                            ImGui::PushID(static_cast<int>(i));
                            ImGui::PushStyleColor(ImGuiCol_Button, colors[1]);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[0]);
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[0]);

                            if (ui.button(entry.timestamp_str, ImVec2(54, 0))) {
                                seek_to(entry.timestamp_seconds);
                            }

                            ImGui::PopStyleColor(3);
                            ImGui::PopID();

                            ui.same_line();
                            ui.text_wrapped(entry.text);
                            ui.end_group();
                            ui.spacing();
                        }
                    }
                    ui.end_child();
                    ImGui::PopStyleColor(2);
                } else {
                    // --- Plain Text View Mode ---
                    if (ui.button(std::format("{} Copy Plain Text", ICON_MD_CONTENT_COPY))) {
                        ImGui::SetClipboardText(plain_text.c_str());
                    }

                    ui.spacing();

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.11f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.96f, 1.0f));
                    float avail_h = ImGui::GetContentRegionAvail().y;
                    if (avail_h < 150.0f) avail_h = 240.0f;

                    if (ui.begin_child("PlainTextScrollRegion", ImVec2(0, avail_h), true)) {
                        ui.text_wrapped(plain_text);
                    }
                    ui.end_child();
                    ImGui::PopStyleColor(2);
                }
            } else if (!fetching) {
                ui.text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "Click 'Request Transcript' above to fetch YouTube subtitles for the currently playing video.");
            }
        });
    }

    void media_companion::render_video_ui() {
        std::vector<dynamic_commentary_point> points_copy;

        {
            std::lock_guard<std::mutex> const lock(state->mutex);
            points_copy = state->commentary_points;
        }

        if (points_copy.empty()) return;

        // Query active player position in real time
        bool yt_active = false;
        double current_pos = 0.0;

        {
            std::lock_guard<std::recursive_mutex> const lock(media_player::items_mutex());
            for (auto& [id, item_ptr] : media_player::items()) {
                if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                    std::string const u = item_ptr->url;
                    std::string const l = item_ptr->item_link;
                    if (u.find("youtube.com") != std::string::npos || u.find("youtu.be") != std::string::npos ||
                        l.find("youtube.com") != std::string::npos || l.find("youtu.be") != std::string::npos) {
                        yt_active = true;
                        current_pos = item_ptr->position.load();
                        if (!item_ptr->is_paused.load()) {
                            break;
                        }
                    }
                }
            }
        }

        if (!yt_active) return;

        // Find active point for current_pos
        int active_idx = -1;
        float anim_factor = 0.0f;
        double rel_sec = 0.0;
        double active_duration = 20.0;

        for (size_t i = 0; i < points_copy.size(); ++i) {
            double const start_t = points_copy[i].timestamp_seconds;
            double const next_t = (i + 1 < points_copy.size()) ? points_copy[i + 1].timestamp_seconds : (start_t + 30.0);
            double const dur = std::min(30.0, std::max(12.0, next_t - start_t));

            if (current_pos >= start_t && current_pos < (start_t + dur)) {
                active_idx = static_cast<int>(i);
                active_duration = dur;
                rel_sec = current_pos - start_t;
                break;
            }
        }

        if (active_idx < 0) return;

        // Calculate animation factor based on rel_sec and active_duration
        if (rel_sec < 0.5) {
            anim_factor = static_cast<float>(rel_sec / 0.5);
        } else if (rel_sec >= (active_duration - 0.8)) {
            anim_factor = static_cast<float>(std::max(0.0, (active_duration - rel_sec) / 0.8));
        } else {
            anim_factor = 1.0f;
        }

        if (anim_factor <= 0.005f) return; // Completely hidden when height is 0

        const auto& active_point = points_copy[static_cast<size_t>(active_idx)];

        constexpr float kFullHeight = 680.0f;
        float const current_height = kFullHeight * anim_factor;

        // Translucent dark background with matching alpha animation
        ImVec4 const bg_color = ImVec4(0.04f, 0.05f, 0.09f, 0.90f * anim_factor);
        ImVec4 const border_color = ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.60f * anim_factor);

        // Half-width text container (880px)
        ImGui::SetNextWindowPos(ImVec2(80.0f, 100.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(880.0f, current_height), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 28.0f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg_color);
        ImGui::PushStyleColor(ImGuiCol_Border, border_color);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.97f, 1.0f, anim_factor));

        if (ImGui::Begin("##MediaCompanionVideoCastOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings)) {

            // Only render text content when the box has reached its full size (anim_factor >= 0.999f)
            if (anim_factor >= 0.999f) {
                // 2.5x font size with soft line breaks (58 chars/line)
                ImGui::SetWindowFontScale(2.5f);
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 800.0f);

                std::string const header_text = std::format("[{}] ", active_point.timestamp_str);
                std::string full_text = header_text + active_point.commentary_md;

                if (!active_point.assertions.empty()) {
                    full_text += "\n\n**Fact Checks:**\n";
                    for (const auto& ass : active_point.assertions) {
                        truth_rating_visual vis = get_truth_rating_visual(ass.truth_score);
                        full_text += std::format("{} **{}** [{:+.2f}] Claim: {}\n", vis.icon, vis.label, ass.truth_score, ass.claim);
                        if (!ass.explanation.empty()) {
                            full_text += std::format("   Context: {}\n", ass.explanation);
                        }
                    }
                }

                std::string const wrapped_commentary = wrap_text_soft_breaks(full_text, 58);

                float const child_h = std::max(1.0f, current_height - 56.0f);
                if (ImGui::BeginChild("##VideoCastScrollRegion", ImVec2(816.0f, child_h), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs)) {

                    const rouen::helpers::markdown_render_config md_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    };

                    rouen::helpers::render_markdown_block(
                        wrapped_commentary,
                        md_config,
                        [](const std::string&) {}
                    );

                    // Calculate internal scroll animation progress if text height exceeds container
                    float const max_scroll_y = ImGui::GetScrollMaxY();
                    if (max_scroll_y > 0.0f) {
                        float const scroll_progress = std::clamp(static_cast<float>((rel_sec - 0.5) / std::max(1.0, active_duration - 1.3)), 0.0f, 1.0f);
                        ImGui::SetScrollY(scroll_progress * max_scroll_y);
                    }

                    ImGui::EndChild();
                }

                ImGui::PopTextWrapPos();
                ImGui::SetWindowFontScale(1.0f);
            }
        }
        ImGui::End();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    void media_companion::paint_video_surface(SDL_Surface* surface, int surface_w, int surface_h) {
        if (!surface) return;
        (void)surface_w;
        (void)surface_h;
    }

    std::vector<card::mcp_function> media_companion::get_mcp_functions() const {
        return {
            mcp_function(
                "get_media_companion_transcript",
                "Request or retrieve the YouTube transcript and dynamic time-marked AI commentaries with fact-check truth scores for the currently playing media item.",
                R"({"type":"object","properties":{}})",
                [this](const std::string&) -> std::string {
                    std::lock_guard<std::mutex> const lock(state->mutex);
                    auto escape_json = [](const std::string& input) {
                        std::string out;
                        for (char const c : input) {
                            if (c == '"') out += "\\\"";
                            else if (c == '\\') out += "\\\\";
                            else if (c == '\n') out += "\\n";
                            else if (c == '\r') out += "\\r";
                            else if (c == '\t') out += "\\t";
                            else out += c;
                        }
                        return out;
                    };

                    std::string points_json = "[";
                    for (size_t i = 0; i < state->commentary_points.size(); ++i) {
                        if (i > 0) points_json += ",";
                        const auto& pt = state->commentary_points[i];

                        std::string assertions_json = "[";
                        for (size_t k = 0; k < pt.assertions.size(); ++k) {
                            if (k > 0) assertions_json += ",";
                            const auto& ass = pt.assertions[k];
                            assertions_json += std::format(
                                R"({{"claim":"{}","truth_score":{},"explanation":"{}"}})",
                                escape_json(ass.claim), ass.truth_score, escape_json(ass.explanation));
                        }
                        assertions_json += "]";

                        points_json += std::format(
                            R"({{"timestamp":"{}","timestamp_seconds":{},"comment":"{}","assertions":{}}})",
                            escape_json(pt.timestamp_str), pt.timestamp_seconds, escape_json(pt.commentary_md), assertions_json);
                    }
                    points_json += "]";

                    return std::format(R"({{"status":"{}","title":"{}","url":"{}","selected_llm_config":"{}","selected_language":"{}","enable_fact_check":{},"enable_web_search":{},"commentary_points":{},"timestamped_transcript":"{}","plain_transcript":"{}"}})",
                        escape_json(state->status_message),
                        escape_json(state->video_title),
                        escape_json(state->video_url),
                        escape_json(selected_llm_config),
                        escape_json(selected_language),
                        enable_fact_check ? "true" : "false",
                        enable_web_search ? "true" : "false",
                        points_json,
                        escape_json(state->timestamped_transcript),
                        escape_json(state->plain_transcript));
                }
            )
        };
    }

} // namespace rouen::cards
