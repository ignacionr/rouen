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
    std::atomic<double> position{0.0};
    std::atomic<double> duration{0.0};
    std::atomic<int> volume{100};
    std::atomic<float> vu_level_l{0.0f};
    std::atomic<float> vu_level_r{0.0f};
    std::atomic<float> vu_watermark_l{0.0f};
    std::atomic<float> vu_watermark_r{0.0f};
    struct audio_peak_sample {
        double pts{0.0};
        float peak_l{0.0f};
        float peak_r{0.0f};
    };
    std::mutex audio_peak_mutex;
    std::deque<audio_peak_sample> audio_peak_queue;
    std::atomic<float> video_aspect_ratio{16.0f / 9.0f};
    bool has_video{false};
    double start_offset{0.0};
    long long feed_id{-1};
    std::string item_link;
    std::string item_title;
    std::optional<double> watermark;
    std::optional<window_rect> last_docked_video_rect;
    bool user_tall_layout{false};
    bool user_tall_layout_set{false};
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
    bool playMedia();
    std::string formatTime(double seconds) const;
    bool seekTo(double position_seconds);
    bool setVolume(int new_volume);
    bool pauseMedia();
    bool resumeMedia();
    bool togglePause();
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

    ImTextureID get_texture_id(SDL_GPUDevice* device = nullptr) {
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
                double current_audio_time = 0.0;
                if (local_audio_stream) {
                    double queued_seconds = static_cast<double>(SDL_GetAudioStreamQueued(local_audio_stream)) / 176400.0;
                    current_audio_time = last_audio_pts.load() - queued_seconds;
                } else {
                    current_audio_time = position.load();
                }

                std::lock_guard<std::mutex> q_lock(video_queue_mutex);
                while (!decoded_video_queue.empty()) {
                    const auto& front = decoded_video_queue.front();
                    if (front.pts > current_audio_time + 0.030) {
                        break;
                    }
                    if (decoded_video_queue.size() > 1) {
                        const auto& next_frame = decoded_video_queue[1];
                        if (next_frame.pts <= current_audio_time + 0.030) {
                            decoded_video_queue.pop_front();
                            continue;
                        }
                    }
                    frame_to_present = std::move(decoded_video_queue.front().pixels);
                    decoded_video_queue.pop_front();
                    break;
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
                Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, upload_buffer, false));
                if (map) {
                    std::memcpy(map, local_pixels.data(), kWidth * kHeight * 4);
                    SDL_UnmapGPUTransferBuffer(device, upload_buffer);
                    SDL_GPUCommandBuffer* cmd_buf = SDL_AcquireGPUCommandBuffer(device);
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
                        SDL_SubmitGPUCommandBuffer(cmd_buf);
                    }
                    has_video = true;
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
    double cur_pos = position.load();
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
    has_video = false;
    position = 0.0;
    duration = 0.0;
    start_offset = 0.0;
    player_pid = 0;
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

