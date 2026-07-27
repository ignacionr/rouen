#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include "sdl_compat.hpp"
#include <algorithm>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <deque>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cerrno>
#include <functional>
#include <optional>
#include <iostream>

#include <SDL3/SDL.h>
#include "./imgui_include.hpp"
#include "texture_utils.hpp"
#include "platform_utils.hpp"
#include "config_service.hpp"
#include "process_helper.hpp"
#include "../registrar.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../../src/helpers/glaze_include.hpp"

/**
 * media_player_item — FFmpeg Library-based internal video decoding & frame streaming engine.
 *
 * Replaces external processes with in-engine FFmpeg library pipeline:
 *  - Decodes media directly to RGBA frames on a background thread.
 *  - Feeds audio PCM chunks directly to VideoFeedHost.
 *  - Uploads video frames directly to SDL3 GPU textures.
 */
struct media_player_item {
    static constexpr int kWidth = 1280;
    static constexpr int kHeight = 720;
    static constexpr int kFps = 30;

    struct window_rect {
        int x{0};
        int y{0};
        int w{0};
        int h{0};
        bool operator==(const window_rect&) const = default;
    };

    std::string url;
    int player_pid{0}; // Kept for backward compatibility with active card tracking
    bool is_playing{false};
    std::atomic<bool> is_paused{false};
    mutable std::atomic<double> position{0.0};
    std::atomic<double> duration{0.0};
    std::atomic<int> volume{100};
    std::atomic<float> vu_level_l{0.0f};
    std::atomic<float> vu_level_r{0.0f};
    std::atomic<float> vu_watermark_l{0.0f};
    std::atomic<float> vu_watermark_r{0.0f};
    struct audio_peak_sample {
        double wall_time{0.0};  // wall-clock time when this audio will be heard (steady_clock seconds)
        float peak_l{0.0f};
        float peak_r{0.0f};
    };
    std::mutex audio_peak_mutex;
    std::deque<audio_peak_sample> audio_peak_queue;
    std::atomic<float> video_aspect_ratio{16.0f / 9.0f};
    std::atomic<bool> has_video{false};
    std::atomic<float> current_luminance{0.0f};
    std::atomic<float> current_audio_peak_l{0.0f};
    std::atomic<float> current_audio_peak_r{0.0f};
    std::atomic<bool> has_presented_first_frame{false};
    std::atomic<double> initial_pts_offset{0.0};
    std::atomic<double> first_video_pts{-1.0};
    std::atomic<double> first_audio_pts{-1.0};
    std::atomic<double> start_offset{0.0};
    mutable std::chrono::steady_clock::time_point playback_start_time{};
    mutable std::chrono::steady_clock::time_point baseline_tp{};
    mutable std::atomic<bool> baseline_set{false};
    mutable std::atomic<double> baseline_start_pts{-1.0};
    mutable std::chrono::steady_clock::time_point pause_start_tp{};
    mutable std::chrono::steady_clock::time_point last_video_present_time{};
    mutable double current_frame_duration{1.0 / 30.0};
    mutable double last_presented_pts{-1.0};
    mutable std::atomic<double> last_av_sync_delta_ms{0.0};  // (video_pts - audio_pts) in ms at last frame present
    mutable std::atomic<int64_t> frames_presented{0};
    mutable std::atomic<int64_t> frames_dropped{0};
    mutable std::atomic<int64_t> frames_held{0};
    long long feed_id{-1};
    std::string item_link;
    std::string item_title;
    std::optional<double> watermark;
    std::optional<window_rect> last_docked_video_rect;
    bool user_tall_layout{false};
    bool user_tall_layout_set{false};
    const void* owner_card{nullptr};
    static inline std::atomic<bool> is_cast_active{false};
    static inline std::function<void(long long, const std::string&, const std::string&, double)> save_watermark_cb;
    static inline std::function<void(const uint8_t*, size_t)> push_audio_cb;
    static inline std::function<void()> reset_sync_cb;
    static inline std::function<size_t()> get_cast_queue_size_cb;
    static inline std::function<bool()> is_offscreen_ctx_cb;

