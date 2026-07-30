#include "media_player_item.hpp"
#include "config_service.hpp"
#include "media_player.hpp"
#include "adlib_engine.hpp"
#include "mp4_writer.hpp"
#include "platform_utils.hpp"
#include "process_helper.hpp"
#include "registrar.hpp"
#include "texture_helper.hpp"
#include "texture_utils.hpp"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <imgui.h>
#include <iostream>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <cstring>
#include <cmath>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

double media_player_item::get_speaker_audio_pts() const {
    if (!is_playing || is_paused.load()) return position.load();
    if (!audio_clock_initialized.load()) {
        return first_audio_pts.load() >= 0.0 ? first_audio_pts.load() : 0.0;
    }
    double last_pts = last_audio_pts.load();
    if (last_pts < 0.0) {
        return first_audio_pts.load() >= 0.0 ? first_audio_pts.load() : 0.0;
    }
    double queue_lag = 0.0;
    if (local_audio_stream) {
        int q_bytes = SDL_GetAudioStreamQueued(local_audio_stream);
        int rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
        queue_lag = static_cast<double>(q_bytes) / static_cast<double>(static_cast<size_t>(rate) * 2 * sizeof(int16_t));
    }
    double current_spk_pts = last_pts - queue_lag;
    double f_a = first_audio_pts.load() >= 0.0 ? first_audio_pts.load() : 0.0;
    return std::max(f_a, current_spk_pts);
}

media_player_item::~media_player_item() {
    try {
        stopMedia();
    } catch (...) {}
}

bool media_player_item::checkMediaStatus() {
    return is_playing;
}

void media_player_item::update_watermark() {
    try {
        double cur_pos = get_current_position();
        if (cur_pos > 0.0) {
            double cur_dur = duration.load();
            if (cur_dur > 0.0 && cur_pos >= cur_dur - 2.0) {
                watermark = 0.0;
            } else {
                watermark = cur_pos;
            }
            if (feed_id != -1 && !item_link.empty() && save_watermark_cb) {
                save_watermark_cb(feed_id, item_link, item_title, watermark.value_or(0.0));
            }
        }
    } catch (...) {}
}

void media_player_item::stopMedia() {
    update_watermark();

    ffmpeg_running.store(false);
    is_playing = false;
    vu_level_l.store(0.0f);
    vu_level_r.store(0.0f);
    vu_watermark_l.store(0.0f);
    vu_watermark_r.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(audio_peak_mutex);
        audio_peak_queue.clear();
    }

    if (ffmpeg_thread.joinable()) {
        ffmpeg_thread.join();
    }

    if (local_audio_stream) {
        SDL_DestroyAudioStream(local_audio_stream);
        local_audio_stream = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(video_queue_mutex);
        decoded_video_queue.clear();
    }

    if (reset_sync_cb) {
        reset_sync_cb();
    }

    {
        std::lock_guard<std::mutex> lock(texture_mutex);
        SDL_GPUDevice* device = nullptr;
        try {
            auto r_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (r_ptr && *r_ptr) device = *r_ptr;
        } catch (...) {}
        if (video_texture) {
            TextureHelper::destroyTexture(video_texture);
            video_texture = nullptr;
        }
        if (rss_image_texture) {
            TextureHelper::destroyTexture(rss_image_texture);
            rss_image_texture = nullptr;
        }
        if (device && upload_buffer) {
            SDL_ReleaseGPUTransferBuffer(device, upload_buffer);
            upload_buffer = nullptr;
        }
    }

    is_playing = false;
    is_paused = false;
    has_video.store(false);
    has_audio.store(false);
    has_presented_first_frame.store(false);
    baseline_set.store(false);
    baseline_start_pts.store(-1.0);
    position = 0.0;
    duration = 0.0;
    start_offset = 0.0;
    player_pid = 0;
    owner_card = nullptr;
    last_video_present_time = {};
    current_frame_duration = 1.0 / 30.0;
    last_presented_pts = -1.0;
    last_av_sync_delta_ms.store(0.0);
    frames_presented.store(0);
    frames_dropped.store(0);
    frames_held.store(0);
    audio_clock_initialized.store(false);
    rss_image_url.clear();
    rss_image_width = 0;
    rss_image_height = 0;
}

