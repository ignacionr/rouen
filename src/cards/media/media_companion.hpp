#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../external/IconsMaterialDesign.h"
#include "../../fonts.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/process_helper.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

    class media_companion : public card {
    public:
        struct transcript_entry {
            double timestamp_seconds{0.0};
            std::string timestamp_str;
            std::string text;
        };

        struct shared_state {
            std::mutex mutex;
            bool is_fetching{false};
            std::string status_message;
            std::string plain_transcript;
            std::string timestamped_transcript;
            std::vector<transcript_entry> entries;
            std::string video_title;
            std::string video_url;
            bool card_alive{true};
        };

        media_companion() {
            // Curated harmonious palette: Slate Blue & Cyan accent
            colors[0] = ImVec4{0.18f, 0.55f, 0.72f, 1.0f}; // Primary accent (Cyan/Slate)
            colors[1] = ImVec4{0.12f, 0.38f, 0.50f, 0.7f}; // Darker secondary
            colors[2] = ImVec4{0.20f, 0.22f, 0.26f, 0.9f}; // Surface card bg
            colors[3] = ImVec4{0.10f, 0.11f, 0.14f, 1.0f}; // Text container bg

            name("Media Companion");
            requested_fps = 10;
            width = 480.0f;

            state = std::make_shared<shared_state>();
        }

        ~media_companion() override {
            if (state) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->card_alive = false;
            }
        }

        std::string get_uri() const override {
            return "media-companion";
        }

        bool matches_uri(std::string_view uri) const override {
            return uri == "media-companion" || uri == "media_companion";
        }

        static double parse_srt_timestamp(const std::string& ts_str, std::string& formatted_str) {
            int h = 0, m = 0, s = 0, ms = 0;
            if (sscanf(ts_str.c_str(), "%d:%d:%d,%d", &h, &m, &s, &ms) == 4 ||
                sscanf(ts_str.c_str(), "%d:%d:%d.%d", &h, &m, &s, &ms) == 4) {
                double total_sec = h * 3600.0 + m * 60.0 + s + (ms / 1000.0);
                if (h > 0) {
                    formatted_str = std::format("{:02d}:{:02d}:{:02d}", h, m, s);
                } else {
                    formatted_str = std::format("{:02d}:{:02d}", m, s);
                }
                return total_sec;
            } else if (sscanf(ts_str.c_str(), "%d:%d,%d", &m, &s, &ms) == 3 ||
                       sscanf(ts_str.c_str(), "%d:%d.%d", &m, &s, &ms) == 3) {
                double total_sec = m * 60.0 + s + (ms / 1000.0);
                formatted_str = std::format("{:02d}:{:02d}", m, s);
                return total_sec;
            }
            formatted_str = ts_str;
            return 0.0;
        }

        static std::vector<transcript_entry> parse_srt_entries(const std::string& srt_content) {
            std::istringstream stream(srt_content);
            std::string line;
            std::vector<transcript_entry> entries;

            double current_ts = 0.0;
            std::string current_ts_str;
            std::string current_block_text;
            std::string last_added_text;

            auto push_current_entry = [&]() {
                if (!current_block_text.empty() && !current_ts_str.empty()) {
                    size_t end = current_block_text.find_last_not_of(" \t\r\n");
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
                    size_t s_first = start_ts_raw.find_first_not_of(" \t");
                    size_t s_last = start_ts_raw.find_last_not_of(" \t");
                    if (s_first != std::string::npos && s_last != std::string::npos) {
                        start_ts_raw = start_ts_raw.substr(s_first, s_last - s_first + 1);
                    }
                    current_ts = parse_srt_timestamp(start_ts_raw, current_ts_str);
                    continue;
                }

                // Strip VTT / HTML formatting tags
                std::string clean_line;
                bool in_tag = false;
                for (char c : line) {
                    if (c == '<') in_tag = true;
                    else if (c == '>') in_tag = false;
                    else if (!in_tag) clean_line += c;
                }

                size_t start = clean_line.find_first_not_of(" \t");
                if (start == std::string::npos) continue;
                clean_line = clean_line.substr(start);
                size_t end = clean_line.find_last_not_of(" \t");
                if (end != std::string::npos) clean_line = clean_line.substr(0, end + 1);

                if (!clean_line.empty()) {
                    if (!current_block_text.empty()) current_block_text += " ";
                    current_block_text += clean_line;
                }
            }
            push_current_entry();
            return entries;
        }

        void seek_to(double seconds) {
            std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
            for (auto& [id, item_ptr] : media_player::items()) {
                if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                    item_ptr->seekTo(seconds);
                    break;
                }
            }
        }

        void request_transcript() {
            std::shared_ptr<media_player_item> active_item = nullptr;
            std::string detected_url;
            std::string detected_title;

            {
                std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
                for (auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                        std::string u = item_ptr->url;
                        std::string l = item_ptr->item_link;
                        if (u.find("youtube.com") != std::string::npos || u.find("youtu.be") != std::string::npos) {
                            detected_url = u;
                            detected_title = item_ptr->item_title;
                            active_item = item_ptr;
                        } else if (l.find("youtube.com") != std::string::npos || l.find("youtu.be") != std::string::npos) {
                            detected_url = l;
                            detected_title = item_ptr->item_title;
                            active_item = item_ptr;
                        }
                        if (active_item && !item_ptr->is_paused.load()) {
                            break;
                        }
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->is_fetching) return;

                if (!active_item || detected_url.empty()) {
                    state->status_message = "No active YouTube video detected in Media Player.";
                    return;
                }

                state->is_fetching = true;
                state->status_message = "Requesting transcript via yt-dlp...";
                state->video_url = detected_url;
                state->video_title = detected_title.empty() ? detected_url : detected_title;
                state->plain_transcript.clear();
                state->timestamped_transcript.clear();
                state->entries.clear();
            }

            auto st = state;
            std::thread([st, detected_url]() {
                auto pid = static_cast<unsigned long long>(getpid());
                auto timestamp = static_cast<unsigned long long>(
                    std::chrono::system_clock::now().time_since_epoch().count());
                std::string out_prefix = std::format("/tmp/rouen_trans_{}_{}", pid, timestamp);

                std::string ytdlp_path = rouen::platform::find_executable("yt-dlp");
                std::string cmd = std::format("\"{}\" -q --no-warnings --skip-download --write-sub --write-auto-sub "
                                              "--sub-lang \"en,es,en-US,en-GB,es-419,es-ES,.*\" --sub-format srt -o \"{}.%(ext)s\" \"{}\"",
                                              ytdlp_path, out_prefix, detected_url);

                ProcessHelper::executeCommand(cmd);

                std::filesystem::path found_file;
                try {
                    for (const auto& entry : std::filesystem::directory_iterator("/tmp")) {
                        std::string fname = entry.path().string();
                        if (fname.starts_with(out_prefix)) {
                            found_file = entry.path();
                            break;
                        }
                    }
                } catch (...) {}

                std::string raw_content;
                if (!found_file.empty() && std::filesystem::exists(found_file)) {
                    std::ifstream ifs(found_file);
                    if (ifs.is_open()) {
                        std::stringstream ss;
                        ss << ifs.rdbuf();
                        raw_content = ss.str();
                    }
                    std::error_code ec;
                    std::filesystem::remove(found_file, ec);
                }

                std::lock_guard<std::mutex> lock(st->mutex);
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
                        st->status_message = "Transcript obtained successfully.";
                    } else {
                        st->status_message = "Downloaded subtitle file was empty.";
                    }
                } else {
                    st->status_message = "No YouTube transcript/subtitles found for this video.";
                }
            }).detach();
        }

        bool render(rouen::ui::ui_context& ui) override {
            return render_window([this, &ui]() {
                bool fetching = false;
                std::string status;
                std::string plain_text;
                std::string ts_text;
                std::vector<transcript_entry> entries_copy;
                std::string title;
                std::string url;

                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    fetching = state->is_fetching;
                    status = state->status_message;
                    plain_text = state->plain_transcript;
                    ts_text = state->timestamped_transcript;
                    entries_copy = state->entries;
                    title = state->video_title;
                    url = state->video_url;
                }

                // Check active media player item in real time for UI feedback
                std::string active_yt_title;
                bool yt_active = false;
                {
                    std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
                    for (auto& [id, item_ptr] : media_player::items()) {
                        if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                            std::string u = item_ptr->url;
                            std::string l = item_ptr->item_link;
                            if (u.find("youtube.com") != std::string::npos || u.find("youtu.be") != std::string::npos ||
                                l.find("youtube.com") != std::string::npos || l.find("youtu.be") != std::string::npos) {
                                yt_active = true;
                                active_yt_title = item_ptr->item_title.empty() ? u : item_ptr->item_title;
                                if (!item_ptr->is_paused.load()) {
                                    break;
                                }
                            }
                        }
                    }
                }

                // Header Banner
                ui.text_colored(colors[0], std::format("{} Media Companion", ICON_MD_SUBTITLES));
                ui.separator();
                ui.spacing();

                // Active Player Info
                if (yt_active) {
                    ui.text_colored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), std::format("{} Active YouTube Media Detected:", ICON_MD_PLAY_CIRCLE_FILLED));
                    ui.text_wrapped(active_yt_title);
                } else {
                    ui.text_colored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), std::format("{} No YouTube video currently playing.", ICON_MD_INFO));
                }

                ui.spacing();

                // Button to request transcript
                if (fetching) {
                    ImGui::BeginDisabled();
                    ui.button(std::format("{} Fetching Transcript...", ICON_MD_HOURGLASS_EMPTY), ImVec2(-1, 36));
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
                    if (status.find("successfully") != std::string::npos) {
                        ui.text_colored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), std::format("{} {}", ICON_MD_CHECK_CIRCLE, status));
                    } else if (status.find("No") != std::string::npos || status.find("empty") != std::string::npos) {
                        ui.text_colored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), std::format("{} {}", ICON_MD_WARNING, status));
                    } else {
                        ui.text_colored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), std::format("{} {}", ICON_MD_SYNC, status));
                    }
                }

                ui.spacing();
                ui.separator();
                ui.spacing();

                // Transcript Content Region
                if (!entries_copy.empty()) {
                    if (!title.empty()) {
                        ui.text_colored(colors[0], std::format("{} Transcript for:", ICON_MD_VIDEO_LIBRARY));
                        ui.text_wrapped(title);
                        ui.spacing();
                    }

                    // View Mode Tabs: 0 = Timestamped (Interactive), 1 = Plain Text
                    static int view_mode = 0;
                    if (ui.button(std::format("{} Timestamped", ICON_MD_ACCESS_TIME))) {
                        view_mode = 0;
                    }
                    ui.same_line();
                    if (ui.button(std::format("{} Plain Text", ICON_MD_NOTES))) {
                        view_mode = 1;
                    }

                    ui.same_line();
                    ui.dummy(ImVec2(10, 0));
                    ui.same_line();

                    if (ui.button(std::format("{} Copy", ICON_MD_CONTENT_COPY))) {
                        if (view_mode == 0) {
                            ImGui::SetClipboardText(ts_text.c_str());
                        } else {
                            ImGui::SetClipboardText(plain_text.c_str());
                        }
                    }
                    ui.same_line();
                    if (ui.button(std::format("{} Clear", ICON_MD_DELETE_OUTLINE))) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->plain_transcript.clear();
                        state->timestamped_transcript.clear();
                        state->entries.clear();
                        state->status_message.clear();
                    }

                    ui.spacing();

                    // Scrollable view box
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[3]);
                    float avail_h = ImGui::GetContentRegionAvail().y;
                    if (avail_h < 150.0f) avail_h = 240.0f;

                    if (ui.begin_child("TranscriptScrollRegion", ImVec2(0, avail_h), true)) {
                        if (view_mode == 0) {
                            // Interactive Timestamped view with seek buttons
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
                        } else {
                            // Plain text view
                            ui.text_wrapped(plain_text);
                        }
                    }
                    ui.end_child();
                    ImGui::PopStyleColor();
                } else if (!fetching) {
                    ui.text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "Click 'Request Transcript' above to fetch YouTube subtitles for the currently playing video.");
                }
            });
        }

        std::vector<mcp_function> get_mcp_functions() const override {
            return {
                mcp_function(
                    "get_media_companion_transcript",
                    "Request or retrieve the YouTube transcript (both timestamped and plain text) for the currently playing media item.",
                    R"({"type":"object","properties":{}})",
                    [this](const std::string&) -> std::string {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        auto escape_json = [](const std::string& input) {
                            std::string out;
                            for (char c : input) {
                                if (c == '"') out += "\\\"";
                                else if (c == '\\') out += "\\\\";
                                else if (c == '\n') out += "\\n";
                                else if (c == '\r') out += "\\r";
                                else if (c == '\t') out += "\\t";
                                else out += c;
                            }
                            return out;
                        };

                        return std::format(R"({{"status":"{}","title":"{}","url":"{}","timestamped_transcript":"{}","plain_transcript":"{}"}})",
                            escape_json(state->status_message),
                            escape_json(state->video_title),
                            escape_json(state->video_url),
                            escape_json(state->timestamped_transcript),
                            escape_json(state->plain_transcript));
                    }
                )
            };
        }

    private:
        std::shared_ptr<shared_state> state;
    };

} // namespace rouen::cards