    // FFmpeg Engine Members
    std::thread ffmpeg_thread;
    std::atomic<bool> ffmpeg_running{false};
    std::atomic<double> seek_target{-1.0};
    std::mutex frame_mutex;
    std::vector<uint8_t> back_pixels;
    std::atomic<bool> new_frame_ready{false};
    RouenGPUTexture* video_texture{nullptr};
    SDL_GPUTransferBuffer* upload_buffer{nullptr};
    std::mutex texture_mutex;
    SDL_AudioStream* local_audio_stream{nullptr};
    std::atomic<double> last_audio_pts{0.0};
    double m_last_valid_audio_time{0.0};
    std::chrono::steady_clock::time_point m_last_clock_time{};

    struct decoded_video_frame {
        std::vector<uint8_t> pixels;
        double pts;
    };
    std::mutex video_queue_mutex;
    std::deque<decoded_video_frame> decoded_video_queue;

    media_player_item() = default;
    media_player_item(const media_player_item&) = delete;
    media_player_item& operator=(const media_player_item&) = delete;
    media_player_item(media_player_item&&) = delete;
    media_player_item& operator=(media_player_item&&) = delete;
    
    ~media_player_item() { 
        stopMedia(); 
    }

    bool checkMediaStatus();
    void stopMedia();
    std::string urlDecode(const std::string& encoded);
    bool isUrlEncoded(const std::string& input_str);
    std::string sanitizeURL(const std::string& input_url);
    bool playMedia(const void* owner = nullptr);
    std::string formatTime(double seconds) const;
    bool seekTo(double position_seconds);
    bool setVolume(int new_volume);
    bool pauseMedia();
    bool resumeMedia();
    bool togglePause();
    double get_current_position() const {
        if (!is_playing) return position.load();
        if (is_paused.load()) return position.load();

        auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - playback_start_time).count();
        double live_pos = start_offset.load() + elapsed;
        double max_dur = duration.load();
        if (max_dur > 0.0 && live_pos > max_dur) {
            live_pos = max_dur;
        }
        position.store(live_pos);
        return live_pos;
    }
    bool setPaused(bool paused);
    void update_watermark();
    void update_vu_levels();
    float get_vu_level_l();
    float get_vu_level_r();
    float get_vu_watermark_l();
    float get_vu_watermark_r();
    static int decode_interrupt_cb(void* ctx) {
        auto* player = static_cast<media_player_item*>(ctx);
        if (player && !player->ffmpeg_running.load()) {
            return 1;
        }
        return 0;
    }

    void decode_loop(std::string video_target, std::string audio_target, double start_offset);

    ImTextureID get_texture_id(SDL_GPUDevice* device = nullptr, SDL_GPUCommandBuffer* existing_cmdbuf = nullptr) {
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
            if (is_cast_active.load()) {
                bool is_offscreen = is_offscreen_ctx_cb ? is_offscreen_ctx_cb() : false;
                if (is_offscreen) {
                    std::lock_guard<std::mutex> q_lock(video_queue_mutex);
                    if (!decoded_video_queue.empty()) {
                        frame_to_present = std::move(decoded_video_queue.front().pixels);
                        decoded_video_queue.pop_front();
                    }
                }
            } else {
                if (first_audio_pts.load() < 0.0 && first_video_pts.load() >= 0.0) {
                    auto now = std::chrono::steady_clock::now();
                    double wait_sec = std::chrono::duration<double>(now - playback_start_time).count();
                    if (wait_sec < 2.0 && is_playing) {
                        // Hold video presentation up to 2.0 seconds while waiting for audio stream
                        return reinterpret_cast<ImTextureID>(video_texture);
                    }
                }
                double speaker_audio_time = get_current_position();

                std::lock_guard<std::mutex> q_lock(video_queue_mutex);
                double f_v = first_video_pts.load() >= 0.0 ? first_video_pts.load() : 0.0;
                double f_a = first_audio_pts.load() >= 0.0 ? first_audio_pts.load() : 0.0;

                while (!decoded_video_queue.empty()) {
                    const auto& front = decoded_video_queue.front();
                    double norm_video = front.pts - f_v;
                    double norm_audio = speaker_audio_time - f_a;
                    double delta = norm_video - norm_audio;

                    if (delta > 0.025) {
                        // HOLD: Frame is ahead of speaker audio presentation time
                        frames_held.fetch_add(1, std::memory_order_relaxed);
                        break;
                    } else if (delta < -0.300 && decoded_video_queue.size() > 5) {
                        // SKIP: Frame is severely behind speaker audio presentation time (>300ms)
                        frames_dropped.fetch_add(1, std::memory_order_relaxed);
                        decoded_video_queue.pop_front();
                        continue;
                    } else {
                        // SHOW: Present frame to GPU texture
                        last_av_sync_delta_ms.store(delta * 1000.0);
                        last_presented_pts = front.pts;
                        frames_presented.fetch_add(1, std::memory_order_relaxed);
                        frame_to_present = std::move(decoded_video_queue.front().pixels);
                        decoded_video_queue.pop_front();
                        break;
                    }
                }
            }

            if (!frame_to_present.empty()) {
                std::lock_guard<std::mutex> f_lock(frame_mutex);
                back_pixels = std::move(frame_to_present);
                new_frame_ready.store(true);
            }
        }

        if (new_frame_ready.load()) {
            std::vector<uint8_t> local_pixels;
            {
                std::lock_guard<std::mutex> f_lock(frame_mutex);
                local_pixels = back_pixels;
                new_frame_ready.store(false);
            }
            if (!local_pixels.empty() && video_texture && upload_buffer) {
                if (local_pixels.size() >= 48) {
                    uint64_t sum = 0;
                    size_t count = 0;
                    for (size_t i = 0; i < local_pixels.size(); i += 64) {
                        sum += (local_pixels[i] + local_pixels[i + 1] + local_pixels[i + 2]);
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
                    has_video.store(true);
                }
            }
        }
        return rouen::helpers::texture_id_cast(video_texture);
    }
};