std::string media_player_item::urlDecode(const std::string& encoded) {
    std::string decoded;
    char ch;
    unsigned int j;
    for (size_t i = 0; i < encoded.length(); i++) {
        if (encoded[i] == '%') {
            sscanf(encoded.substr(i + 1, 2).c_str(), "%x", &j);
            ch = static_cast<char>(j);
            decoded += ch;
            i = i + 2;
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

bool media_player_item::isUrlEncoded(const std::string& input_str) {
    return input_str.find('%') != std::string::npos;
}

std::string media_player_item::sanitizeURL(const std::string& input_url) {
    std::string sanitized_url = input_url;
    if (isUrlEncoded(input_url)) {
        sanitized_url = urlDecode(input_url);
    }
    return sanitized_url;
}

bool media_player_item::playMedia(const void* owner) {
    if (owner) {
        owner_card = owner;
    }
    double offset = start_offset;
    stopMedia();
    start_offset.store(offset);
    playback_start_time = std::chrono::steady_clock::now();
    has_video.store(false);
    is_paused = false;

    if (media_player::is_detached_mode_active()) {
        std::shared_ptr<media_player_item> self_ptr = nullptr;
        try {
            self_ptr = shared_from_this();
        } catch (...) {
            if (!url.empty()) {
                self_ptr = media_player::get_item_ptr(url);
            }
        }
        if (self_ptr) {
            media_player::set_detached_item(self_ptr);
            if (media_player::get_active_fullscreen_item() == self_ptr) {
                media_player::clear_active_fullscreen_item();
            }
        }
    }

    std::string sanitized_url = sanitizeURL(url);

    ffmpeg_running.store(true);
    is_playing = true;
    position.store(offset);
    last_audio_pts.store(offset);
    first_audio_pts.store(-1.0);
    audio_clock_initialized.store(false);
    player_pid = 1;
    if (reset_sync_cb) {
        reset_sync_cb();
    }

    ffmpeg_thread = std::thread([this, sanitized_url, offset]() {
        std::string video_target = sanitized_url;
        std::string audio_target;

        if (sanitized_url.find("youtube.com") != std::string::npos ||
            sanitized_url.find("youtu.be") != std::string::npos) {
            
            // Check if a channel or user URL was passed instead of a video watch URL
            if (sanitized_url.find("/channel/") != std::string::npos ||
                sanitized_url.find("/@") != std::string::npos ||
                sanitized_url.find("/user/") != std::string::npos ||
                sanitized_url.find("/c/") != std::string::npos) {
                std::cerr << "[NativePlayer] Channel URL passed to media player. Cannot play channel URL directly." << std::endl;
                ffmpeg_running.store(false);
                is_playing = false;
                player_pid = 0;
                return;
            }

            static std::mutex s_yt_resolved_url_mutex;
            static std::unordered_map<std::string, std::pair<std::chrono::steady_clock::time_point, std::vector<std::string>>> s_yt_resolved_urls;

            auto assign_targets_from_urls = [&](const std::vector<std::string>& urls_vec) {
                std::string v_url;
                std::string a_url;
                for (const auto& u : urls_vec) {
                    if (u.find("mime=video") != std::string::npos || u.find("mime%3Dvideo") != std::string::npos) {
                        if (v_url.empty()) v_url = u;
                    } else if (u.find("mime=audio") != std::string::npos || u.find("mime%3Daudio") != std::string::npos) {
                        if (a_url.empty()) a_url = u;
                    }
                }

                if (!v_url.empty() && !a_url.empty()) {
                    video_target = v_url;
                    audio_target = a_url;
                } else if (urls_vec.size() >= 2) {
                    video_target = urls_vec[0];
                    audio_target = urls_vec[1];
                } else if (!v_url.empty()) {
                    video_target = v_url;
                    audio_target = v_url;
                } else if (!urls_vec.empty()) {
                    video_target = urls_vec[0];
                    audio_target = urls_vec[0];
                }
            };

            bool found_cached = false;
            {
                std::lock_guard<std::mutex> lock(s_yt_resolved_url_mutex);
                auto it = s_yt_resolved_urls.find(sanitized_url);
                if (it != s_yt_resolved_urls.end()) {
                    auto now = std::chrono::steady_clock::now();
                    auto age_mins = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.first).count();
                    if (age_mins < 120 && !it->second.second.empty()) {
                        assign_targets_from_urls(it->second.second);
                        found_cached = true;
                    }
                }
            }

            if (!found_cached) {
                std::string ytdl_exe = rouen::platform::find_executable("yt-dlp");
                auto config = rouen::helpers::ConfigService::instance();
                std::string initial_cookie_args = config ? config->get_ytdlp_cookie_args() : "";

                std::string pref_quality = config ? config->get_env("ROUEN_YOUTUBE_PREFERRED_QUALITY") : "360p";
                std::string format_spec;
                if (pref_quality == "1080p") {
                    format_spec = "bestvideo[height<=1080][vcodec^=avc1]+bestaudio/bestvideo[height<=1080]+bestaudio/best[height<=1080]/best[protocol*=m3u8]/bestvideo[height<=720]+bestaudio/best[height<=720]/best";
                } else if (pref_quality == "4k" || pref_quality == "2160p") {
                    format_spec = "bestvideo[height<=2160][vcodec^=avc1]+bestaudio/bestvideo[height<=2160]+bestaudio/best[height<=2160]/best[protocol*=m3u8]/bestvideo+bestaudio/best";
                } else {
                    format_spec = "bestvideo[height<=360][vcodec^=avc1]+bestaudio/bestvideo[height<=360]+bestaudio/best[height<=360]/best[protocol*=m3u8]/bestvideo[height<=480]+bestaudio/best[height<=480]/best";
                }

                auto run_ytdlp = [&](const std::string& cookie_args) -> std::pair<std::vector<std::string>, std::string> {
                    std::string cmd;
                    std::string remote_flag = ProcessHelper::ytdlp_supports_remote_components(ytdl_exe) ? "--remote-components ejs:github " : "";
                    if (!cookie_args.empty()) {
                        cmd = std::format("\"{}\" --no-warnings {}--socket-timeout 10 {} -g -f \"{}\" \"{}\" 2>&1", ytdl_exe, remote_flag, cookie_args, format_spec, sanitized_url);
                    } else {
                        cmd = std::format("\"{}\" --no-warnings {}--socket-timeout 10 -g -f \"{}\" \"{}\" 2>&1", ytdl_exe, remote_flag, format_spec, sanitized_url);
                    }
                    std::string output = ProcessHelper::executeCommand(cmd);
                    std::stringstream ss(output);
                    std::string line;
                    std::vector<std::string> parsed_urls;
                    while (std::getline(ss, line)) {
                        line.erase(line.find_last_not_of(" \r\n\t") + 1);
                        if (line.starts_with("http://") || line.starts_with("https://")) {
                            parsed_urls.push_back(line);
                        }
                    }
                    return {parsed_urls, output};
                };

                auto [urls, resolved] = run_ytdlp(initial_cookie_args);

                // If yt-dlp failed to resolve YouTube URL, attempt browser cookies auto-detection fallback
                if (urls.empty()) {
                    static const std::vector<std::string> candidate_browsers = {"safari", "chrome", "firefox", "brave", "edge", "arc", "vivaldi", "opera"};
                    std::string configured_browser = config ? config->get_env("ROUEN_COOKIES_BROWSER") : "";

                    for (const auto& browser : candidate_browsers) {
                        if (browser == configured_browser) continue;
                        std::string fallback_args = std::format("--cookies-from-browser {}", browser);
                        auto [fb_urls, fb_output] = run_ytdlp(fallback_args);
                        if (!fb_urls.empty()) {
                            urls = fb_urls;
                            resolved = fb_output;
                            if (config) {
                                config->set_env_value("ROUEN_COOKIES_BROWSER", browser, true);
                            }
                            std::cout << "[NativePlayer] Successfully resolved YouTube URL using cookies from browser: " << browser << std::endl;
                            break;
                        }
                    }
                }

                if (!urls.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(s_yt_resolved_url_mutex);
                        s_yt_resolved_urls[sanitized_url] = {std::chrono::steady_clock::now(), urls};
                    }
                    assign_targets_from_urls(urls);
                } else {
                    std::string clean_resolved = resolved;
                    clean_resolved.erase(clean_resolved.find_last_not_of(" \r\n\t") + 1);

                    std::string err_msg;
                    if (clean_resolved.find("Sign in to confirm you") != std::string::npos || clean_resolved.find("not a bot") != std::string::npos) {
                        err_msg = "YouTube Authentication Required:\nYouTube requires cookies for this video.\nSave a cookies.txt file to ~/.config/rouen/cookies.txt or ~/Downloads/cookies.txt, or set ROUEN_COOKIES_FILE in Settings.";
                    } else if (clean_resolved.find("live event has ended") != std::string::npos || clean_resolved.find("This live event has ended") != std::string::npos) {
                        err_msg = "YouTube Live Event Processing:\nThis live stream has ended and YouTube is currently processing the recording into a video. Please try again in a few minutes once YouTube finishes processing.";
                    } else {
                        if (clean_resolved.empty()) {
                            clean_resolved = "No output from yt-dlp";
                        }
                        if (clean_resolved.length() > 200) {
                            clean_resolved = clean_resolved.substr(0, 197) + "...";
                        }
                        err_msg = std::format("Media Player Error: yt-dlp failed to resolve YouTube URL.\nOutput: {}", clean_resolved);
                    }

                    std::cerr << "[NativePlayer] " << err_msg << std::endl;
                    try {
                        "notify"_sfn(err_msg);
                    } catch (...) {}
                    ffmpeg_running.store(false);
                    is_playing = false;
                    player_pid = 0;
                    return;
                }
            }
        }

        decode_loop(video_target, audio_target, offset);
    });

    return true;
}

std::string media_player_item::formatTime(double seconds) const {
    if (seconds < 0.0) seconds = 0.0;
    auto hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    auto secs = static_cast<int>(seconds) % 60;
    if (hours > 0) {
        return std::format("{:02d}:{:02d}:{:02d}", hours, minutes, secs);
    } else {
        return std::format("{:02d}:{:02d}", minutes, secs);
    }
}

bool media_player_item::seekTo(double position_seconds) {
    std::lock_guard<std::mutex> s_lock(seek_mutex);
    start_offset.store(position_seconds);
    playback_start_time = std::chrono::steady_clock::now();
    position.store(position_seconds);
    has_presented_first_frame.store(false);
    {
        std::lock_guard<std::mutex> q_lock(video_queue_mutex);
        decoded_video_queue.clear();
    }
    first_audio_pts.store(-1.0);
    first_video_pts.store(-1.0);
    audio_clock_initialized.store(false);
    baseline_set.store(false);
    baseline_start_pts.store(-1.0);
    last_video_present_time = {};
    current_frame_duration = 1.0 / 30.0;
    last_presented_pts = -1.0;
    last_av_sync_delta_ms.store(0.0);
    frames_presented.store(0);
    frames_dropped.store(0);
    frames_held.store(0);
    audio_clock_initialized.store(false);
    if (local_audio_stream) {
        SDL_ClearAudioStream(local_audio_stream);
        SDL_PauseAudioStreamDevice(local_audio_stream);
    }
    if (!ffmpeg_running.load()) {
        return playMedia();
    }
    seek_target.store(position_seconds);
    return true;
}

bool media_player_item::setVolume(int new_volume) {
    volume = std::clamp(new_volume, 0, 100);
    return true;
}

double media_player_item::get_current_position() const {
    if (is_adlib_item.load() && rouen::helpers::AdLibEngine::instance().is_recording()) {
        return start_offset.load() + rouen::helpers::AdLibEngine::instance().get_stage_written_seconds();
    }

    if (!is_playing) return position.load();
    if (is_paused.load()) return position.load();
    if (!has_presented_first_frame.load()) return std::max(start_offset.load(), position.load());

    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - playback_start_time).count();
    double live_pos = start_offset.load() + elapsed;

    if (!is_adlib_item.load()) {
        bool audio_active = (local_audio_stream && !SDL_AudioStreamDevicePaused(local_audio_stream));
        if (audio_active && first_audio_pts.load() >= 0.0 && audio_clock_initialized.load()) {
            double speaker_pts = get_speaker_audio_pts();
            double audio_pos = start_offset.load() + (speaker_pts - first_audio_pts.load());
            if (audio_pos > live_pos - 2.0 && audio_pos < live_pos + 2.0 && audio_pos >= start_offset.load()) {
                live_pos = audio_pos;
            }
        }
    }

    double max_dur = duration.load();
    if (max_dur > 0.0 && live_pos > max_dur) {
        live_pos = max_dur;
    }
    double prev_pos = position.load();
    if (live_pos > prev_pos) {
        position.store(live_pos);
    } else {
        live_pos = prev_pos;
    }
    return live_pos;
}

bool media_player_item::setPaused(bool paused) {
    if (paused && !is_paused.load()) {
        position.store(get_current_position());
        update_watermark();
    } else if (!paused && is_paused.load()) {
        double cur_pos = std::max(start_offset.load(), position.load());
        start_offset.store(cur_pos);
        playback_start_time = std::chrono::steady_clock::now();
    }
    is_paused = paused;
    if (local_audio_stream) {
        if (paused) {
            SDL_PauseAudioStreamDevice(local_audio_stream);
        } else {
            SDL_ResumeAudioStreamDevice(local_audio_stream);
        }
    }
    if (reset_sync_cb) {
        reset_sync_cb();
    }
    return true;
}

bool media_player_item::pauseMedia() {
    return setPaused(true);
}

bool media_player_item::resumeMedia() {
    return setPaused(false);
}

bool media_player_item::togglePause() {
    return is_paused.load() ? resumeMedia() : pauseMedia();
}

void media_player_item::update_vu_levels() {
    double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    float target_l = 0.0f;
    float target_r = 0.0f;

    {
        std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
        // Remove peaks that have already been played (older than 0.05s ago)
        while (!audio_peak_queue.empty() && audio_peak_queue.front().wall_time < now - 0.05) {
            audio_peak_queue.pop_front();
        }

        // Match peaks within a window around 'now' (already heard or about to be heard)
        for (const auto& sample : audio_peak_queue) {
            if (sample.wall_time <= now + 0.05) {  // include slightly future peaks
                if (sample.peak_l > target_l) target_l = sample.peak_l;
                if (sample.peak_r > target_r) target_r = sample.peak_r;
            }
        }
    }

    float curr_l = vu_level_l.load();
    float curr_r = vu_level_r.load();
    float new_l = std::max(curr_l * 0.85f, target_l);
    float new_r = std::max(curr_r * 0.85f, target_r);

    vu_level_l.store(new_l);
    vu_level_r.store(new_r);

    float wm_l = vu_watermark_l.load();
    float wm_r = vu_watermark_r.load();
    vu_watermark_l.store(std::max(wm_l * 0.96f, new_l));
    vu_watermark_r.store(std::max(wm_r * 0.96f, new_r));
}