inline bool media_player_item::playMedia() {
    double offset = start_offset;
    stopMedia();
    start_offset = offset;
    has_video = false;
    is_paused = false;

    std::string sanitized_url = sanitizeURL(url);
    std::string video_target = sanitized_url;
    std::string audio_target;

    if (sanitized_url.find("youtube.com") != std::string::npos ||
        sanitized_url.find("youtu.be") != std::string::npos) {
        std::string ytdl_cmd = std::format("yt-dlp -g -f \"bestvideo+bestaudio/best\" \"{}\" 2>/dev/null", sanitized_url);
        std::string resolved = ProcessHelper::executeCommand(ytdl_cmd);
        if (!resolved.empty()) {
            std::stringstream ss(resolved);
            std::string line;
            std::vector<std::string> urls;
            while (std::getline(ss, line)) {
                line.erase(line.find_last_not_of(" \r\n\t") + 1);
                if (line.starts_with("http://") || line.starts_with("https://")) {
                    urls.push_back(line);
                }
            }
            if (urls.size() >= 2) {
                video_target = urls[0];
                audio_target = urls[1];
            } else if (!urls.empty()) {
                video_target = urls[0];
                audio_target = urls[0];
            }
        }
    }

    ffmpeg_running.store(true);
    is_playing = true;
    position.store(start_offset);
    player_pid = 1;
    if (reset_sync_cb) {
        reset_sync_cb();
    }

    ffmpeg_thread = std::thread([this, video_target, audio_target, offset]() {
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
    if (!ffmpeg_running.load()) {
        start_offset = position_seconds;
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
    is_paused = paused;
    if (paused) {
        update_watermark();
    }
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
    if (!is_playing || is_paused.load()) {
        float curr_l = vu_level_l.load();
        float curr_r = vu_level_r.load();
        vu_level_l.store(std::max(0.0f, curr_l * 0.80f));
        vu_level_r.store(std::max(0.0f, curr_r * 0.80f));

        float wm_l = vu_watermark_l.load();
        float wm_r = vu_watermark_r.load();
        vu_watermark_l.store(std::max(0.0f, wm_l - 0.0025f));
        vu_watermark_r.store(std::max(0.0f, wm_r - 0.0025f));
        return;
    }

    double current_audio_time = 0.0;
    if (local_audio_stream && !is_cast_active.load()) {
        int queued_bytes = SDL_GetAudioStreamQueued(local_audio_stream);
        double queued_seconds = static_cast<double>(queued_bytes) / 176400.0;
        current_audio_time = last_audio_pts.load() - queued_seconds;
    } else {
        current_audio_time = position.load();
    }

    float target_l = 0.0f;
    float target_r = 0.0f;

    {
        std::lock_guard<std::mutex> p_lock(audio_peak_mutex);
        while (!audio_peak_queue.empty() && audio_peak_queue.front().pts < current_audio_time - 0.05) {
            audio_peak_queue.pop_front();
        }

        if (!audio_peak_queue.empty()) {
            const auto& front = audio_peak_queue.front();
            if (front.pts <= current_audio_time + 0.08) {
                target_l = front.peak_l;
                target_r = front.peak_r;
            }
        }
    }

    float curr_l = vu_level_l.load();
    float curr_r = vu_level_r.load();
    float wm_l = vu_watermark_l.load();
    float wm_r = vu_watermark_r.load();

    constexpr float kWatermarkDropStep = 0.0025f; // 0.25 per frame on 0..100 scale

    // Left channel level & watermark update
    if (target_l >= curr_l) {
        curr_l = target_l;
    } else {
        curr_l = curr_l * 0.82f + target_l * 0.18f;
    }

    if (target_l >= wm_l) {
        wm_l = target_l;
    } else {
        wm_l = std::max(target_l, wm_l - kWatermarkDropStep);
    }

    // Right channel level & watermark update
    if (target_r >= curr_r) {
        curr_r = target_r;
    } else {
        curr_r = curr_r * 0.82f + target_r * 0.18f;
    }

    if (target_r >= wm_r) {
        wm_r = target_r;
    } else {
        wm_r = std::max(target_r, wm_r - kWatermarkDropStep);
    }

    if (curr_l < 0.001f) curr_l = 0.0f;
    if (curr_r < 0.001f) curr_r = 0.0f;
    if (wm_l < 0.001f) wm_l = 0.0f;
    if (wm_r < 0.001f) wm_r = 0.0f;

    vu_level_l.store(curr_l);
    vu_level_r.store(curr_r);
    vu_watermark_l.store(wm_l);
    vu_watermark_r.store(wm_r);
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

inline void media_player_item::decode_loop(std::string video_target, std::string audio_target, double offset) {
    avformat_network_init();

    AVFormatContext* video_format_ctx = avformat_alloc_context();
    if (!video_format_ctx) {
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }
    video_format_ctx->interrupt_callback.callback = decode_interrupt_cb;
    video_format_ctx->interrupt_callback.opaque = this;

    if (avformat_open_input(&video_format_ctx, video_target.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[NativePlayer] Failed to open input: " << video_target << std::endl;
        avformat_free_context(video_format_ctx);
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }

    if (avformat_find_stream_info(video_format_ctx, nullptr) < 0) {
        std::cerr << "[NativePlayer] Failed to find stream info" << std::endl;
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
            if (avformat_open_input(&audio_format_ctx, audio_target.c_str(), nullptr, nullptr) < 0) {
                avformat_free_context(audio_format_ctx);
                audio_format_ctx = nullptr;
            } else {
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

    if (video_stream_idx >= 0) {
        const AVCodec* video_codec = avcodec_find_decoder(video_format_ctx->streams[video_stream_idx]->codecpar->codec_id);
        if (video_codec) {
            video_codec_ctx = avcodec_alloc_context3(video_codec);
            avcodec_parameters_to_context(video_codec_ctx, video_format_ctx->streams[video_stream_idx]->codecpar);
            video_codec_ctx->thread_count = 4;
            if (avcodec_open2(video_codec_ctx, video_codec, nullptr) >= 0) {
                double aspect_ratio = static_cast<double>(video_codec_ctx->width) / static_cast<double>(video_codec_ctx->height);
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
                    video_codec_ctx->width, video_codec_ctx->height, video_codec_ctx->pix_fmt,
                    dst_w, dst_h, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
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

    while (ffmpeg_running.load()) {
        double target = seek_target.exchange(-1.0);
        if (target >= 0.0) {
            if (video_stream_idx >= 0 && video_format_ctx) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, video_format_ctx->streams[video_stream_idx]->time_base);
                av_seek_frame(video_format_ctx, video_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);
                if (video_codec_ctx) avcodec_flush_buffers(video_codec_ctx);
            }
            if (audio_stream_idx >= 0 && audio_format_ctx) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, audio_format_ctx->streams[audio_stream_idx]->time_base);
                av_seek_frame(audio_format_ctx, audio_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);
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
            if (queued > 44100 * 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        if (!is_dual_input) {
            int read_res = av_read_frame(video_format_ctx, packet);
            if (read_res < 0) {
                if (read_res == AVERROR_EOF) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            if (packet->stream_index == video_stream_idx && video_codec_ctx && sws_ctx) {
                if (avcodec_send_packet(video_codec_ctx, packet) >= 0) {
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
                            if (!is_cast_active.load() && decoded_video_queue.size() > 15) {
                                decoded_video_queue.pop_front();
                            }
                        }
                        has_video = true;
                        position.store(pts_time);
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
                        last_audio_pts.store(pts_time);
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
                                    audio_peak_queue.push_back({pts_time, peak_l, peak_r});
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

            if (video_stream_idx >= 0 && current_q_size < 20) {
                int read_res = av_read_frame(video_format_ctx, packet);
                if (read_res >= 0) {
                    did_work = true;
                    if (packet->stream_index == video_stream_idx && video_codec_ctx && sws_ctx) {
                        if (avcodec_send_packet(video_codec_ctx, packet) >= 0) {
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
                                has_video = true;
                                position.store(pts_time);
                            }
                        }
                    }
                    av_packet_unref(packet);
                }
            }

            int queued_audio = local_audio_stream ? SDL_GetAudioStreamQueued(local_audio_stream) : 0;
            if (audio_stream_idx >= 0 && queued_audio < 44100 * 2) {
                int read_res = av_read_frame(audio_format_ctx, packet);
                if (read_res >= 0) {
                    did_work = true;
                    if (packet->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
                        if (avcodec_send_packet(audio_codec_ctx, packet) >= 0) {
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
                                            audio_peak_queue.push_back({pts_time, peak_l, peak_r});
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
                }
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