using media_player_item_map = std::unordered_map<ImGuiID, std::shared_ptr<media_player_item>>;

// --- Implementation ---

inline bool media_player_item::checkMediaStatus() {
    return is_playing;
}

inline void media_player_item::update_watermark() {
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
}

inline void media_player_item::stopMedia() {
    update_watermark();

    ffmpeg_running.store(false);
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
        if (device && upload_buffer) {
            SDL_ReleaseGPUTransferBuffer(device, upload_buffer);
            upload_buffer = nullptr;
        }
    }

    is_playing = false;
    is_paused = false;
    has_video.store(false);
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
}

inline std::string media_player_item::urlDecode(const std::string& encoded) {
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

inline bool media_player_item::isUrlEncoded(const std::string& input_str) {
    return input_str.find('%') != std::string::npos;
}

inline std::string media_player_item::sanitizeURL(const std::string& input_url) {
    std::string sanitized_url = input_url;
    if (isUrlEncoded(input_url)) {
        sanitized_url = urlDecode(input_url);
    }
    return sanitized_url;
}

inline bool media_player_item::playMedia(const void* owner) {
    if (owner) {
        owner_card = owner;
    }
    double offset = start_offset;
    stopMedia();
    start_offset.store(offset);
    playback_start_time = std::chrono::steady_clock::now();
    has_video.store(false);
    is_paused = false;

    std::string sanitized_url = sanitizeURL(url);

    ffmpeg_running.store(true);
    is_playing = true;
    position.store(offset);
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

                auto run_ytdlp = [&](const std::string& cookie_args) -> std::pair<std::vector<std::string>, std::string> {
                    std::string cmd;
                    if (!cookie_args.empty()) {
                        cmd = std::format("\"{}\" --no-warnings --remote-components ejs:github --socket-timeout 10 {} -g -f \"bestvideo[vcodec^=avc1]+bestaudio/bestvideo[vcodec^=vp9]+bestaudio/bestvideo+bestaudio/best\" \"{}\" 2>&1", ytdl_exe, cookie_args, sanitized_url);
                    } else {
                        cmd = std::format("\"{}\" --no-warnings --remote-components ejs:github --socket-timeout 10 -g -f \"bestvideo[vcodec^=avc1]+bestaudio/bestvideo[vcodec^=vp9]+bestaudio/bestvideo+bestaudio/best\" \"{}\" 2>&1", ytdl_exe, sanitized_url);
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

inline std::string media_player_item::formatTime(double seconds) const {
    auto hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    auto secs = static_cast<int>(seconds) % 60;
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, secs);
    return std::string(buffer);
}

inline bool media_player_item::seekTo(double position_seconds) {
    start_offset.store(position_seconds);
    playback_start_time = std::chrono::steady_clock::now();
    position.store(position_seconds);
    has_presented_first_frame.store(false);
    baseline_set.store(false);
    baseline_start_pts.store(-1.0);
    last_video_present_time = {};
    current_frame_duration = 1.0 / 30.0;
    last_presented_pts = -1.0;
    last_av_sync_delta_ms.store(0.0);
    frames_presented.store(0);
    frames_dropped.store(0);
    frames_held.store(0);
    if (!ffmpeg_running.load()) {
        return playMedia();
    }
    seek_target.store(position_seconds);
    return true;
}

inline bool media_player_item::setVolume(int new_volume) {
    volume = std::clamp(new_volume, 0, 100);
    return true;
}

inline bool media_player_item::setPaused(bool paused) {
    if (paused && !is_paused.load()) {
        position.store(get_current_position());
        update_watermark();
    } else if (!paused && is_paused.load()) {
        start_offset.store(position.load());
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

inline bool media_player_item::pauseMedia() {
    return setPaused(true);
}

inline bool media_player_item::resumeMedia() {
    return setPaused(false);
}

inline bool media_player_item::togglePause() {
    return is_paused.load() ? resumeMedia() : pauseMedia();
}

inline void media_player_item::update_vu_levels() {
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
            if (sample.wall_time <= now + 0.02) {  // include slightly future peaks
                if (sample.peak_l > target_l) target_l = sample.peak_l;
                if (sample.peak_r > target_r) target_r = sample.peak_r;
            }
        }
    }

    float curr_l = vu_level_l.load();
    float curr_r = vu_level_r.load();

    vu_level_l.store(std::max(curr_l * 0.85f, target_l));
    vu_level_r.store(std::max(curr_r * 0.85f, target_r));
}

inline float media_player_item::get_vu_level_l() {
    update_vu_levels();
    return vu_level_l.load();
}

inline float media_player_item::get_vu_level_r() {
    update_vu_levels();
    return vu_level_r.load();
}

inline float media_player_item::get_vu_watermark_l() {
    return vu_watermark_l.load();
}

inline float media_player_item::get_vu_watermark_r() {
    return vu_watermark_r.load();
}

inline std::string get_ffmpeg_error_string(int errnum) {
    char errbuf[256];
    if (av_strerror(errnum, errbuf, sizeof(errbuf)) == 0) {
        return std::string(errbuf);
    }
    return "Unknown FFmpeg error " + std::to_string(errnum);
}

inline void media_player_item::decode_loop(std::string video_target, std::string audio_target, double offset) {
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
    }

    int err = avformat_open_input(&video_format_ctx, video_target.c_str(), nullptr, &v_opts);
    if (err < 0) {
        if (v_opts) av_dict_free(&v_opts);
        std::string err_msg = get_ffmpeg_error_string(err);
        std::string final_err = std::format("Media Player Error: Failed to open input ({}): {}", video_target, err_msg);
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
            }
            if (avformat_open_input(&audio_format_ctx, audio_target.c_str(), nullptr, &a_opts) < 0) {
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
            video_codec_ctx->thread_count = 4;
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
                
                av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_layout, 0);
                av_opt_set_int(swr_ctx, "out_sample_rate", 44100, 0);
                av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                
                swr_init(swr_ctx);
                av_channel_layout_uninit(&out_layout);
            }
        }
    }

    double total_duration = 0.0;
    if (video_format_ctx->duration != AV_NOPTS_VALUE) {
        total_duration = static_cast<double>(video_format_ctx->duration) / AV_TIME_BASE;
    } else if (audio_format_ctx->duration != AV_NOPTS_VALUE) {
        total_duration = static_cast<double>(audio_format_ctx->duration) / AV_TIME_BASE;
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

    bool is_dual_input = (audio_format_ctx != video_format_ctx);
    bool video_eof = (video_stream_idx < 0);
    bool audio_eof = (audio_stream_idx < 0);

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
        if (audio_codec_ctx && swr_ctx && audio_stream_idx >= 0) {
            avcodec_send_packet(audio_codec_ctx, nullptr);
            while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                double pts_time = 0.0;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[audio_stream_idx]->time_base);
                } else {
                    pts_time = position.load() + (static_cast<double>(frame->nb_samples) / 44100.0);
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
                    if (!local_audio_stream) {
                        SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 44100};
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
                        int q_bytes = SDL_GetAudioStreamQueued(local_audio_stream);
                        double q_sec = static_cast<double>(q_bytes) / 176400.0;
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
            int flush_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, nullptr, 0);
            if (flush_samples > 0) {
                size_t pcm_bytes = static_cast<size_t>(flush_samples) * 2 * sizeof(int16_t);
                std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                if (local_audio_stream) {
                    SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                }
            }
        }

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

    while (ffmpeg_running.load()) {
        double target = seek_target.exchange(-1.0);
        if (target >= 0.0) {
            video_eof = (video_stream_idx < 0);
            audio_eof = (audio_stream_idx < 0);
            if (video_stream_idx >= 0 && video_format_ctx) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, video_format_ctx->streams[video_stream_idx]->time_base);
                avformat_seek_file(video_format_ctx, video_stream_idx, INT64_MIN, target_pts, target_pts, 0);
                if (video_codec_ctx) avcodec_flush_buffers(video_codec_ctx);
            }
            if (audio_stream_idx >= 0 && audio_format_ctx && audio_format_ctx != video_format_ctx) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, audio_format_ctx->streams[audio_stream_idx]->time_base);
                avformat_seek_file(audio_format_ctx, audio_stream_idx, INT64_MIN, target_pts, target_pts, 0);
                if (audio_codec_ctx) avcodec_flush_buffers(audio_codec_ctx);
            }

            if (local_audio_stream) {
                SDL_ClearAudioStream(local_audio_stream);
            }
            {
                std::lock_guard<std::mutex> q_lock(video_queue_mutex);
                decoded_video_queue.clear();
            }
            {
                std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
                audio_peak_queue.clear();
            }

            start_time = std::chrono::steady_clock::now();
            position.store(target);
            last_audio_pts.store(target);
        }

        if (is_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            start_time += std::chrono::milliseconds(10);
            continue;
        }

        if (local_audio_stream && !is_cast_active.load()) {
            int queued = SDL_GetAudioStreamQueued(local_audio_stream);
            if (queued > 176400) { // 1.0 second of audio buffer (176,400 bytes)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        if (!is_dual_input) {
            int read_res = av_read_frame(video_format_ctx, packet);
            if (read_res < 0) {
                if (read_res != AVERROR(EAGAIN)) {
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
                            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                            } else {
                                pts_time = position.load() + (1.0 / kFps);
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
                                if (!is_cast_active.load() && decoded_video_queue.size() > 30) {
                                    decoded_video_queue.pop_front();
                                }
                            }
                            has_video.store(true);
                            if (!local_audio_stream) {
                                position.store(pts_time);
                            }
                        }
                    }
                }
            } else if (packet->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
                if (avcodec_send_packet(audio_codec_ctx, packet) >= 0) {
                    while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                        double pts_time = 0.0;
                        if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                            pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[audio_stream_idx]->time_base);
                        } else {
                            pts_time = position.load() + (static_cast<double>(frame->nb_samples) / 44100.0);
                        }
                        if (first_audio_pts.load() < 0.0 && pts_time >= 0.0) {
                            first_audio_pts.store(pts_time);
                            playback_start_time = std::chrono::steady_clock::now();
                        }
                        last_audio_pts.store(pts_time);
                        double cur_dur = duration.load();
                        if (pts_time > cur_dur) {
                            duration.store(pts_time);
                        }
                        if (video_stream_idx < 0) {
                            position.store(pts_time);
                        }

                        const uint8_t* input_data[8];
                        for (int i = 0; i < 8; ++i) input_data[i] = frame->data[i];
                        int out_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, input_data, frame->nb_samples);
                        if (out_samples > 0) {
                            size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                            std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);

                            if (is_cast_active.load()) {
                                if (local_audio_stream) { SDL_DestroyAudioStream(local_audio_stream); local_audio_stream = nullptr; }
                                if (push_audio_cb) push_audio_cb(pcm_chunk.data(), pcm_chunk.size());
                            } else {
                                if (!local_audio_stream) {
                                    SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 44100};
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
                                    int q_bytes = SDL_GetAudioStreamQueued(local_audio_stream);
                                    double q_sec = static_cast<double>(q_bytes) / 176400.0;
                                    double cur_pos = std::max(0.0, pts_time - q_sec);
                                    position.store(cur_pos);
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
                std::lock_guard<std::mutex> lock(video_queue_mutex);
                current_q_size = decoded_video_queue.size();
            }

            bool video_ahead = false;
            double f_v = first_video_pts.load();
            double f_a = first_audio_pts.load();
            if (audio_stream_idx >= 0 && f_a < 0.0) {
                video_ahead = true; // Hold video decoding until audio stream connects & buffers
            } else if (f_v >= 0.0 && f_a >= 0.0 && local_audio_stream && !decoded_video_queue.empty()) {
                double last_v_pts = decoded_video_queue.back().pts;
                double norm_v = last_v_pts - f_v;
                double norm_a = get_current_position() - f_a;
                if (norm_v > norm_a + 0.300) {
                    video_ahead = true;
                }
            }

            if (video_stream_idx >= 0 && !video_eof && current_q_size < 30 && !video_ahead) {
                int read_res = av_read_frame(video_format_ctx, packet);
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
                                            SWS_BILINEAR, nullptr, nullptr, nullptr
                                        );
                                        last_src_w = src_w;
                                        last_src_h = src_h;
                                    }
                                }

                                if (sws_ctx) {
                                    double pts_time = 0.0;
                                    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                        pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_format_ctx->streams[video_stream_idx]->time_base);
                                    } else {
                                        pts_time = position.load() + (1.0 / kFps);
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
                                    if (!local_audio_stream) {
                                        position.store(pts_time);
                                    }
                                }
                            }
                        }
                    }
                    av_packet_unref(packet);
                } else if (read_res < 0 && read_res != AVERROR(EAGAIN)) {
                    video_eof = true;
                }
            }

            int queued_audio = local_audio_stream ? SDL_GetAudioStreamQueued(local_audio_stream) : 0;
            if (audio_stream_idx >= 0 && !audio_eof && queued_audio < 176400 * 2) {
                int read_res = av_read_frame(audio_format_ctx, packet);
                if (read_res >= 0) {
                    did_work = true;
                    if (packet->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
                        if (avcodec_send_packet(audio_codec_ctx, packet) >= 0) {
                            while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                                double pts_time = 0.0;
                                if (frame->pts != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->pts) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                                } else if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                    pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                                } else {
                                    pts_time = std::max(0.0, position.load()) + (static_cast<double>(frame->nb_samples) / 44100.0);
                                }
                                if (first_audio_pts.load() < 0.0 && pts_time >= 0.0) {
                                    first_audio_pts.store(pts_time);
                                    playback_start_time = std::chrono::steady_clock::now();
                                    {
                                        std::lock_guard<std::mutex> lock(video_queue_mutex);
                                        if (!decoded_video_queue.empty()) {
                                            first_video_pts.store(decoded_video_queue.front().pts);
                                        }
                                    }
                                }
                                last_audio_pts.store(pts_time);

                                const uint8_t* input_data[8];
                                for (int i = 0; i < 8; ++i) input_data[i] = frame->data[i];
                                int out_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, input_data, frame->nb_samples);
                                if (out_samples > 0) {
                                    size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                                    std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);

                                    if (!local_audio_stream) {
                                        SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 44100};
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
                } else if (read_res < 0 && read_res != AVERROR(EAGAIN)) {
                    audio_eof = true;
                    if (audio_codec_ctx && swr_ctx) {
                        avcodec_send_packet(audio_codec_ctx, nullptr);
                        while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                            double pts_time = 0.0;
                            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(audio_format_ctx->streams[audio_stream_idx]->time_base);
                            } else {
                                pts_time = position.load() + (static_cast<double>(frame->nb_samples) / 44100.0);
                            }
                            last_audio_pts.store(pts_time);

                            const uint8_t* input_data[8];
                            for (int i = 0; i < 8; ++i) input_data[i] = frame->data[i];
                            int out_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, input_data, frame->nb_samples);
                            if (out_samples > 0) {
                                size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                                std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                                if (!local_audio_stream) {
                                    SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 44100};
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
                                }
                            }
                        }
                        int flush_samples = swr_convert(swr_ctx, &audio_out_buf, max_audio_out_samples, nullptr, 0);
                        if (flush_samples > 0) {
                            size_t pcm_bytes = static_cast<size_t>(flush_samples) * 2 * sizeof(int16_t);
                            std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);
                            if (local_audio_stream) {
                                SDL_PutAudioStreamData(local_audio_stream, pcm_chunk.data(), static_cast<int>(pcm_chunk.size()));
                            }
                        }
                    }
                }
            }

            if (video_eof && audio_eof) {
                drain_and_finish();
                break;
            }

            if (!did_work) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
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