float media_player_item::get_vu_level_l() {
    update_vu_levels();
    return vu_level_l.load();
}

float media_player_item::get_vu_level_r() {
    update_vu_levels();
    return vu_level_r.load();
}

float media_player_item::get_vu_watermark_l() {
    return vu_watermark_l.load();
}

float media_player_item::get_vu_watermark_r() {
    return vu_watermark_r.load();
}

int media_player_item::decode_interrupt_cb(void* ctx) {
    auto* player = static_cast<media_player_item*>(ctx);
    if (player && !player->ffmpeg_running.load()) {
        return 1;
    }
    return 0;
}

namespace {
std::string get_ffmpeg_error_string(int errnum) {
    char errbuf[256];
    if (av_strerror(errnum, errbuf, sizeof(errbuf)) == 0) {
        return std::string(errbuf);
    }
    return "Unknown FFmpeg error " + std::to_string(errnum);
}
} // namespace

void media_player_item::decode_loop(std::string video_target, std::string audio_target, double offset) {
    avformat_network_init();

    AVFormatContext* video_format_ctx = avformat_alloc_context();
    if (!video_format_ctx) {
        try {
            "notify"_sfn("Media Player Error: Failed to allocate video format context");
        } catch (...) {}
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }
    video_format_ctx->interrupt_callback.callback = decode_interrupt_cb;
    video_format_ctx->interrupt_callback.opaque = this;

    AVDictionary* v_opts = nullptr;
    av_dict_set(&v_opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto,data", 0);
    if (video_target.find("http://") == 0 || video_target.find("https://") == 0) {
        av_dict_set(&v_opts, "reconnect", "1", 0);
        av_dict_set(&v_opts, "reconnect_streamed", "1", 0);
        av_dict_set(&v_opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&v_opts, "rw_timeout", "10000000", 0);
        av_dict_set(&v_opts, "buffer_size", "1048576", 0);
        av_dict_set(&v_opts, "fifo_size", "1048576", 0);
        av_dict_set(&v_opts, "user_agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36", 0);
    }

    const AVInputFormat* v_fmt = nullptr;
    if (video_target.find("/manifest/dash/") != std::string::npos || video_target.find(".mpd") != std::string::npos) {
        v_fmt = av_find_input_format("dash");
    } else if (video_target.find("/manifest/hls") != std::string::npos || video_target.find(".m3u8") != std::string::npos) {
        v_fmt = av_find_input_format("hls");
    }

    int err = avformat_open_input(&video_format_ctx, video_target.c_str(), v_fmt, &v_opts);
    if (err < 0) {
        if (v_opts) av_dict_free(&v_opts);
        std::string err_msg = get_ffmpeg_error_string(err);
        std::string final_err;
        if (video_target.find("force_finished") != std::string::npos ||
            video_target.find("yt_live_broadcast") != std::string::npos ||
            video_target.find("/manifest/dash/") != std::string::npos) {
            final_err = "YouTube Live Event Processing:\nThis live stream has ended and YouTube is currently processing the recording into a video. Please try again in a few minutes once YouTube finishes processing.";
        } else {
            final_err = std::format("Media Player Error: Failed to open input ({}): {}", video_target, err_msg);
        }
        std::cerr << "[NativePlayer] " << final_err << std::endl;
        try {
            "notify"_sfn(final_err);
        } catch (...) {}
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }
    if (v_opts) av_dict_free(&v_opts);

    int find_info_err = avformat_find_stream_info(video_format_ctx, nullptr);
    if (find_info_err < 0) {
        std::string err_msg = get_ffmpeg_error_string(find_info_err);
        std::string final_err = std::format("Media Player Error: Failed to find stream info: {}", err_msg);
        std::cerr << "[NativePlayer] Failed to find stream info: " << err_msg << std::endl;
        try {
            "notify"_sfn(final_err);
        } catch (...) {}
        avformat_close_input(&video_format_ctx);
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }

    AVFormatContext* audio_format_ctx = nullptr;
    if (!audio_target.empty() && audio_target != video_target) {
        audio_format_ctx = avformat_alloc_context();
        if (audio_format_ctx) {
            audio_format_ctx->interrupt_callback.callback = decode_interrupt_cb;
            audio_format_ctx->interrupt_callback.opaque = this;
            AVDictionary* a_opts = nullptr;
            av_dict_set(&a_opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto,data", 0);
            if (audio_target.find("http://") == 0 || audio_target.find("https://") == 0) {
                av_dict_set(&a_opts, "reconnect", "1", 0);
                av_dict_set(&a_opts, "reconnect_streamed", "1", 0);
                av_dict_set(&a_opts, "reconnect_delay_max", "5", 0);
                av_dict_set(&a_opts, "rw_timeout", "10000000", 0);
                av_dict_set(&a_opts, "buffer_size", "1048576", 0);
                av_dict_set(&a_opts, "fifo_size", "1048576", 0);
                av_dict_set(&a_opts, "user_agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36", 0);
            }
            const AVInputFormat* a_fmt = nullptr;
            if (audio_target.find("/manifest/dash/") != std::string::npos || audio_target.find(".mpd") != std::string::npos) {
                a_fmt = av_find_input_format("dash");
            } else if (audio_target.find("/manifest/hls") != std::string::npos || audio_target.find(".m3u8") != std::string::npos) {
                a_fmt = av_find_input_format("hls");
            }
            if (avformat_open_input(&audio_format_ctx, audio_target.c_str(), a_fmt, &a_opts) < 0) {
                if (a_opts) av_dict_free(&a_opts);
                avformat_free_context(audio_format_ctx);
                audio_format_ctx = nullptr;
            } else {
                if (a_opts) av_dict_free(&a_opts);
                if (avformat_find_stream_info(audio_format_ctx, nullptr) < 0) {
                    avformat_close_input(&audio_format_ctx);
                    audio_format_ctx = nullptr;
                }
            }
        }
    }
    if (!audio_format_ctx) {
        audio_format_ctx = video_format_ctx;
    }

    int video_stream_idx = -1;
    for (unsigned int i = 0; i < video_format_ctx->nb_streams; i++) {
        if (video_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx == -1) {
            video_stream_idx = static_cast<int>(i);
        }
    }

    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < audio_format_ctx->nb_streams; i++) {
        if (audio_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx == -1) {
            audio_stream_idx = static_cast<int>(i);
        }
    }

    AVCodecContext* video_codec_ctx = nullptr;
    AVCodecContext* audio_codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;

    int dst_w = kWidth;
    int dst_h = kHeight;
    int offset_x = 0;
    int offset_y = 0;
    int last_src_w = 0;
    int last_src_h = 0;

    if (video_stream_idx >= 0) {
        const AVCodec* video_codec = avcodec_find_decoder(video_format_ctx->streams[video_stream_idx]->codecpar->codec_id);
        if (video_codec) {
            video_codec_ctx = avcodec_alloc_context3(video_codec);
            avcodec_parameters_to_context(video_codec_ctx, video_format_ctx->streams[video_stream_idx]->codecpar);
            video_codec_ctx->thread_count = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
            video_codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
            if (avcodec_open2(video_codec_ctx, video_codec, nullptr) >= 0) {
                if (video_codec_ctx->width > 0 && video_codec_ctx->height > 0) {
                    last_src_w = video_codec_ctx->width;
                    last_src_h = video_codec_ctx->height;
                    double aspect_ratio = static_cast<double>(last_src_w) / static_cast<double>(last_src_h);
                    if (aspect_ratio > 0.0) {
                        video_aspect_ratio.store(static_cast<float>(aspect_ratio));
                    }
                    if (aspect_ratio > static_cast<double>(kWidth) / static_cast<double>(kHeight)) {
                        dst_w = kWidth;
                        dst_h = static_cast<int>(static_cast<double>(kWidth) / aspect_ratio);
                    } else {
                        dst_h = kHeight;
                        dst_w = static_cast<int>(static_cast<double>(kHeight) * aspect_ratio);
                    }
                    dst_w = (dst_w / 2) * 2;
                    dst_h = (dst_h / 2) * 2;
                    offset_x = (kWidth - dst_w) / 2;
                    offset_y = (kHeight - dst_h) / 2;

                    sws_ctx = sws_getContext(
                        last_src_w, last_src_h, video_codec_ctx->pix_fmt,
                        dst_w, dst_h, AV_PIX_FMT_RGBA,
                        SWS_BILINEAR, nullptr, nullptr, nullptr
                    );
                }
            }
        }
    }

    if (audio_stream_idx >= 0) {
        has_audio.store(true);
        const AVCodec* audio_codec = avcodec_find_decoder(audio_format_ctx->streams[audio_stream_idx]->codecpar->codec_id);
        if (audio_codec) {
            audio_codec_ctx = avcodec_alloc_context3(audio_codec);
            avcodec_parameters_to_context(audio_codec_ctx, audio_format_ctx->streams[audio_stream_idx]->codecpar);
            if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) >= 0) {
                AVChannelLayout out_layout;
                av_channel_layout_default(&out_layout, 2); // Stereo

                swr_ctx = swr_alloc();
                av_opt_set_chlayout(swr_ctx, "in_chlayout", &audio_codec_ctx->ch_layout, 0);
                av_opt_set_int(swr_ctx, "in_sample_rate", audio_codec_ctx->sample_rate, 0);
                av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", audio_codec_ctx->sample_fmt, 0);
                
                int target_rate = is_adlib_item.load() ? 44100 : (audio_codec_ctx->sample_rate > 0 ? audio_codec_ctx->sample_rate : 44100);
                audio_sample_rate.store(target_rate);
                av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_layout, 0);
                av_opt_set_int(swr_ctx, "out_sample_rate", target_rate, 0);
                av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                
                swr_init(swr_ctx);
                av_channel_layout_uninit(&out_layout);
            }
        }
    }

    double total_duration = 0.0;
    if (video_format_ctx && video_format_ctx->duration != AV_NOPTS_VALUE) {
        total_duration = static_cast<double>(video_format_ctx->duration) / AV_TIME_BASE;
    } else if (audio_format_ctx && audio_format_ctx->duration != AV_NOPTS_VALUE) {
        total_duration = static_cast<double>(audio_format_ctx->duration) / AV_TIME_BASE;
    }
    if (total_duration <= 0.0 && video_format_ctx && audio_stream_idx >= 0 && audio_stream_idx < static_cast<int>(video_format_ctx->nb_streams)) {
        auto* st = video_format_ctx->streams[audio_stream_idx];
        if (st && st->duration != AV_NOPTS_VALUE) {
            total_duration = static_cast<double>(st->duration) * av_q2d(st->time_base);
        }
    }
    if (total_duration <= 0.0 && audio_format_ctx && audio_stream_idx >= 0 && audio_stream_idx < static_cast<int>(audio_format_ctx->nb_streams)) {
        auto* st = audio_format_ctx->streams[audio_stream_idx];
        if (st && st->duration != AV_NOPTS_VALUE) {
            total_duration = static_cast<double>(st->duration) * av_q2d(st->time_base);
        }
    }
    if (total_duration > 0.0) {
        duration.store(total_duration);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgba_frame = av_frame_alloc();

    int size = kWidth * kHeight * 4;
    uint8_t* rgba_buffer = static_cast<uint8_t*>(av_malloc(static_cast<size_t>(size)));
    std::memset(rgba_buffer, 0, static_cast<size_t>(size));

    rgba_frame->linesize[0] = kWidth * 4;

    if (offset > 0.05) {
        int seek_stream = video_stream_idx >= 0 ? video_stream_idx : audio_stream_idx;
        if (seek_stream >= 0) {
            int64_t target_pts = av_rescale_q(static_cast<int64_t>(offset * AV_TIME_BASE), AV_TIME_BASE_Q, video_format_ctx->streams[seek_stream]->time_base);
            av_seek_frame(video_format_ctx, seek_stream, target_pts, AVSEEK_FLAG_BACKWARD);
            if (video_codec_ctx) avcodec_flush_buffers(video_codec_ctx);
            if (audio_codec_ctx && audio_format_ctx == video_format_ctx) avcodec_flush_buffers(audio_codec_ctx);
        }
        if (audio_format_ctx != video_format_ctx && audio_stream_idx >= 0) {
            int64_t target_pts = av_rescale_q(static_cast<int64_t>(offset * AV_TIME_BASE), AV_TIME_BASE_Q, audio_format_ctx->streams[audio_stream_idx]->time_base);
            av_seek_frame(audio_format_ctx, audio_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);
            if (audio_codec_ctx) avcodec_flush_buffers(audio_codec_ctx);
        }
    }

    auto start_time = std::chrono::steady_clock::now();
    (void)start_time;
    position.store(offset);

    uint8_t* audio_out_buf = nullptr;
    int max_audio_out_samples = 4096;
    av_samples_alloc(&audio_out_buf, nullptr, 2, max_audio_out_samples, AV_SAMPLE_FMT_S16, 0);

    ffmpeg_running.store(true);
    is_playing = true;
    player_pid = 1;

    bool is_dual_input = (audio_format_ctx && audio_format_ctx != video_format_ctx);
    bool video_eof = (video_stream_idx < 0);
    std::atomic<bool> audio_eof(audio_stream_idx < 0);
    std::thread audio_thread;

    auto drain_and_finish = [&]() {
        if (video_codec_ctx && sws_ctx && video_stream_idx >= 0) {
            avcodec_send_packet(video_codec_ctx, nullptr);
            while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                double pts_time = 0.0;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                } else {
                    pts_time = position.load() + (1.0 / kFps);
                }
                std::memset(rgba_buffer, 0, static_cast<size_t>(size));
                rgba_frame->data[0] = rgba_buffer + (offset_y * kWidth * 4) + (offset_x * 4);
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, video_codec_ctx->height, rgba_frame->data, rgba_frame->linesize);
                {
                    std::lock_guard<std::mutex> lock(video_queue_mutex);
                    decoded_video_queue.push_back({
                        std::vector<uint8_t>(rgba_buffer, rgba_buffer + size),
                        pts_time
                    });
                }
            }
        }
        if (!is_dual_input && audio_codec_ctx && swr_ctx && audio_stream_idx >= 0) {
            avcodec_send_packet(audio_codec_ctx, nullptr);
            while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                double pts_time = 0.0;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[audio_stream_idx]->time_base);
                } else {
                    int target_rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
                    pts_time = position.load() + (static_cast<double>(frame->nb_samples) / static_cast<double>(target_rate));
                }
                last_audio_pts.store(pts_time);
                double cur_dur = duration.load();
                if (pts_time > cur_dur) {
                    duration.store(pts_time);
                }

                const uint8_t* input_data[8];
                for (int i = 0; i < 8; ++i) input_data[i] = frame->data[i];
                int out_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, input_data, frame->nb_samples);
                if (out_samples > 0) {
                    size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                    std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                    int target_rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
                    if (!local_audio_stream) {
                        SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, target_rate};
                        local_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
                        if (local_audio_stream) SDL_ResumeAudioStreamDevice(local_audio_stream);
                    }
                    if (local_audio_stream) {
                        float vol_factor = static_cast<float>(volume.load()) / 100.0f;
                        if (vol_factor < 1.0f) {
                            int16_t* samples = reinterpret_cast<int16_t*>(pcm_chunk.data());
                            size_t sample_count = pcm_chunk.size() / sizeof(int16_t);
                            for (size_t i = 0; i < sample_count; ++i) {
                                samples[i] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(samples[i] * vol_factor), -32768, 32767));
                            }
                        }
                        SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                        if (SDL_AudioStreamDevicePaused(local_audio_stream) && !is_paused.load()) {
                            SDL_ResumeAudioStreamDevice(local_audio_stream);
                        }
                        if (on_audio_pcm_cb) {
                            on_audio_pcm_cb(pcm_chunk.data(), pcm_chunk.size());
                        }
                        int q_bytes = SDL_GetAudioStreamQueued(local_audio_stream);
                        double q_sec = static_cast<double>(q_bytes) / static_cast<double>(target_rate * 4);
                        double cur_pos = std::max(0.0, pts_time - q_sec);
                        position.store(cur_pos);
                    }
                    if (!pcm_chunk.empty()) {
                        const int16_t* samples = reinterpret_cast<const int16_t*>(pcm_chunk.data());
                        size_t f_count = pcm_chunk.size() / (2 * sizeof(int16_t));
                        int32_t max_l = 0, max_r = 0;
                        for (size_t i = 0; i < f_count; ++i) {
                            int32_t l = std::abs(static_cast<int32_t>(samples[i * 2]));
                            int32_t r = std::abs(static_cast<int32_t>(samples[i * 2 + 1]));
                            if (l > max_l) max_l = l;
                            if (r > max_r) max_r = r;
                        }
                        float peak_l = static_cast<float>(max_l) / 32768.0f;
                        float peak_r = static_cast<float>(max_r) / 32768.0f;
                        {
                            std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
                            // Compute wall-clock time when this audio will reach the speaker
                            double buf_lag = local_audio_stream ? static_cast<double>(SDL_GetAudioStreamQueued(local_audio_stream)) / static_cast<double>(target_rate * 4) : 0.0;
                            double arrival = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() + buf_lag;
                            audio_peak_queue.push_back({arrival, peak_l, peak_r});
                            if (audio_peak_queue.size() > 300) {
                                audio_peak_queue.pop_front();
                            }
                        }
                    }
                }
            }
            int flush_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, nullptr, 0);
            if (flush_samples > 0) {
                size_t pcm_bytes = static_cast<size_t>(flush_samples) * 2 * sizeof(int16_t);
                std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                if (local_audio_stream) {
                    SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                    if (SDL_AudioStreamDevicePaused(local_audio_stream) && !is_paused.load()) {
                        SDL_ResumeAudioStreamDevice(local_audio_stream);
                    }
                }
            }
        };

        while (ffmpeg_running.load() && !is_paused.load()) {
            int queued_audio = local_audio_stream ? SDL_GetAudioStreamQueued(local_audio_stream) : 0;
            if (queued_audio <= 8820) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::lock_guard<std::mutex> lock(video_queue_mutex);
            decoded_video_queue.clear();
        }

        if (duration.load() > 0.0) {
            position.store(duration.load());
        }
        update_watermark();
        is_playing = false;
        is_paused = false;
        player_pid = 0;
    };

    if (is_dual_input) {
        audio_thread = std::thread([this, audio_codec_ctx, swr_ctx, audio_stream_idx, audio_format_ctx, &audio_eof]() {
            AVPacket* a_pkt = av_packet_alloc();
            AVFrame* a_frm = av_frame_alloc();
            uint8_t* a_out_buf = nullptr;
            int a_out_max_samples = 0;
            while (ffmpeg_running.load() && !audio_eof) {
                if (is_paused.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                int target_rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
                int queued_audio = local_audio_stream ? SDL_GetAudioStreamQueued(local_audio_stream) : 0;
                if (queued_audio >= (target_rate * 4) / 4) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                {
                    std::lock_guard<std::mutex> s_lock(seek_mutex);
                    if (seek_target.load() >= 0.0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }

                    int read_res = av_read_frame(audio_format_ctx, a_pkt);
                    if (read_res < 0) {
                        audio_eof = true;
                    } else if (a_pkt->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
                        if (avcodec_send_packet(audio_codec_ctx, a_pkt) >= 0) {
                            while (avcodec_receive_frame(audio_codec_ctx, a_frm) >= 0) {
                                double pts_time = 0.0;
                                if (a_frm->pts != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(a_frm->pts) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                                } else if (a_frm->best_effort_timestamp != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(a_frm->best_effort_timestamp) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                                } else {
                                    pts_time = std::max(0.0, position.load()) + (static_cast<double>(a_frm->nb_samples) / static_cast<double>(target_rate));
                                }
                                if (first_audio_pts.load() < 0.0 && pts_time >= 0.0) {
                                    first_audio_pts.store(pts_time);
                                    playback_start_time = std::chrono::steady_clock::now();
                                }
                                last_audio_pts.store(pts_time);

                                int needed_out_samples = static_cast<int>(av_rescale_rnd(
                                    swr_get_delay(swr_ctx, target_rate) + a_frm->nb_samples,
                                    target_rate, audio_codec_ctx->sample_rate, AV_ROUND_UP));
                                if (needed_out_samples > a_out_max_samples) {
                                    if (a_out_buf) av_freep(&a_out_buf);
                                    av_samples_alloc(&a_out_buf, nullptr, 2, needed_out_samples, AV_SAMPLE_FMT_S16, 0);
                                    a_out_max_samples = needed_out_samples;
                                }

                                const uint8_t* input_data[8];
                                for (int i = 0; i < 8; ++i) input_data[i] = a_frm->data[i];
                                int out_samples = swr_convert(swr_ctx, &a_out_buf, a_out_max_samples, input_data, a_frm->nb_samples);
                                if (out_samples > 0) {
                                    double out_sec = static_cast<double>(out_samples) / static_cast<double>(target_rate);
                                    last_audio_pts.store(pts_time + out_sec);
                                    size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                                    std::vector<uint8_t> pcm_chunk(a_out_buf, a_out_buf + pcm_bytes);
                                    if (on_audio_pcm_cb) {
                                        on_audio_pcm_cb(pcm_chunk.data(), pcm_chunk.size());
                                    }
                                    if (!local_audio_stream) {
                                         SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, target_rate};
                                         local_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
                                         if (local_audio_stream) {
                                             if (audio_clock_initialized.load() || decoded_video_queue.size() >= 15 || !has_video.load()) {
                                                 SDL_ResumeAudioStreamDevice(local_audio_stream);
                                             } else {
                                                 SDL_PauseAudioStreamDevice(local_audio_stream);
                                             }
                                         }
                                    }
                                    if (local_audio_stream) {
                                        float vol_factor = static_cast<float>(volume.load()) / 100.0f;
                                        if (vol_factor < 1.0f) {
                                            int16_t* samples = reinterpret_cast<int16_t*>(pcm_chunk.data());
                                            size_t sample_count = pcm_chunk.size() / sizeof(int16_t);
                                            for (size_t i = 0; i < sample_count; ++i) {
                                                samples[i] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(samples[i] * vol_factor), -32768, 32767));
                                            }
                                        }
                                        SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                                        if (SDL_AudioStreamDevicePaused(local_audio_stream) && !is_paused.load()) {
                                            SDL_ResumeAudioStreamDevice(local_audio_stream);
                                        }
                                    }
                                    if (!pcm_chunk.empty()) {
                                        const int16_t* samples = reinterpret_cast<const int16_t*>(pcm_chunk.data());
                                        size_t f_count = pcm_chunk.size() / (2 * sizeof(int16_t));
                                        int32_t max_l = 0, max_r = 0;
                                        for (size_t i = 0; i < f_count; ++i) {
                                            int32_t l = std::abs(static_cast<int32_t>(samples[i * 2]));
                                            int32_t r = std::abs(static_cast<int32_t>(samples[i * 2 + 1]));
                                            if (l > max_l) max_l = l;
                                            if (r > max_r) max_r = r;
                                        }
                                        float peak_l = static_cast<float>(max_l) / 32768.0f;
                                        float peak_r = static_cast<float>(max_r) / 32768.0f;
                                        {
                                            std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
                                            double buf_lag = local_audio_stream ? static_cast<double>(SDL_GetAudioStreamQueued(local_audio_stream)) / static_cast<double>(target_rate * 4) : 0.0;
                                            double arrival = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() + buf_lag;
                                            audio_peak_queue.push_back({arrival, peak_l, peak_r});
                                            if (audio_peak_queue.size() > 300) {
                                                audio_peak_queue.pop_front();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    av_packet_unref(a_pkt);
                }
            }
            if (a_out_buf) av_freep(&a_out_buf);
            av_packet_free(&a_pkt);
            av_frame_free(&a_frm);
        });
    }

    while (ffmpeg_running.load()) {
        double target = seek_target.exchange(-1.0);
        if (target >= 0.0) {
            std::lock_guard<std::mutex> s_lock(seek_mutex);
            video_eof = (video_stream_idx < 0);
            audio_eof = (audio_stream_idx < 0);
            first_audio_pts.store(-1.0);
            first_video_pts.store(-1.0);
            audio_clock_initialized.store(false);
            int seek_stream = video_stream_idx >= 0 ? video_stream_idx : audio_stream_idx;
            if (seek_stream >= 0 && video_format_ctx) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, video_format_ctx->streams[seek_stream]->time_base);
                av_seek_frame(video_format_ctx, seek_stream, target_pts, AVSEEK_FLAG_BACKWARD);
                if (video_codec_ctx) avcodec_flush_buffers(video_codec_ctx);
                if (audio_codec_ctx && audio_format_ctx == video_format_ctx) avcodec_flush_buffers(audio_codec_ctx);
            }
            if (audio_stream_idx >= 0 && audio_format_ctx && audio_format_ctx != video_format_ctx) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, audio_format_ctx->streams[audio_stream_idx]->time_base);
                av_seek_frame(audio_format_ctx, audio_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);
                if (audio_codec_ctx) avcodec_flush_buffers(audio_codec_ctx);
            }

            if (local_audio_stream) {
                SDL_ClearAudioStream(local_audio_stream);
            }
            {
                std::lock_guard<std::mutex> q_lock(video_queue_mutex);
                if (target > 0.05 || decoded_video_queue.empty() || decoded_video_queue.front().pts > 0.05) {
                    decoded_video_queue.clear();
                }
            }
            {
                std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
                audio_peak_queue.clear();
            }

            position.store(target);
            last_audio_pts.store(target);
        }

        if (is_paused.load() && !is_adlib_item.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            start_time += std::chrono::milliseconds(10);
            continue;
        }

        int queued_audio = 0;
        if (is_cast_active.load()) {
            if (get_cast_queue_size_cb) {
                queued_audio = static_cast<int>(get_cast_queue_size_cb());
            }
        } else if (local_audio_stream) {
            queued_audio = SDL_GetAudioStreamQueued(local_audio_stream);
        }

        size_t v_q_size = 0;
        {
            std::lock_guard<std::mutex> q_lock(video_queue_mutex);
            v_q_size = decoded_video_queue.size();
        }

        if (video_stream_idx >= 0 && audio_stream_idx >= 0) {
            bool is_detached = (media_player::get_detached_item().get() == this);
            size_t max_limit = is_adlib_item.load() ? 600 : 30;
            if (is_detached || is_cast_active.load() || is_adlib_item.load()) {
                if (v_q_size >= max_limit) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            } else {
                int target_rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
                int bytes_per_sec = target_rate * 4;
                if (v_q_size >= 60 && queued_audio >= (bytes_per_sec * 3) / 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }
        } else if (video_stream_idx >= 0) {
            // Video-only stream
            if (v_q_size >= 60) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        } else if (audio_stream_idx >= 0) {
            // Audio-only stream
            int target_rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
            int bytes_per_sec = target_rate * 4;
            if (queued_audio >= bytes_per_sec / 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        if (!is_dual_input) {
            int read_res = av_read_frame(video_format_ctx, packet);
            if (read_res < 0) {
                if (read_res != AVERROR(EAGAIN)) {
                    if (!video_eof && video_codec_ctx && sws_ctx && rgba_frame) {
                        video_eof = true;
                        avcodec_send_packet(video_codec_ctx, NULL);
                        while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                            int src_w = frame->width;
                            int src_h = frame->height;
                            if (src_w > 0 && src_h > 0) {
                                double pts_time = 0.0;
                                if (frame->pts != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->pts) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                } else if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                } else {
                                    pts_time = static_cast<double>(frames_presented.load()) / kFps;
                                }
                                std::memset(rgba_buffer, 0, static_cast<size_t>(size));
                                rgba_frame->data[0] = rgba_buffer + (offset_y * kWidth * 4) + (offset_x * 4);
                                sws_scale(sws_ctx, frame->data, frame->linesize, 0, src_h, rgba_frame->data, rgba_frame->linesize);
                                {
                                    std::lock_guard<std::mutex> lock(video_queue_mutex);
                                    decoded_video_queue.push_back({
                                        std::vector<uint8_t>(rgba_buffer, rgba_buffer + size),
                                        pts_time
                                    });
                                }
                                has_video.store(true);
                            }
                        }
                    }
                    if (is_adlib_item.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    drain_and_finish();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            if (packet->stream_index == video_stream_idx && video_codec_ctx) {
                if (avcodec_send_packet(video_codec_ctx, packet) >= 0) {
                    while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                        int src_w = frame->width;
                        int src_h = frame->height;
                        if (src_w > 0 && src_h > 0) {
                            if (!sws_ctx || last_src_w != src_w || last_src_h != src_h) {
                                if (sws_ctx) sws_freeContext(sws_ctx);
                                double aspect_ratio = static_cast<double>(src_w) / static_cast<double>(src_h);
                                if (aspect_ratio > 0.0) {
                                    video_aspect_ratio.store(static_cast<float>(aspect_ratio));
                                }
                                if (aspect_ratio > static_cast<double>(kWidth) / static_cast<double>(kHeight)) {
                                    dst_w = kWidth;
                                    dst_h = static_cast<int>(static_cast<double>(kWidth) / aspect_ratio);
                                } else {
                                    dst_h = kHeight;
                                    dst_w = static_cast<int>(static_cast<double>(kHeight) * aspect_ratio);
                                }
                                dst_w = (dst_w / 2) * 2;
                                dst_h = (dst_h / 2) * 2;
                                offset_x = (kWidth - dst_w) / 2;
                                offset_y = (kHeight - dst_h) / 2;

                                sws_ctx = sws_getContext(
                                    src_w, src_h, static_cast<AVPixelFormat>(frame->format),
                                    dst_w, dst_h, AV_PIX_FMT_RGBA,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr
                                );
                                last_src_w = src_w;
                                last_src_h = src_h;
                            }
                        }

                        if (sws_ctx) {
                            double pts_time = 0.0;
                            int64_t v_pts_raw = frame->best_effort_timestamp;
                            if (v_pts_raw == AV_NOPTS_VALUE) v_pts_raw = frame->pts;

                            if (v_pts_raw != AV_NOPTS_VALUE) {
                                pts_time = static_cast<double>(v_pts_raw) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                            } else {
                                pts_time = (first_video_pts.load() >= 0.0 ? first_video_pts.load() : 0.0) + (static_cast<double>(frames_presented.load()) / kFps);
                            }
                            if (first_video_pts.load() < 0.0 && pts_time >= 0.0) {
                                first_video_pts.store(pts_time);
                            }

                            std::memset(rgba_buffer, 0, static_cast<size_t>(size));
                            rgba_frame->data[0] = rgba_buffer + (offset_y * kWidth * 4) + (offset_x * 4);
                            sws_scale(sws_ctx, frame->data, frame->linesize, 0, src_h, rgba_frame->data, rgba_frame->linesize);

                            {
                                std::lock_guard<std::mutex> lock(video_queue_mutex);
                                decoded_video_queue.push_back({
                                    std::vector<uint8_t>(rgba_buffer, rgba_buffer + size),
                                    pts_time
                                });
                            }
                            has_video.store(true);
                        }
                    }
                }
            } else if (packet->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
                has_audio.store(true);
                if (avcodec_send_packet(audio_codec_ctx, packet) >= 0) {
                    while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                        double pts_time = 0.0;
                        if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                            pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[audio_stream_idx]->time_base);
                        } else if (frame->pts != AV_NOPTS_VALUE) {
                            pts_time = static_cast<double>(frame->pts) * av_q2d(video_format_ctx->streams[audio_stream_idx]->time_base);
                        } else {
                            pts_time = last_audio_pts.load() >= 0.0 ? last_audio_pts.load() : 0.0;
                        }
                        if (first_audio_pts.load() < 0.0 && pts_time >= 0.0) {
                            first_audio_pts.store(pts_time);
                        }

                        const uint8_t* input_data[8];
                        for (int i = 0; i < 8; ++i) input_data[i] = frame->data[i];
                        int out_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, input_data, frame->nb_samples);
                        if (out_samples > 0) {
                            int target_rate = audio_codec_ctx->sample_rate > 0 ? audio_codec_ctx->sample_rate : 44100;
                            double out_sec = static_cast<double>(out_samples) / static_cast<double>(target_rate);
                            last_audio_pts.store(pts_time + out_sec);
                            size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                            std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                            if (on_audio_pcm_cb) {
                                on_audio_pcm_cb(pcm_chunk.data(), pcm_chunk.size());
                            }

                            if (is_cast_active.load()) {
                                if (local_audio_stream) { SDL_DestroyAudioStream(local_audio_stream); local_audio_stream = nullptr; }
                                if (push_audio_cb) push_audio_cb(pcm_chunk.data(), pcm_chunk.size());
                            } else {
                                if (!local_audio_stream) {
                                    SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, target_rate};
                                    local_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
                                    if (local_audio_stream) {
                                        SDL_ResumeAudioStreamDevice(local_audio_stream);
                                    }
                                }
                                if (local_audio_stream) {
                                    int max_queued = static_cast<int>(static_cast<size_t>(target_rate) * 2 * sizeof(int16_t) / 2); // 0.5s of audio
                                    while (ffmpeg_running.load() && local_audio_stream && SDL_GetAudioStreamQueued(local_audio_stream) > max_queued && is_playing && !is_paused.load()) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                    }
                                    float vol_factor = static_cast<float>(volume.load()) / 100.0f;
                                    if (vol_factor < 1.0f) {
                                        int16_t* samples = reinterpret_cast<int16_t*>(pcm_chunk.data());
                                        size_t sample_count = pcm_chunk.size() / sizeof(int16_t);
                                        for (size_t i = 0; i < sample_count; ++i) {
                                            samples[i] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(samples[i] * vol_factor), -32768, 32767));
                                        }
                                    }
                                     if (!is_adlib_item.load()) {
                                         SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                                         if (SDL_AudioStreamDevicePaused(local_audio_stream) && !is_paused.load()) {
                                             SDL_ResumeAudioStreamDevice(local_audio_stream);
                                         }
                                     }
                                    if (audio_clock_initialized.load()) {
                                        double spk_pts = get_speaker_audio_pts();
                                        double live_pos = start_offset.load() + (spk_pts - first_audio_pts.load());
                                        double max_dur = duration.load();
                                        if (max_dur > 0.0 && live_pos > max_dur) live_pos = max_dur;
                                        double prev_pos = position.load();
                                        if (live_pos > prev_pos) position.store(live_pos);
                                    }
                                    int q_bytes = SDL_GetAudioStreamQueued(local_audio_stream);
                                    (void)q_bytes;
                                }
                            }

                            if (!pcm_chunk.empty()) {
                                const int16_t* samples = reinterpret_cast<const int16_t*>(pcm_chunk.data());
                                size_t f_count = pcm_chunk.size() / (2 * sizeof(int16_t));
                                int32_t max_l = 0, max_r = 0;
                                for (size_t i = 0; i < f_count; ++i) {
                                    int32_t l = std::abs(static_cast<int32_t>(samples[i * 2]));
                                    int32_t r = std::abs(static_cast<int32_t>(samples[i * 2 + 1]));
                                    if (l > max_l) max_l = l;
                                    if (r > max_r) max_r = r;
                                }
                                float peak_l = static_cast<float>(max_l) / 32768.0f;
                                float peak_r = static_cast<float>(max_r) / 32768.0f;
                                float prev_l = current_audio_peak_l.load();
                                float prev_r = current_audio_peak_r.load();
                                current_audio_peak_l.store(std::max(prev_l * 0.85f, peak_l));
                                current_audio_peak_r.store(std::max(prev_r * 0.85f, peak_r));
                                {
                                    std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
                                    double buf_lag = local_audio_stream ? static_cast<double>(SDL_GetAudioStreamQueued(local_audio_stream)) / 176400.0 : 0.0;
                                    double arrival = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() + buf_lag;
                                    audio_peak_queue.push_back({arrival, peak_l, peak_r});
                                    if (audio_peak_queue.size() > 300) {
                                        audio_peak_queue.pop_front();
                                    }
                                }
                            }
                        }
                    }
                }
            }
            av_packet_unref(packet);
        } else {
            bool did_work = false;
            size_t current_q_size = 0;
            {
                std::lock_guard<std::mutex> q_size_lock(video_queue_mutex);
                current_q_size = decoded_video_queue.size();
            }
            size_t max_q_limit = 600;
            if (video_stream_idx >= 0 && !video_eof && current_q_size < max_q_limit) {
                int read_res = -1;
                {
                    std::lock_guard<std::mutex> s_lock(seek_mutex);
                    read_res = av_read_frame(video_format_ctx, packet);
                }
                static int read_count = 0;
                if (++read_count % 30 == 0 || read_res < 0) {
                    std::cout << "[AVReadDebug] read_res=" << read_res << " stream=" << (read_res >= 0 ? packet->stream_index : -1) << " q_size=" << current_q_size << " v_idx=" << video_stream_idx << std::endl;
                }
                if (read_res >= 0) {
                    did_work = true;
                    if (packet->stream_index == video_stream_idx && video_codec_ctx) {
                        if (avcodec_send_packet(video_codec_ctx, packet) >= 0) {
                            while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                                int src_w = frame->width;
                                int src_h = frame->height;
                                if (src_w > 0 && src_h > 0) {
                                    if (!sws_ctx || last_src_w != src_w || last_src_h != src_h) {
                                        if (sws_ctx) sws_freeContext(sws_ctx);
                                        double aspect_ratio = static_cast<double>(src_w) / static_cast<double>(src_h);
                                        if (aspect_ratio > 0.0) {
                                            video_aspect_ratio.store(static_cast<float>(aspect_ratio));
                                        }
                                        if (aspect_ratio > static_cast<double>(kWidth) / static_cast<double>(kHeight)) {
                                            dst_w = kWidth;
                                            dst_h = static_cast<int>(static_cast<double>(kWidth) / aspect_ratio);
                                        } else {
                                            dst_h = kHeight;
                                            dst_w = static_cast<int>(static_cast<double>(kHeight) * aspect_ratio);
                                        }
                                        dst_w = std::max(64, (dst_w / 2) * 2);
                                        dst_h = std::max(64, (dst_h / 2) * 2);
                                        offset_x = (kWidth - dst_w) / 2;
                                        offset_y = (kHeight - dst_h) / 2;

                                        sws_ctx = sws_getContext(
                                            src_w, src_h, static_cast<AVPixelFormat>(frame->format),
                                            dst_w, dst_h, AV_PIX_FMT_RGBA,
                                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
                                        );
                                        last_src_w = src_w;
                                        last_src_h = src_h;
                                    }
                                }

                                if (sws_ctx) {
                                    double pts_time = 0.0;
                                    if (frame->pts != AV_NOPTS_VALUE) {
                                        pts_time = static_cast<double>(frame->pts) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                    } else if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                        pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                    } else {
                                        pts_time = (first_video_pts.load() >= 0.0 ? first_video_pts.load() : 0.0) + (static_cast<double>(frames_presented.load()) / kFps);
                                    }
                                    if (first_video_pts.load() < 0.0 && pts_time >= 0.0) {
                                        first_video_pts.store(pts_time);
                                    }

                                    std::memset(rgba_buffer, 0, static_cast<size_t>(size));
                                    rgba_frame->data[0] = rgba_buffer + (offset_y * kWidth * 4) + (offset_x * 4);
                                    sws_scale(sws_ctx, frame->data, frame->linesize, 0, src_h, rgba_frame->data, rgba_frame->linesize);

                                    {
                                        std::lock_guard<std::mutex> lock(video_queue_mutex);
                                        decoded_video_queue.push_back({
                                            std::vector<uint8_t>(rgba_buffer, rgba_buffer + size),
                                            pts_time
                                        });
                                        if (decoded_video_queue.size() % 10 == 0 || decoded_video_queue.size() > 180) {
                                            std::cout << "[DecodeLoopDebug] pushed frame pts=" << pts_time << " new_q_size=" << decoded_video_queue.size() << std::endl;
                                        }
                                    }
                                    has_video.store(true);
                                }
                            }
                        }
                    } else if (!is_dual_input && packet->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
                        if (avcodec_send_packet(audio_codec_ctx, packet) >= 0) {
                            while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                                double pts_time = 0.0;
                                if (frame->pts != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->pts) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                                } else if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                                } else {
                                    pts_time = last_audio_pts.load() >= 0.0 ? last_audio_pts.load() : 0.0;
                                }
                                if (first_audio_pts.load() < 0.0 && pts_time >= 0.0) {
                                    first_audio_pts.store(pts_time);
                                }

                                const uint8_t* input_data[8];
                                for (int i = 0; i < 8; ++i) input_data[i] = frame->data[i];
                                int out_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, input_data, frame->nb_samples);
                                if (out_samples > 0) {
                                    int target_rate = audio_sample_rate.load() > 0 ? audio_sample_rate.load() : 44100;
                                    double out_sec = static_cast<double>(out_samples) / static_cast<double>(target_rate);
                                    last_audio_pts.store(pts_time + out_sec);
                                    size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                                    std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                                    if (on_audio_pcm_cb) {
                                        on_audio_pcm_cb(pcm_chunk.data(), pcm_chunk.size());
                                    }

                                    if (!local_audio_stream) {
                                        SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, target_rate};
                                        local_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
                                        if (local_audio_stream) SDL_ResumeAudioStreamDevice(local_audio_stream);
                                    }
                                    if (local_audio_stream) {
                                        float vol_factor = static_cast<float>(volume.load()) / 100.0f;
                                        if (vol_factor < 1.0f) {
                                            int16_t* samples = reinterpret_cast<int16_t*>(pcm_chunk.data());
                                            size_t sample_count = pcm_chunk.size() / sizeof(int16_t);
                                            for (size_t i = 0; i < sample_count; ++i) {
                                                samples[i] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(samples[i] * vol_factor), -32768, 32767));
                                            }
                                        }
                                         if (!is_adlib_item.load()) {
                                             SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                                             if (SDL_AudioStreamDevicePaused(local_audio_stream) && !is_paused.load()) {
                                                 SDL_ResumeAudioStreamDevice(local_audio_stream);
                                             }
                                         }
                                    }
                                }
                            }
                        }
                    }
                    av_packet_unref(packet);
                } else if (read_res == AVERROR_EOF) {
                    if (!video_eof) {
                        video_eof = true;
                        if (video_codec_ctx && sws_ctx && rgba_frame) {
                            avcodec_send_packet(video_codec_ctx, NULL);
                            while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                                double pts_time = 0.0;
                                if (frame->pts != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->pts) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                } else if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                } else {
                                    pts_time = last_presented_pts.load() >= 0.0 ? last_presented_pts.load() + (1.0 / 24.0) : 0.0;
                                }
                                uint8_t* flush_buffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(kWidth * kHeight * 4)));
                                if (flush_buffer) {
                                    int f_off_x = 0, f_off_y = 0;
                                    int flush_w = video_codec_ctx->width;
                                    int flush_h = video_codec_ctx->height;
                                    if (flush_w > 0 && flush_h > 0) {
                                        float scale_w = static_cast<float>(kWidth) / static_cast<float>(flush_w);
                                        float scale_h = static_cast<float>(kHeight) / static_cast<float>(flush_h);
                                        float scale = std::min(scale_w, scale_h);
                                        int target_w = static_cast<int>(static_cast<float>(flush_w) * scale);
                                        int target_h = static_cast<int>(static_cast<float>(flush_h) * scale);
                                        f_off_x = (kWidth - target_w) / 2;
                                        f_off_y = (kHeight - target_h) / 2;
                                    }
                                    std::memset(flush_buffer, 0, static_cast<size_t>(kWidth * kHeight * 4));
                                    rgba_frame->data[0] = flush_buffer + (f_off_y * kWidth * 4) + (f_off_x * 4);
                                    sws_scale(sws_ctx, frame->data, frame->linesize, 0, flush_h, rgba_frame->data, rgba_frame->linesize);
                                    {
                                        std::lock_guard<std::mutex> lock(video_queue_mutex);
                                        decoded_video_queue.push_back({
                                            std::vector<uint8_t>(flush_buffer, flush_buffer + (kWidth * kHeight * 4)),
                                            pts_time
                                        });
                                    }
                                    free(flush_buffer);
                                }
                            }
                        }
                    }
                }
            }

            if (video_eof && audio_eof.load()) {
                drain_and_finish();
                break;
            }

            if (!did_work) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    if (audio_thread.joinable()) {
        audio_thread.join();
    }

    if (audio_out_buf) av_freep(&audio_out_buf);
    av_free(rgba_buffer);
    av_frame_free(&rgba_frame);
    av_frame_free(&frame);
    av_packet_free(&packet);

    if (sws_ctx) sws_freeContext(sws_ctx);
    if (swr_ctx) swr_free(&swr_ctx);
    if (video_codec_ctx) avcodec_free_context(&video_codec_ctx);
    if (audio_codec_ctx) avcodec_free_context(&audio_codec_ctx);

    if (audio_format_ctx && audio_format_ctx != video_format_ctx) {
        avformat_close_input(&audio_format_ctx);
    }
    if (video_format_ctx) {
        avformat_close_input(&video_format_ctx);
    }

    is_playing = false;
    ffmpeg_running.store(false);
    player_pid = 0;
}

