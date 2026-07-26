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
        struct language_option {
            std::string name;
            std::string code;
        };

        inline static const std::vector<language_option> supported_languages = {
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

        inline static const std::vector<int> supported_word_counts = {50, 100, 150, 300};

        struct transcript_entry {
            double timestamp_seconds{0.0};
            std::string timestamp_str;
            std::string text;
        };

        struct segment_commentary {
            int segment_index{0};
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
            int generating_segment_index{-1};
            std::string status_message;
            std::string plain_transcript;
            std::string timestamped_transcript;
            std::vector<transcript_entry> entries;
            std::vector<segment_commentary> segment_commentaries;
            std::string video_title;
            std::string video_url;
            double video_duration{0.0};
            bool card_alive{true};
        };

        media_companion() {
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
            target_word_count = 100;
            enable_fact_check = false;
            enable_web_search = true;
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
                state->segment_commentaries.clear();
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

                        // Build 2-minute segment commentaries (120 seconds per segment)
                        constexpr double kSegmentLengthSec = 120.0;
                        int total_segments = std::max(1, static_cast<int>(std::floor(max_ts / kSegmentLengthSec)) + 1);
                        st->segment_commentaries.clear();
                        st->segment_commentaries.reserve(static_cast<size_t>(total_segments));

                        for (int s = 0; s < total_segments; ++s) {
                            double window_start = std::max(0.0, s * kSegmentLengthSec - 10.0); // 10s context padding
                            double window_end = (s + 1) * kSegmentLengthSec + 10.0; // 10s context padding

                            std::stringstream ss_ctx;
                            for (const auto& entry : st->entries) {
                                if (entry.timestamp_seconds >= window_start && entry.timestamp_seconds <= window_end) {
                                    ss_ctx << "[" << entry.timestamp_str << "] " << entry.text << "\n";
                                }
                            }

                            int start_min = s * 2;
                            int end_min = s * 2 + 1;
                            segment_commentary sc;
                            sc.segment_index = s;
                            sc.timestamp_label = std::format("{:02d}:00 - {:02d}:59", start_min, end_min);
                            sc.transcript_context = ss_ctx.str();
                            sc.is_generating = false;
                            sc.generated = false;
                            st->segment_commentaries.push_back(std::move(sc));
                        }

                        st->status_message = std::format("Transcript obtained ({} 2-minute segments).", total_segments);
                    } else {
                        st->status_message = "Downloaded subtitle file was empty.";
                    }
                } else {
                    st->status_message = "No YouTube transcript/subtitles found for this video.";
                }
            }).detach();
        }

        void generate_commentary_for_segment(int segment_idx) {
            std::string context;
            std::string title;
            std::string config_name = selected_llm_config;
            std::string lang_name = selected_language;
            int words_target = target_word_count;
            bool do_fact_check = enable_fact_check;
            bool do_web_search = enable_web_search;

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (segment_idx < 0 || segment_idx >= static_cast<int>(state->segment_commentaries.size())) return;
                auto& sc = state->segment_commentaries[static_cast<size_t>(segment_idx)];
                if (sc.is_generating) return;
                sc.is_generating = true;
                context = sc.transcript_context;
                title = state->video_title;
                state->status_message = std::format("Generating AI commentary for Segment {} ({}, {}, ~{} words, fact-check: {})...",
                    segment_idx, config_name.empty() ? "Default" : config_name, lang_name, words_target, do_fact_check ? "ON" : "OFF");
            }

            auto st = state;
            std::thread([st, segment_idx, context, title, config_name, lang_name, words_target, do_fact_check, do_web_search]() {
                std::string commentary;
                if (context.empty()) {
                    commentary = std::format("*No transcript available for Segment {}.*", segment_idx);
                } else if (!rouen::helpers::LLMConfig::is_configured(config_name)) {
                    commentary = std::format("*AI Model configuration '{}' is not fully configured. Please set API key in Settings -> LLM Configuration.*",
                        config_name.empty() ? "Default" : config_name);
                } else {
                    auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance(config_name);
                    if (!llm_instance) {
                        commentary = std::format("*Failed to initialize LLM instance for configuration '{}'.*", config_name);
                    } else {
                        auto settings = rouen::helpers::LLMConfig::get_current_config(config_name);
                            auto fetcher = std::make_shared<http::fetch>(300);

                        // Check if selected AI Model provider supports web search grounding
                        bool adapter_allows_search = (settings.provider == rouen::helpers::LLMConfig::Provider::GROK ||
                                                      settings.provider == rouen::helpers::LLMConfig::Provider::GEMINI ||
                                                      settings.provider == rouen::helpers::LLMConfig::Provider::OPENAI);

                        bool use_search = adapter_allows_search && (do_web_search || do_fact_check);
                        std::string search_mode = use_search ? "on" : "";

                        std::string lang_directive = (lang_name == "Original / Auto-detect")
                            ? "Write the commentary in the primary language of the transcript."
                            : std::format("You MUST write the entire response and commentary in {}.", lang_name);

                        std::string fact_check_directive = do_fact_check
                            ? "FACT-CHECKING MODE: Actively verify and fact-check key claims, statistics, and statements in this transcript segment. "
                              "Include a dedicated section '### Fact-Check & Verification' in your Markdown output noting verified facts, unverified claims, or inaccuracies."
                            : "";

                        llm_instance->add_instructions(
                            std::format(
                                "You are an insightful, engaging AI Media Companion analyzing video transcripts in 2-minute segments. "
                                "Provide a concise, intelligent commentary and summary in Markdown format for the specified 2-minute segment of the video.\n\n"
                                "STRICT OUTPUT RULES:\n"
                                "1. STRICT WORD COUNT: Your total output MUST be strictly target approximately {} words (do not write significantly more or fewer words).\n"
                                "2. NO PREAMBLE / NO INTRO: Start IMMEDIATELY with the commentary body or header. Do NOT include conversational intros (e.g. 'Here is a commentary...', 'Here is the summary...', 'Sure!', 'Below is...', 'In this segment...').\n"
                                "3. NO CLOSING / NO FOLLOW-UP: Stop IMMEDIATELY after completing the commentary. Do NOT include follow-up questions, suggestions, or sign-offs (e.g. 'Let me know if you want more details', 'Would you like further analysis?', 'Hope this helps!').\n"
                                "4. LANGUAGE: {}\n"
                                "{}\n"
                                "Use bullet points, bold text, or key takeaways where appropriate. Keep it clear, engaging, and well-structured.",
                                words_target, lang_directive, fact_check_directive
                            )
                        );

                        int start_m = segment_idx * 2;
                        int end_m = start_m + 2;
                        std::string prompt = std::format(
                            "Video Title: {}\n"
                            "Time Segment: Segment {} ({:02d}:00 - {:02d}:00, 2-minute block)\n"
                            "Target Language: {}\n"
                            "Strict Word Count Target: Exactly ~{} words (STRICT REQUIREMENT).\n"
                            "Fact-Checking Requested: {}\n\n"
                            "Transcript Context (2-minute block plus 10s context padding before and after):\n{}\n\n"
                            "Write an engaging, structured Markdown commentary adhering strictly to ~{} words. Start directly without any introductory preamble ('Here is...') and end cleanly without any follow-up suggestions or questions.",
                            title, segment_idx, start_m, end_m, lang_name, words_target,
                            do_fact_check ? "YES - verify claims and provide a Fact-Check & Verification section" : "NO",
                            context, words_target
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

                if (segment_idx >= 0 && segment_idx < static_cast<int>(st->segment_commentaries.size())) {
                    auto& sc = st->segment_commentaries[static_cast<size_t>(segment_idx)];
                    sc.commentary_md = commentary;
                    sc.is_generating = false;
                    sc.generated = true;
                }
                st->status_message = std::format("AI Commentary ready for Segment {}.", segment_idx);
            }).detach();
        }

        void generate_all_commentaries() {
            std::string config_name = selected_llm_config;
            std::string lang_name = selected_language;
            int words_target = target_word_count;
            bool do_fact_check = enable_fact_check;
            bool do_web_search = enable_web_search;

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->is_generating_all || state->segment_commentaries.empty()) return;
                state->is_generating_all = true;
                state->status_message = std::format("Generating AI commentary for all 2-minute segments ({}, {}, ~{} words, fact-check: {})...",
                    config_name.empty() ? "Default" : config_name, lang_name, words_target, do_fact_check ? "ON" : "OFF");
            }

            auto st = state;
            std::thread([st, config_name, lang_name, words_target, do_fact_check, do_web_search]() {
                size_t total = 0;
                {
                    std::lock_guard<std::mutex> lock(st->mutex);
                    total = st->segment_commentaries.size();
                }

                for (size_t i = 0; i < total; ++i) {
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        if (!st->card_alive) return;
                        st->generating_segment_index = static_cast<int>(i);
                        st->status_message = std::format("Generating AI commentary for Segment {} of {} (in {}, ~{} words)...", i, total - 1, lang_name, words_target);
                        st->segment_commentaries[i].is_generating = true;
                    }

                    std::string context;
                    std::string title;
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        context = st->segment_commentaries[i].transcript_context;
                        title = st->video_title;
                    }

                    std::string commentary;
                    if (context.empty()) {
                        commentary = std::format("*No transcript available for Segment {}.*", i);
                    } else if (!rouen::helpers::LLMConfig::is_configured(config_name)) {
                        commentary = std::format("*AI Model configuration '{}' is not fully configured. Please set API key in Settings -> LLM Configuration.*",
                            config_name.empty() ? "Default" : config_name);
                    } else {
                        auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance(config_name);
                        if (!llm_instance) {
                            commentary = std::format("*Failed to initialize LLM instance for configuration '{}'.*", config_name);
                        } else {
                            auto settings = rouen::helpers::LLMConfig::get_current_config(config_name);
                                auto fetcher = std::make_shared<http::fetch>(300);

                            bool adapter_allows_search = (settings.provider == rouen::helpers::LLMConfig::Provider::GROK ||
                                                          settings.provider == rouen::helpers::LLMConfig::Provider::GEMINI ||
                                                          settings.provider == rouen::helpers::LLMConfig::Provider::OPENAI);

                            bool use_search = adapter_allows_search && (do_web_search || do_fact_check);
                            std::string search_mode = use_search ? "on" : "";

                            std::string lang_directive = (lang_name == "Original / Auto-detect")
                                ? "Write the commentary in the primary language of the transcript."
                                : std::format("You MUST write the entire response and commentary in {}.", lang_name);

                            std::string fact_check_directive = do_fact_check
                                ? "FACT-CHECKING MODE: Actively verify and fact-check key claims, statistics, and statements in this transcript segment. "
                                  "Include a dedicated section '### Fact-Check & Verification' in your Markdown output noting verified facts, unverified claims, or inaccuracies."
                                : "";

                            llm_instance->add_instructions(
                                std::format(
                                    "You are an insightful, engaging AI Media Companion analyzing video transcripts in 2-minute segments. "
                                    "Provide a concise, intelligent commentary and summary in Markdown format for the specified 2-minute segment of the video.\n\n"
                                    "STRICT OUTPUT RULES:\n"
                                    "1. STRICT WORD COUNT: Your total output MUST be strictly target approximately {} words (do not write significantly more or fewer words).\n"
                                    "2. NO PREAMBLE / NO INTRO: Start IMMEDIATELY with the commentary body or header. Do NOT include conversational intros (e.g. 'Here is a commentary...', 'Here is the summary...', 'Sure!', 'Below is...', 'In this segment...').\n"
                                    "3. NO CLOSING / NO FOLLOW-UP: Stop IMMEDIATELY after completing the commentary. Do NOT include follow-up questions, suggestions, or sign-offs (e.g. 'Let me know if you want more details', 'Would you like further analysis?', 'Hope this helps!').\n"
                                    "4. LANGUAGE: {}\n"
                                    "{}\n"
                                    "Use bullet points, bold text, or key takeaways where appropriate. Keep it clear, engaging, and well-structured.",
                                    words_target, lang_directive, fact_check_directive
                                )
                            );

                            int start_m = static_cast<int>(i) * 2;
                            int end_m = start_m + 2;
                            std::string prompt = std::format(
                                "Video Title: {}\n"
                                "Time Segment: Segment {} ({:02d}:00 - {:02d}:00, 2-minute block)\n"
                                "Target Language: {}\n"
                                "Strict Word Count Target: Exactly ~{} words (STRICT REQUIREMENT).\n"
                                "Fact-Checking Requested: {}\n\n"
                                "Transcript Context (2-minute block plus 10s context padding before and after):\n{}\n\n"
                                "Write an engaging, structured Markdown commentary adhering strictly to ~{} words. Start directly without any introductory preamble ('Here is...') and end cleanly without any follow-up suggestions or questions.",
                                title, static_cast<int>(i), start_m, end_m, lang_name, words_target,
                                do_fact_check ? "YES - verify claims and provide a Fact-Check & Verification section" : "NO",
                                context, words_target
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
                        st->segment_commentaries[i].commentary_md = commentary;
                        st->segment_commentaries[i].is_generating = false;
                        st->segment_commentaries[i].generated = true;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(st->mutex);
                    if (!st->card_alive) return;
                    st->is_generating_all = false;
                    st->generating_segment_index = -1;
                    st->status_message = "All 2-minute AI commentaries generated successfully.";
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
                std::vector<segment_commentary> commentaries_copy;
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
                    commentaries_copy = state->segment_commentaries;
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

                // 2-minute segment calculations (120 seconds per segment)
                int current_segment = yt_active ? static_cast<int>(current_pos / 120.0) : 0;

                // Auto-sync position tracking
                if (auto_sync_enabled && yt_active && !commentaries_copy.empty()) {
                    if (current_segment != last_synced_segment) {
                        if (current_segment >= 0 && current_segment < static_cast<int>(commentaries_copy.size())) {
                            selected_segment = current_segment;
                        }
                        last_synced_segment = current_segment;
                    }
                }

                // Ensure selected segment is within valid bounds
                if (!commentaries_copy.empty()) {
                    selected_segment = std::clamp(selected_segment, 0, static_cast<int>(commentaries_copy.size()) - 1);
                }

                // Header Banner
                ui.text_colored(colors[0], std::format("{} Media Companion", ICON_MD_SUBTITLES));
                ui.separator();
                ui.spacing();

                // LLM Config, Language & Word Count Dropdowns
                auto& lcm = rouen::helpers::LLMConfigManager::instance();
                const auto& llm_configs = lcm.get_configs();
                std::string current_config_label = selected_llm_config;
                if (current_config_label.empty()) {
                    current_config_label = lcm.get_default_config_name();
                }

                ui.text_colored(ImVec4(0.8f, 0.8f, 0.95f, 1.0f), std::format("{} Model:", ICON_MD_SETTINGS));
                ui.same_line();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::BeginCombo("##media_companion_llm_config", current_config_label.c_str())) {
                    for (const auto& cfg : llm_configs) {
                        bool is_selected = (selected_llm_config == cfg.name);
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
                ImGui::SetNextItemWidth(125.0f);
                if (ImGui::BeginCombo("##media_companion_language", selected_language.c_str())) {
                    for (const auto& lang : supported_languages) {
                        bool is_selected = (selected_language == lang.name);
                        if (ImGui::Selectable(lang.name.c_str(), is_selected)) {
                            selected_language = lang.name;
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                ui.same_line();
                ui.text_colored(ImVec4(0.8f, 0.8f, 0.95f, 1.0f), std::format("{} Words:", ICON_MD_SHORT_TEXT));
                ui.same_line();
                ImGui::SetNextItemWidth(90.0f);
                std::string word_label = std::format("~{}", target_word_count);
                if (ImGui::BeginCombo("##media_companion_word_count", word_label.c_str())) {
                    for (int count : supported_word_counts) {
                        std::string item_label = std::format("~{} words", count);
                        bool is_selected = (target_word_count == count);
                        if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                            target_word_count = count;
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

                    ui.text_colored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                        std::format("{} Active YouTube Media [{:02d}:{:02d} / {:02d}:{:02d}] (2-min seg #{}):",
                            ICON_MD_PLAY_CIRCLE_FILLED, pos_m, pos_s, dur_m, dur_s, current_segment));
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
                    // View Mode Tabs: 0 = AI Commentary (2-min groups), 1 = Timestamped Subtitles, 2 = Plain Text
                    static int view_tab = 0;
                    if (ui.button(std::format("{} AI Commentary (2-min)", ICON_MD_AUTO_AWESOME))) {
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
                        // --- AI Commentary View Mode (2-Minute Groups) ---
                        ui.checkbox("Auto-Sync", &auto_sync_enabled);
                        ui.same_line();
                        ui.checkbox("Fact-Check", &enable_fact_check);

                        auto current_cfg_settings = rouen::helpers::LLMConfig::get_current_config(selected_llm_config);
                        bool adapter_allows_search = (current_cfg_settings.provider == rouen::helpers::LLMConfig::Provider::GROK ||
                                                      current_cfg_settings.provider == rouen::helpers::LLMConfig::Provider::GEMINI ||
                                                      current_cfg_settings.provider == rouen::helpers::LLMConfig::Provider::OPENAI);

                        if (adapter_allows_search) {
                            ui.same_line();
                            ui.checkbox("Web Search", &enable_web_search);
                        }

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

                        // Horizontal 2-Minute Segment Selection Pills Bar
                        ui.text_colored(colors[0], std::format("{} Select 2-Minute Segment:", ICON_MD_VIEW_TIMELINE));
                        if (ui.begin_child("SegmentSelectorBar", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar)) {
                            for (size_t s = 0; s < commentaries_copy.size(); ++s) {
                                if (s > 0) ui.same_line();

                                bool is_current_playing = (yt_active && static_cast<int>(s) == current_segment);
                                bool is_selected = (static_cast<int>(s) == selected_segment);

                                std::string label = std::format("{:02d}:00", s * 2);
                                if (commentaries_copy[s].generated) {
                                    label += std::format(" {}", ICON_MD_CHECK);
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
                                    selected_segment = static_cast<int>(s);
                                }

                                ImGui::PopStyleColor(3);
                            }
                        }
                        ui.end_child();

                        ui.spacing();
                        ui.separator();
                        ui.spacing();

                        // Selected Segment Banner & Action Bar
                        if (selected_segment >= 0 && selected_segment < static_cast<int>(commentaries_copy.size())) {
                            auto& sc = commentaries_copy[static_cast<size_t>(selected_segment)];

                            ui.text_colored(colors[0], std::format("{} 2-Min Segment {} ({})",
                                ICON_MD_AUTO_AWESOME, selected_segment, sc.timestamp_label));

                            ui.same_line();
                            if (ui.button(std::format("{} Jump to {:02d}:00", ICON_MD_PLAY_ARROW, selected_segment * 2))) {
                                seek_to(selected_segment * 120.0);
                            }

                            ui.same_line();
                            if (!sc.is_generating) {
                                if (ui.button(std::format("{} Regenerate", ICON_MD_REFRESH))) {
                                    generate_commentary_for_segment(selected_segment);
                                }
                            }

                            ui.spacing();

                            // Markdown Commentary Display Pane with High Contrast Charcoal Background
                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.11f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.96f, 1.0f));

                            float avail_h = ImGui::GetContentRegionAvail().y;
                            if (avail_h < 150.0f) avail_h = 240.0f;

                            if (ui.begin_child("CommentaryMarkdownRegion", ImVec2(0, avail_h), true)) {
                                if (sc.is_generating) {
                                    ui.text_colored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f),
                                        std::format("{} Generating AI commentary for Segment {} (in {}, ~{} words, fact-check: {})...",
                                            ICON_MD_HOURGLASS_EMPTY, selected_segment, selected_language, target_word_count, enable_fact_check ? "ON" : "OFF"));
                                } else if (sc.generated && !sc.commentary_md.empty()) {
                                    const rouen::helpers::markdown_render_config md_config{
                                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                                    };

                                    rouen::helpers::render_markdown_block(
                                        sc.commentary_md,
                                        md_config,
                                        [](const std::string& target_url) {
                                            rouen::platform::open_url(target_url);
                                        }
                                    );
                                } else {
                                    ui.text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                        std::format("No commentary generated for 2-minute Segment {} yet.", selected_segment));
                                    ui.spacing();
                                    if (ui.button(std::format("{} Generate AI Commentary for Segment {}", ICON_MD_AUTO_AWESOME, selected_segment))) {
                                        generate_commentary_for_segment(selected_segment);
                                    }
                                }
                            }
                            ui.end_child();
                            ImGui::PopStyleColor(2);
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

        static std::string wrap_text_soft_breaks(const std::string& input, size_t max_chars_per_line = 58) {
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

        void render_video_ui() override {
            std::vector<segment_commentary> commentaries_copy;

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                commentaries_copy = state->segment_commentaries;
            }

            if (commentaries_copy.empty()) return;

            // Query active player position in real time
            bool yt_active = false;
            double current_pos = 0.0;

            {
                std::lock_guard<std::recursive_mutex> lock(media_player::items_mutex());
                for (auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && (item_ptr->is_playing || item_ptr->ffmpeg_running.load() || item_ptr->player_pid > 0)) {
                        std::string u = item_ptr->url;
                        std::string l = item_ptr->item_link;
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

            int current_segment = static_cast<int>(current_pos / 120.0);
            if (current_segment < 0 || current_segment >= static_cast<int>(commentaries_copy.size())) return;

            // 2-minute block timing: 120 seconds total
            double segment_sec = fmod(current_pos, 120.0);
            if (segment_sec < 0.0) segment_sec += 120.0;

            // Display overlay in the last half of the target 2-minute time block (60.0s to 120.0s)
            // Block start of 2nd half (60.0s - 60.4s): vertically expands from 0 to 1 over 0.4s
            // Block pre-end (119.2s - 120.0s): vertically shrinks from 1 to 0 over 0.8s
            float anim_factor = 0.0f;
            if (segment_sec < 60.0) {
                anim_factor = 0.0f;
            } else if (segment_sec < 60.4) {
                anim_factor = static_cast<float>((segment_sec - 60.0) / 0.4);
            } else if (segment_sec >= 119.2) {
                anim_factor = static_cast<float>(std::max(0.0, (120.0 - segment_sec) / 0.8));
            } else {
                anim_factor = 1.0f;
            }

            if (anim_factor <= 0.005f) return; // Completely hidden when height is 0

            const auto& sc = commentaries_copy[static_cast<size_t>(current_segment)];
            if (!sc.generated || sc.commentary_md.empty()) return;

            constexpr float kFullHeight = 880.0f;
            float current_height = kFullHeight * anim_factor;

            // Translucent dark background with matching alpha animation (less transparent for enhanced readability)
            ImVec4 bg_color = ImVec4(0.04f, 0.05f, 0.09f, 0.90f * anim_factor);
            ImVec4 border_color = ImVec4(colors[0].x, colors[0].y, colors[0].z, 0.60f * anim_factor);

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

                    std::string wrapped_commentary = wrap_text_soft_breaks(sc.commentary_md, 58);

                    float child_h = std::max(1.0f, current_height - 56.0f);
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
                        float max_scroll_y = ImGui::GetScrollMaxY();
                        if (max_scroll_y > 0.0f) {
                            float scroll_progress = std::clamp(static_cast<float>((segment_sec - 61.0) / 57.0), 0.0f, 1.0f);
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

        void paint_video_surface(SDL_Surface* surface, int surface_w, int surface_h) override {
            if (!surface) return;
            (void)surface_w;
            (void)surface_h;
        }

        std::vector<mcp_function> get_mcp_functions() const override {
            return {
                mcp_function(
                    "get_media_companion_transcript",
                    "Request or retrieve the YouTube transcript and 2-minute segment AI commentaries for the currently playing media item.",
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
                        for (size_t i = 0; i < state->segment_commentaries.size(); ++i) {
                            if (i > 0) commentaries_json += ",";
                            const auto& sc = state->segment_commentaries[i];
                            commentaries_json += std::format(
                                R"({{"segment":{},"label":"{}","commentary":"{}"}})",
                                sc.segment_index, escape_json(sc.timestamp_label), escape_json(sc.commentary_md));
                        }
                        commentaries_json += "]";

                        return std::format(R"({{"status":"{}","title":"{}","url":"{}","selected_llm_config":"{}","selected_language":"{}","target_word_count":{},"enable_fact_check":{},"enable_web_search":{},"segment_commentaries":{},"timestamped_transcript":"{}","plain_transcript":"{}"}})",
                            escape_json(state->status_message),
                            escape_json(state->video_title),
                            escape_json(state->video_url),
                            escape_json(selected_llm_config),
                            escape_json(selected_language),
                            target_word_count,
                            enable_fact_check ? "true" : "false",
                            enable_web_search ? "true" : "false",
                            commentaries_json,
                            escape_json(state->timestamped_transcript),
                            escape_json(state->plain_transcript));
                    }
                )
            };
        }

    private:
        std::shared_ptr<shared_state> state;
        std::string selected_llm_config;
        std::string selected_language{"English"};
        int target_word_count{100};
        bool enable_fact_check{false};
        bool enable_web_search{true};
        int selected_segment{0};
        int last_synced_segment{-1};
        bool auto_sync_enabled{true};
    };

} // namespace rouen::cards
