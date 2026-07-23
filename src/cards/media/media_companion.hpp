#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
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
#include "../../helpers/fetch.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../helpers/markdown_renderer.hpp"
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

        struct minute_commentary {
            int minute_index{0};
            std::string timestamp_label;
            std::string transcript_context;
            std::string commentary_md;
            bool is_generating{false};
            bool generated{false};
        };

        struct shared_state {
            std::mutex mutex;
            bool is_fetching{false};
            bool is_generating_all{false};
            int generating_minute_index{-1};
            std::string status_message;
            std::string plain_transcript;
            std::string timestamped_transcript;
            std::vector<transcript_entry> entries;
            std::vector<minute_commentary> minute_commentaries;
            std::string video_title;
            std::string video_url;
            double video_duration{0.0};
            bool card_alive{true};
        };

        media_companion() {
            colors[0] = ImVec4{0.18f, 0.55f, 0.72f, 1.0f}; // Primary accent (Cyan/Slate)
            colors[1] = ImVec4{0.12f, 0.38f, 0.50f, 0.7f}; // Darker secondary
            colors[2] = ImVec4{0.20f, 0.22f, 0.26f, 0.9f}; // Surface card bg
            colors[3] = ImVec4{0.10f, 0.11f, 0.14f, 1.0f}; // Text container bg

            name("Media Companion");
            requested_fps = 10;
            width = 520.0f;

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
            double detected_duration = 0.0;

            {
                std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
                for (auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                        std::string u = item_ptr->url;
                        std::string l = item_ptr->item_link;
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
                state->video_duration = detected_duration;
                state->plain_transcript.clear();
                state->timestamped_transcript.clear();
                state->entries.clear();
                state->minute_commentaries.clear();
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
                        double max_ts = st->video_duration;
                        for (size_t i = 0; i < st->entries.size(); ++i) {
                            if (i > 0) {
                                ss_plain << "\n";
                                ss_ts << "\n";
                            }
                            ss_plain << st->entries[i].text;
                            ss_ts << "[" << st->entries[i].timestamp_str << "] " << st->entries[i].text;
                            max_ts = std::max(max_ts, st->entries[i].timestamp_seconds);
                        }
                        st->plain_transcript = ss_plain.str();
                        st->timestamped_transcript = ss_ts.str();

                        // Build minute commentaries segmentation
                        int total_minutes = std::max(1, static_cast<int>(std::floor(max_ts / 60.0)) + 1);
                        st->minute_commentaries.clear();
                        st->minute_commentaries.reserve(static_cast<size_t>(total_minutes));

                        for (int m = 0; m < total_minutes; ++m) {
                            double window_start = std::max(0.0, m * 60.0 - 10.0);
                            double window_end = (m + 1) * 60.0 + 10.0;

                            std::stringstream ss_ctx;
                            for (const auto& entry : st->entries) {
                                if (entry.timestamp_seconds >= window_start && entry.timestamp_seconds <= window_end) {
                                    ss_ctx << "[" << entry.timestamp_str << "] " << entry.text << "\n";
                                }
                            }

                            minute_commentary mc;
                            mc.minute_index = m;
                            mc.timestamp_label = std::format("{:02d}:00 - {:02d}:59", m, m);
                            mc.transcript_context = ss_ctx.str();
                            mc.is_generating = false;
                            mc.generated = false;
                            st->minute_commentaries.push_back(std::move(mc));
                        }

                        st->status_message = std::format("Transcript obtained ({} minutes segmented).", total_minutes);
                    } else {
                        st->status_message = "Downloaded subtitle file was empty.";
                    }
                } else {
                    st->status_message = "No YouTube transcript/subtitles found for this video.";
                }
            }).detach();
        }

        void generate_commentary_for_minute(int minute_idx) {
            std::string context;
            std::string title;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (minute_idx < 0 || minute_idx >= static_cast<int>(state->minute_commentaries.size())) return;
                auto& mc = state->minute_commentaries[static_cast<size_t>(minute_idx)];
                if (mc.is_generating) return;
                mc.is_generating = true;
                context = mc.transcript_context;
                title = state->video_title;
                state->status_message = std::format("Generating AI commentary for Minute {}...", minute_idx);
            }

            auto st = state;
            std::thread([st, minute_idx, context, title]() {
                std::string commentary;
                if (context.empty()) {
                    commentary = std::format("*No transcript available for Minute {}.*", minute_idx);
                } else if (!rouen::helpers::LLMConfig::is_configured()) {
                    commentary = "*AI Model not configured. Please set your API key in Settings -> LLM Configuration to enable AI commentary.*";
                } else {
                    auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance();
                    if (!llm_instance) {
                        commentary = "*Failed to initialize LLM instance.*";
                    } else {
                        auto settings = rouen::helpers::LLMConfig::get_current_config();
                        auto fetcher = std::make_shared<http::fetch>();

                        llm_instance->add_instructions(
                            "You are an insightful, engaging AI Media Companion analyzing video transcripts minute by minute. "
                            "Provide a concise, intelligent commentary and summary in Markdown format for the specified minute of the video. "
                            "Use bullet points, bold text, or key takeaways where appropriate. Keep it engaging, clear, and well-structured."
                        );

                        std::string prompt = std::format(
                            "Video Title: {}\n"
                            "Segment: Minute {} ({:02d}:00 - {:02d}:00)\n\n"
                            "Transcript Context (Minute {} plus 10s context padding):\n{}\n\n"
                            "Write an engaging, structured Markdown commentary analyzing this minute.",
                            title, minute_idx, minute_idx, minute_idx + 1, minute_idx, context
                        );

                        try {
                            auto response = llm_instance->sendMessage(
                                prompt,
                                [fetcher](const std::string& url, const std::string& data, auto header_client) {
                                    return fetcher->post(url, data, header_client);
                                },
                                "user",
                                settings.model_name
                            );
                            if (!response.choices.empty()) {
                                commentary = response.choices[0].message.content;
                            } else {
                                commentary = "*No commentary text returned by AI model.*";
                            }
                        } catch (const std::exception& e) {
                            commentary = std::format("*Error generating commentary: {}*", e.what());
                        }
                    }
                }

                std::lock_guard<std::mutex> lock(st->mutex);
                if (!st->card_alive) return;

                if (minute_idx >= 0 && minute_idx < static_cast<int>(st->minute_commentaries.size())) {
                    auto& mc = st->minute_commentaries[static_cast<size_t>(minute_idx)];
                    mc.commentary_md = commentary;
                    mc.is_generating = false;
                    mc.generated = true;
                }
                st->status_message = std::format("AI Commentary ready for Minute {}.", minute_idx);
            }).detach();
        }

        void generate_all_commentaries() {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->is_generating_all || state->minute_commentaries.empty()) return;
                state->is_generating_all = true;
                state->status_message = "Generating AI commentary for all minutes...";
            }

            auto st = state;
            std::thread([st]() {
                size_t total = 0;
                {
                    std::lock_guard<std::mutex> lock(st->mutex);
                    total = st->minute_commentaries.size();
                }

                for (size_t i = 0; i < total; ++i) {
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        if (!st->card_alive) return;
                        st->generating_minute_index = static_cast<int>(i);
                        st->status_message = std::format("Generating AI commentary for Minute {} of {}...", i, total - 1);
                        st->minute_commentaries[i].is_generating = true;
                    }

                    std::string context;
                    std::string title;
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        context = st->minute_commentaries[i].transcript_context;
                        title = st->video_title;
                    }

                    std::string commentary;
                    if (context.empty()) {
                        commentary = std::format("*No transcript available for Minute {}.*", i);
                    } else if (!rouen::helpers::LLMConfig::is_configured()) {
                        commentary = "*AI Model not configured. Please set your API key in Settings -> LLM Configuration to enable AI commentary.*";
                    } else {
                        auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance();
                        if (!llm_instance) {
                            commentary = "*Failed to initialize LLM instance.*";
                        } else {
                            auto settings = rouen::helpers::LLMConfig::get_current_config();
                            auto fetcher = std::make_shared<http::fetch>();

                            llm_instance->add_instructions(
                                "You are an insightful, engaging AI Media Companion analyzing video transcripts minute by minute. "
                                "Provide a concise, intelligent commentary and summary in Markdown format for the specified minute of the video. "
                                "Use bullet points, bold text, or key takeaways where appropriate. Keep it engaging, clear, and well-structured."
                            );

                            std::string prompt = std::format(
                                "Video Title: {}\n"
                                "Segment: Minute {} ({:02d}:00 - {:02d}:00)\n\n"
                                "Transcript Context (Minute {} plus 10s context padding):\n{}\n\n"
                                "Write an engaging, structured Markdown commentary analyzing this minute.",
                                title, static_cast<int>(i), static_cast<int>(i), static_cast<int>(i + 1), static_cast<int>(i), context
                            );

                            try {
                                auto response = llm_instance->sendMessage(
                                    prompt,
                                    [fetcher](const std::string& url, const std::string& data, auto header_client) {
                                        return fetcher->post(url, data, header_client);
                                    },
                                    "user",
                                    settings.model_name
                                );
                                if (!response.choices.empty()) {
                                    commentary = response.choices[0].message.content;
                                } else {
                                    commentary = "*No commentary text returned by AI model.*";
                                }
                            } catch (const std::exception& e) {
                                commentary = std::format("*Error generating commentary: {}*", e.what());
                            }
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        if (!st->card_alive) return;
                        st->minute_commentaries[i].commentary_md = commentary;
                        st->minute_commentaries[i].is_generating = false;
                        st->minute_commentaries[i].generated = true;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(st->mutex);
                    if (!st->card_alive) return;
                    st->is_generating_all = false;
                    st->generating_minute_index = -1;
                    st->status_message = "All minute AI commentaries generated successfully.";
                }
            }).detach();
        }

        bool render(rouen::ui::ui_context& ui) override {
            return render_window([this, &ui]() {
                bool fetching = false;
                bool generating_all = false;
                std::string status;
                std::string plain_text;
                std::string ts_text;
                std::vector<transcript_entry> entries_copy;
                std::vector<minute_commentary> commentaries_copy;
                std::string title;
                std::string url;

                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    fetching = state->is_fetching;
                    generating_all = state->is_generating_all;
                    status = state->status_message;
                    plain_text = state->plain_transcript;
                    ts_text = state->timestamped_transcript;
                    entries_copy = state->entries;
                    commentaries_copy = state->minute_commentaries;
                    title = state->video_title;
                    url = state->video_url;
                }

                // Query active player position in real time
                std::string active_yt_title;
                bool yt_active = false;
                double current_pos = 0.0;
                double total_duration = 0.0;

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
                                current_pos = item_ptr->position.load();
                                total_duration = item_ptr->duration.load();
                                if (!item_ptr->is_paused.load()) {
                                    break;
                                }
                            }
                        }
                    }
                }

                int current_minute = yt_active ? static_cast<int>(current_pos / 60.0) : 0;

                // Auto-sync position tracking
                if (auto_sync_enabled && yt_active && !commentaries_copy.empty()) {
                    if (current_minute != last_synced_minute) {
                        if (current_minute >= 0 && current_minute < static_cast<int>(commentaries_copy.size())) {
                            selected_minute = current_minute;
                        }
                        last_synced_minute = current_minute;
                    }
                }

                // Ensure selected minute is within valid bounds
                if (!commentaries_copy.empty()) {
                    selected_minute = std::clamp(selected_minute, 0, static_cast<int>(commentaries_copy.size()) - 1);
                }

                // Header Banner
                ui.text_colored(colors[0], std::format("{} Media Companion", ICON_MD_SUBTITLES));
                ui.separator();
                ui.spacing();

                // Active Player Info & Live Position
                if (yt_active) {
                    int pos_m = static_cast<int>(current_pos) / 60;
                    int pos_s = static_cast<int>(current_pos) % 60;
                    int dur_m = static_cast<int>(total_duration) / 60;
                    int dur_s = static_cast<int>(total_duration) % 60;

                    ui.text_colored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                        std::format("{} Active YouTube Media [{:02d}:{:02d} / {:02d}:{:02d}]:",
                            ICON_MD_PLAY_CIRCLE_FILLED, pos_m, pos_s, dur_m, dur_s));
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
                    if (status.find("successfully") != std::string::npos || status.find("ready") != std::string::npos || status.find("obtained") != std::string::npos) {
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

                // Main Card View Content
                if (!entries_copy.empty()) {
                    // View Mode Tabs: 0 = AI Commentary, 1 = Timestamped Subtitles, 2 = Plain Text
                    static int view_tab = 0;
                    if (ui.button(std::format("{} AI Commentary", ICON_MD_AUTO_AWESOME))) {
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
                        // --- AI Commentary View Mode ---
                        ui.checkbox("Auto-Sync with Video", &auto_sync_enabled);
                        ui.same_line();

                        if (generating_all) {
                            ImGui::BeginDisabled();
                            ui.button(std::format("{} Generating All...", ICON_MD_HOURGLASS_EMPTY));
                            ImGui::EndDisabled();
                        } else {
                            if (ui.button(std::format("{} Generate All AI Commentaries", ICON_MD_AUTO_AWESOME))) {
                                generate_all_commentaries();
                            }
                        }

                        ui.spacing();

                        // Horizontal Minute Selection Pills Bar
                        ui.text_colored(colors[0], std::format("{} Select Minute Segment:", ICON_MD_VIEW_TIMELINE));
                        if (ui.begin_child("MinuteSelectorBar", ImVec2(0, 36), false, ImGuiWindowFlags_HorizontalScrollbar)) {
                            for (size_t m = 0; m < commentaries_copy.size(); ++m) {
                                if (m > 0) ui.same_line();

                                bool is_current_playing = (yt_active && static_cast<int>(m) == current_minute);
                                bool is_selected = (static_cast<int>(m) == selected_minute);

                                std::string label = std::format("Min {}", m);
                                if (commentaries_copy[m].generated) {
                                    label += std::format(" {}", ICON_MD_CHECK);
                                }

                                if (is_selected) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[0]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[1]);
                                } else if (is_current_playing) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.4f, 0.9f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.45f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.5f, 0.35f, 1.0f));
                                } else {
                                    ImGui::PushStyleColor(ImGuiCol_Button, colors[1]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[0]);
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[0]);
                                }

                                if (ui.button(label)) {
                                    selected_minute = static_cast<int>(m);
                                }

                                ImGui::PopStyleColor(3);
                            }
                        }
                        ui.end_child();

                        ui.spacing();
                        ui.separator();
                        ui.spacing();

                        // Selected Minute Banner & Action Bar
                        if (selected_minute >= 0 && selected_minute < static_cast<int>(commentaries_copy.size())) {
                            auto& mc = commentaries_copy[static_cast<size_t>(selected_minute)];

                            ui.text_colored(colors[0], std::format("{} Minute {} Commentary ({})",
                                ICON_MD_AUTO_AWESOME, selected_minute, mc.timestamp_label));

                            ui.same_line();
                            if (ui.button(std::format("{} Jump to {:02d}:00", ICON_MD_PLAY_ARROW, selected_minute))) {
                                seek_to(selected_minute * 60.0);
                            }

                            ui.same_line();
                            if (!mc.is_generating) {
                                if (ui.button(std::format("{} Regenerate", ICON_MD_REFRESH))) {
                                    generate_commentary_for_minute(selected_minute);
                                }
                            }

                            ui.spacing();

                            // Markdown Commentary Display Pane using Rouen Markdown Renderer
                            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[3]);
                            float avail_h = ImGui::GetContentRegionAvail().y;
                            if (avail_h < 150.0f) avail_h = 240.0f;

                            if (ui.begin_child("CommentaryMarkdownRegion", ImVec2(0, avail_h), true)) {
                                if (mc.is_generating) {
                                    ui.text_colored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f),
                                        std::format("{} Generating AI commentary for Minute {}...", ICON_MD_HOURGLASS_EMPTY, selected_minute));
                                } else if (mc.generated && !mc.commentary_md.empty()) {
                                    const rouen::helpers::markdown_render_config md_config{
                                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                                    };

                                    rouen::helpers::render_markdown_block(
                                        mc.commentary_md,
                                        md_config,
                                        [](const std::string& target_url) {
                                            rouen::platform::open_url(target_url);
                                        }
                                    );
                                } else {
                                    ui.text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                        std::format("No commentary generated for Minute {} yet.", selected_minute));
                                    ui.spacing();
                                    if (ui.button(std::format("{} Generate AI Commentary for Minute {}", ICON_MD_AUTO_AWESOME, selected_minute))) {
                                        generate_commentary_for_minute(selected_minute);
                                    }
                                }
                            }
                            ui.end_child();
                            ImGui::PopStyleColor();
                        }
                    } else if (view_tab == 1) {
                        // --- Timestamped Subtitles View Mode ---
                        if (ui.button(std::format("{} Copy Timestamped Subtitles", ICON_MD_CONTENT_COPY))) {
                            ImGui::SetClipboardText(ts_text.c_str());
                        }

                        ui.spacing();

                        ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[3]);
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
                        ImGui::PopStyleColor();
                    } else {
                        // --- Plain Text View Mode ---
                        if (ui.button(std::format("{} Copy Plain Text", ICON_MD_CONTENT_COPY))) {
                            ImGui::SetClipboardText(plain_text.c_str());
                        }

                        ui.spacing();

                        ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[3]);
                        float avail_h = ImGui::GetContentRegionAvail().y;
                        if (avail_h < 150.0f) avail_h = 240.0f;

                        if (ui.begin_child("PlainTextScrollRegion", ImVec2(0, avail_h), true)) {
                            ui.text_wrapped(plain_text);
                        }
                        ui.end_child();
                        ImGui::PopStyleColor();
                    }
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
                    "Request or retrieve the YouTube transcript and minute-by-minute AI commentaries for the currently playing media item.",
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

                        std::string commentaries_json = "[";
                        for (size_t i = 0; i < state->minute_commentaries.size(); ++i) {
                            if (i > 0) commentaries_json += ",";
                            const auto& mc = state->minute_commentaries[i];
                            commentaries_json += std::format(
                                R"({{"minute":{},"label":"{}","commentary":"{}"}})",
                                mc.minute_index, escape_json(mc.timestamp_label), escape_json(mc.commentary_md));
                        }
                        commentaries_json += "]";

                        return std::format(R"({{"status":"{}","title":"{}","url":"{}","minute_commentaries":{},"timestamped_transcript":"{}","plain_transcript":"{}"}})",
                            escape_json(state->status_message),
                            escape_json(state->video_title),
                            escape_json(state->video_url),
                            commentaries_json,
                            escape_json(state->timestamped_transcript),
                            escape_json(state->plain_transcript));
                    }
                )
            };
        }

    private:
        std::shared_ptr<shared_state> state;
        int selected_minute{0};
        int last_synced_minute{-1};
        bool auto_sync_enabled{true};
    };

} // namespace rouen::cards