std::vector<uint8_t> media_player_item::get_current_adlib_frame_pixels() const {
    std::lock_guard<std::mutex> q_lock(const_cast<std::mutex&>(video_queue_mutex));
    if (decoded_video_queue.empty()) return {};
    double cur_pos = get_current_position();
    double src_fps = 24.0;
    int target_src_idx = static_cast<int>(std::round(cur_pos * src_fps));
    size_t target_idx = target_src_idx < 0 ? 0 : static_cast<size_t>(target_src_idx);
    size_t idx = std::clamp<size_t>(target_idx, 0, decoded_video_queue.size() - 1);
    return decoded_video_queue[idx].pixels;
}

ImTextureID media_player_item::get_texture_id(SDL_GPUDevice* device, SDL_GPUCommandBuffer* existing_cmdbuf) {
    if (!device) device = TextureHelper::g_gpu_device;
    if (!device) {
        try {
            auto r_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (r_ptr && *r_ptr) device = *r_ptr;
        } catch (...) {}
    }
    if (!device) return ImTextureID{};

    std::lock_guard<std::mutex> lock(texture_mutex);
    if (!video_texture) {
        SDL_GPUTextureCreateInfo texture_info = {};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = kWidth;
        texture_info.height = kHeight;
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        SDL_GPUTexture* raw_texture = SDL_CreateGPUTexture(device, &texture_info);
        if (raw_texture) {
            video_texture = new RouenGPUTexture();
            video_texture->binding.texture = raw_texture;
            video_texture->binding.sampler = TextureHelper::getDefaultSampler(device);
            video_texture->width = kWidth;
            video_texture->height = kHeight;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = kHeight * kWidth * 4;
        upload_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);

        if (upload_buffer && video_texture && video_texture->binding.texture) {
            Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, upload_buffer, false));
            if (map) {
                std::memset(map, 0, kHeight * kWidth * 4);
                SDL_UnmapGPUTransferBuffer(device, upload_buffer);
                SDL_GPUCommandBuffer* cmd_buf = existing_cmdbuf ? existing_cmdbuf : SDL_AcquireGPUCommandBuffer(device);
                if (cmd_buf) {
                    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);
                    if (copy_pass) {
                        SDL_GPUTextureTransferInfo transfer_info_gpu = {};
                        transfer_info_gpu.transfer_buffer = upload_buffer;
                        transfer_info_gpu.offset = 0;
                        transfer_info_gpu.pixels_per_row = kWidth;
                        transfer_info_gpu.rows_per_layer = kHeight;
                        SDL_GPUTextureRegion region = {};
                        region.texture = video_texture->binding.texture;
                        region.w = kWidth;
                        region.h = kHeight;
                        region.d = 1;
                        SDL_UploadToGPUTexture(copy_pass, &transfer_info_gpu, &region, false);
                        SDL_EndGPUCopyPass(copy_pass);
                    }
                    if (!existing_cmdbuf) {
                        SDL_SubmitGPUCommandBuffer(cmd_buf);
                    }
                }
            }
        }
    }
    {
        std::vector<uint8_t> frame_to_present;
        bool is_detached = (media_player::get_detached_item().get() == this);
        if (is_cast_active.load() || is_detached || is_adlib_item.load()) {
            std::lock_guard<std::mutex> q_lock(video_queue_mutex);
            if (!decoded_video_queue.empty()) {
                double cur_pos = get_current_position();
                if (is_adlib_item.load() || rouen::helpers::AdLibEngine::instance().is_recording()) {
                    int rec_tick = static_cast<int>(std::round(cur_pos * 30.0));
                    int target_src_idx = (rec_tick * 4) / 5;
                    size_t target_idx = target_src_idx < 0 ? 0 : static_cast<size_t>(target_src_idx);
                    size_t idx = std::clamp<size_t>(target_idx, 0, decoded_video_queue.size() - 1);
                    static int log_counter = 0;
                    if (++log_counter % 30 == 0) {
                        std::cout << "[AdLibTextureDebug] cur_pos=" << cur_pos << " rec_tick=" << rec_tick << " target_src_idx=" << target_src_idx << " idx=" << idx << " q_size=" << decoded_video_queue.size() << std::endl;
                    }
                    frame_to_present = decoded_video_queue[idx].pixels;
                } else {
                    if (decoded_video_queue.size() > 1) {
                        if (decoded_video_queue[1].pts <= cur_pos || decoded_video_queue[1].pts < 0.0) {
                            frame_to_present = std::move(decoded_video_queue.front().pixels);
                            decoded_video_queue.pop_front();
                        } else {
                            frame_to_present = decoded_video_queue.front().pixels;
                        }
                    } else {
                        double f_pts = decoded_video_queue.front().pts;
                        if (f_pts <= cur_pos || f_pts < 0.0) {
                            frame_to_present = decoded_video_queue.front().pixels;
                        }
                    }
                }
            }
        } else {
            std::lock_guard<std::mutex> q_lock(video_queue_mutex);

            if (local_audio_stream && SDL_AudioStreamDevicePaused(local_audio_stream)) {
                if (!decoded_video_queue.empty() || !has_video.load()) {
                    SDL_ResumeAudioStreamDevice(local_audio_stream);
                    if (!audio_clock_initialized.load()) {
                        audio_callback_time = std::chrono::steady_clock::now();
                        audio_clock_initialized.store(true);
                    }
                }
            } else if (!audio_clock_initialized.load()) {
                if (!decoded_video_queue.empty() || !has_video.load()) {
                    audio_callback_time = std::chrono::steady_clock::now();
                    audio_clock_initialized.store(true);
                }
            }

            double speaker_pts = get_speaker_audio_pts();

            if (!decoded_video_queue.empty()) {
                // Keep video synchronized with speaker audio:
                // Advance video queue until the front frame's display interval covers speaker_pts.
                while (decoded_video_queue.size() > 1 && audio_clock_initialized.load()) {
                    double next_pts = decoded_video_queue[1].pts;
                    if (next_pts >= 0.0 && speaker_pts >= next_pts) {
                        decoded_video_queue.pop_front();
                    } else {
                        break;
                    }
                }

                const auto& front = decoded_video_queue.front();
                double video_pts = front.pts;
                double delta = video_pts - speaker_pts;

                last_av_sync_delta_ms.store(delta * 1000.0);

                if (audio_clock_initialized.load() && speaker_pts >= video_pts) {
                    last_presented_pts.store(front.pts);
                    frames_presented.fetch_add(1, std::memory_order_relaxed);
                    frame_to_present = std::move(decoded_video_queue.front().pixels);
                    decoded_video_queue.pop_front();
                } else if (has_video.load() && first_audio_pts.load() < 0.0) {
                    // Video-only stream
                    last_presented_pts.store(front.pts);
                    frames_presented.fetch_add(1, std::memory_order_relaxed);
                    frame_to_present = std::move(decoded_video_queue.front().pixels);
                    decoded_video_queue.pop_front();
                } else {
                    // HOLD: Frame is in the future relative to physical speaker audio
                    frames_held.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        if (!frame_to_present.empty()) {
            std::lock_guard<std::mutex> f_lock(frame_mutex);
            back_pixels = std::move(frame_to_present);
            new_frame_ready.store(true);
        }
    }

    if (new_frame_ready.load(std::memory_order_relaxed) || is_adlib_item.load()) {
        std::vector<uint8_t> local_pixels;
        {
            std::lock_guard<std::mutex> f_lock(frame_mutex);
            local_pixels = back_pixels;
            if (!is_adlib_item.load()) {
                new_frame_ready.store(false, std::memory_order_relaxed);
            }
        }
        if (!local_pixels.empty() && video_texture && upload_buffer) {
            if (local_pixels.size() >= 4096) {
                uint64_t sum = 0;
                size_t count = 0;
                for (size_t i = 0; i < local_pixels.size(); i += 4096) {
                    sum += static_cast<uint64_t>(local_pixels[i]) + local_pixels[i + 1] + local_pixels[i + 2];
                    count++;
                }
                if (count > 0) {
                    float lum = static_cast<float>(sum) / (static_cast<float>(count) * 3.0f * 255.0f);
                    float prev_lum = current_luminance.load();
                    current_luminance.store(std::max(prev_lum * 0.92f, lum));
                }
            }
            Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, upload_buffer, false));
            if (map) {
                std::memcpy(map, local_pixels.data(), kWidth * kHeight * 4);
                SDL_UnmapGPUTransferBuffer(device, upload_buffer);
                SDL_GPUCommandBuffer* cmd_buf = existing_cmdbuf;
                bool custom_buf = false;
                if (!cmd_buf) {
                    try {
                        auto r_buf = registrar::get<SDL_GPUCommandBuffer*>("current_cmdbuf");
                        if (r_buf && *r_buf) cmd_buf = *r_buf;
                    } catch (...) {}
                }
                if (!cmd_buf) {
                    cmd_buf = SDL_AcquireGPUCommandBuffer(device);
                    custom_buf = true;
                }
                if (cmd_buf) {
                    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);
                    if (copy_pass) {
                        SDL_GPUTextureTransferInfo transfer_info_gpu = {};
                        transfer_info_gpu.transfer_buffer = upload_buffer;
                        transfer_info_gpu.offset = 0;
                        transfer_info_gpu.pixels_per_row = kWidth;
                        transfer_info_gpu.rows_per_layer = kHeight;
                        SDL_GPUTextureRegion region = {};
                        region.texture = video_texture->binding.texture;
                        region.w = kWidth;
                        region.h = kHeight;
                        region.d = 1;
                        SDL_UploadToGPUTexture(copy_pass, &transfer_info_gpu, &region, false);
                        SDL_EndGPUCopyPass(copy_pass);

                        gpu_frames_rendered.fetch_add(1, std::memory_order_relaxed);
                        auto now_gpu = std::chrono::steady_clock::now();
                        if (last_gpu_upload_time.time_since_epoch().count() > 0) {
                            double dt_gpu = std::chrono::duration<double>(now_gpu - last_gpu_upload_time).count();
                            if (dt_gpu > 0.001) {
                                float inst_fps = static_cast<float>(1.0 / dt_gpu);
                                float old_fps = actual_rendering_fps.load(std::memory_order_relaxed);
                                actual_rendering_fps.store(old_fps * 0.9f + inst_fps * 0.1f, std::memory_order_relaxed);
                            }
                        }
                        last_gpu_upload_time = now_gpu;
                    }
                    if (custom_buf) {
                        SDL_SubmitGPUCommandBuffer(cmd_buf);
                    }
                }
                if (!has_presented_first_frame.load()) {
                    playback_start_time = std::chrono::steady_clock::now();
                    double cur_off = std::max(start_offset.load(), position.load());
                    start_offset.store(cur_off);
                    position.store(cur_off);
                    has_presented_first_frame.store(true);
                }
            }
        }
    }
    return rouen::helpers::texture_id_cast(video_texture);
}
