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
    bool has_video{false};
    double start_offset{0.0};
    long long feed_id{-1};
    std::string item_link;
    std::string item_title;
    std::optional<double> watermark;
    std::optional<window_rect> last_docked_video_rect;
    bool user_tall_layout{false};
    bool user_tall_layout_set{false};
    static inline std::function<void(long long, const std::string&, const std::string&, double)> save_watermark_cb;
    static inline std::function<void(const uint8_t*, size_t)> push_audio_cb;
    static inline std::function<void()> reset_sync_cb;

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
    void decode_loop(std::string media_target, double start_offset);

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
        if (new_frame_ready.load()) {
            std::vector<uint8_t> local_pixels;
            {
                std::lock_guard<std::mutex> clock(frame_mutex);
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

inline void media_player_item::stopMedia() {
    double cur_pos = position.load();
    if (feed_id != -1 && !item_link.empty() && cur_pos > 0.0) {
        double cur_dur = duration.load();
        if (cur_dur > 0.0 && cur_pos >= cur_dur - 2.0) {
            watermark = 0.0;
            if (save_watermark_cb) {
                save_watermark_cb(feed_id, item_link, item_title, 0.0);
            }
        } else {
            watermark = cur_pos;
            if (save_watermark_cb) {
                save_watermark_cb(feed_id, item_link, item_title, cur_pos);
            }
        }
    }

    ffmpeg_running.store(false);
    vu_level_l.store(0.0f);
    vu_level_r.store(0.0f);
    if (reset_sync_cb) {
        reset_sync_cb();
    }

    if (ffmpeg_thread.joinable()) {
        ffmpeg_thread.join();
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
    std::string media_target = sanitized_url;
    if (sanitized_url.find("youtube.com") != std::string::npos ||
        sanitized_url.find("youtu.be") != std::string::npos) {
        std::string ytdl_cmd = std::format("yt-dlp -g -f \"best[ext=mp4]/best\" \"{}\" 2>/dev/null", sanitized_url);
        std::string resolved = ProcessHelper::executeCommand(ytdl_cmd);
        if (!resolved.empty()) {
            auto newline_pos = resolved.find('\n');
            if (newline_pos != std::string::npos) {
                resolved = resolved.substr(0, newline_pos);
            }
            resolved.erase(resolved.find_last_not_of(" \r\n\t") + 1);
            if (!resolved.empty()) {
                media_target = resolved;
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

    ffmpeg_thread = std::thread([this, media_target, offset]() {
        decode_loop(media_target, offset);
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

inline void media_player_item::decode_loop(std::string media_target, double offset) {
    avformat_network_init();

    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, media_target.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[NativePlayer] Failed to open input: " << media_target << std::endl;
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "[NativePlayer] Failed to find stream info" << std::endl;
        avformat_close_input(&format_ctx);
        is_playing = false;
        ffmpeg_running.store(false);
        player_pid = 0;
        return;
    }

    int video_stream_idx = -1;
    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx == -1) {
            video_stream_idx = static_cast<int>(i);
        } else if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx == -1) {
            audio_stream_idx = static_cast<int>(i);
        }
    }

    AVCodecContext* video_codec_ctx = nullptr;
    AVCodecContext* audio_codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;

    if (video_stream_idx >= 0) {
        const AVCodec* video_codec = avcodec_find_decoder(format_ctx->streams[video_stream_idx]->codecpar->codec_id);
        if (video_codec) {
            video_codec_ctx = avcodec_alloc_context3(video_codec);
            avcodec_parameters_to_context(video_codec_ctx, format_ctx->streams[video_stream_idx]->codecpar);
            video_codec_ctx->thread_count = 4;
            if (avcodec_open2(video_codec_ctx, video_codec, nullptr) >= 0) {
                sws_ctx = sws_getContext(
                    video_codec_ctx->width, video_codec_ctx->height, video_codec_ctx->pix_fmt,
                    kWidth, kHeight, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
            }
        }
    }

    if (audio_stream_idx >= 0) {
        const AVCodec* audio_codec = avcodec_find_decoder(format_ctx->streams[audio_stream_idx]->codecpar->codec_id);
        if (audio_codec) {
            audio_codec_ctx = avcodec_alloc_context3(audio_codec);
            avcodec_parameters_to_context(audio_codec_ctx, format_ctx->streams[audio_stream_idx]->codecpar);
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
    if (format_ctx->duration != AV_NOPTS_VALUE) {
        total_duration = static_cast<double>(format_ctx->duration) / AV_TIME_BASE;
        duration.store(total_duration);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgba_frame = av_frame_alloc();

    int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, kWidth, kHeight, 1);
    uint8_t* rgba_buffer = static_cast<uint8_t*>(av_malloc(static_cast<size_t>(size)));
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buffer,
                         AV_PIX_FMT_RGBA, kWidth, kHeight, 1);

    // Initial seek if requested
    if (offset > 0.05) {
        int seek_stream = video_stream_idx >= 0 ? video_stream_idx : audio_stream_idx;
        if (seek_stream >= 0) {
            int64_t target_pts = av_rescale_q(static_cast<int64_t>(offset * AV_TIME_BASE), AV_TIME_BASE_Q, format_ctx->streams[seek_stream]->time_base);
            av_seek_frame(format_ctx, seek_stream, target_pts, AVSEEK_FLAG_BACKWARD);
            if (video_codec_ctx) avcodec_flush_buffers(video_codec_ctx);
            if (audio_codec_ctx) avcodec_flush_buffers(audio_codec_ctx);
        }
    }

    auto start_time = std::chrono::steady_clock::now();
    double pts_offset = offset;
    position.store(offset);

    uint8_t* audio_out_buf = nullptr;
    int max_audio_out_samples = 4096;
    av_samples_alloc(&audio_out_buf, nullptr, 2, max_audio_out_samples, AV_SAMPLE_FMT_S16, 0);

    ffmpeg_running.store(true);
    is_playing = true;
    player_pid = 1;

    while (ffmpeg_running.load()) {
        // Dynamic seek support
        double target = seek_target.exchange(-1.0);
        if (target >= 0.0) {
            int seek_stream = video_stream_idx >= 0 ? video_stream_idx : audio_stream_idx;
            if (seek_stream >= 0) {
                int64_t target_pts = av_rescale_q(static_cast<int64_t>(target * AV_TIME_BASE), AV_TIME_BASE_Q, format_ctx->streams[seek_stream]->time_base);
                av_seek_frame(format_ctx, seek_stream, target_pts, AVSEEK_FLAG_BACKWARD);
                if (video_codec_ctx) avcodec_flush_buffers(video_codec_ctx);
                if (audio_codec_ctx) avcodec_flush_buffers(audio_codec_ctx);
                
                start_time = std::chrono::steady_clock::now();
                pts_offset = target;
                position.store(target);
            }
        }

        // Pause state handling
        if (is_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            start_time += std::chrono::milliseconds(10);
            continue;
        }

        int read_res = av_read_frame(format_ctx, packet);
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
                        pts_time = static_cast<double>(frame->best_effort_timestamp) * av_q2d(format_ctx->streams[video_stream_idx]->time_base);
                    } else {
                        pts_time = position.load() + (1.0 / kFps);
                    }

                    // Sync video presentation using steady_clock
                    double target_elapsed = pts_time - pts_offset;
                    if (target_elapsed > 0.0) {
                        auto now = std::chrono::steady_clock::now();
                        double actual_elapsed = std::chrono::duration<double>(now - start_time).count();
                        double sleep_dur = target_elapsed - actual_elapsed;
                        if (sleep_dur > 0.0) {
                            if (sleep_dur > 5.0) {
                                start_time = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(target_elapsed));
                            } else {
                                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_dur));
                            }
                        }
                    }

                    // Perform scaling and format conversion directly to RGBA
                    sws_scale(
                        sws_ctx, frame->data, frame->linesize, 0, video_codec_ctx->height,
                        rgba_frame->data, rgba_frame->linesize
                    );

                    {
                        std::lock_guard<std::mutex> lock(frame_mutex);
                        back_pixels.assign(rgba_frame->data[0], rgba_frame->data[0] + kWidth * kHeight * 4);
                        new_frame_ready.store(true);
                        has_video = true;
                    }

                    position.store(pts_time);
                }
            }
        } else if (packet->stream_index == audio_stream_idx && audio_codec_ctx && swr_ctx) {
            if (avcodec_send_packet(audio_codec_ctx, packet) >= 0) {
                while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
                    const uint8_t* input_data[8];
                    for (int i = 0; i < 8; ++i) {
                        input_data[i] = frame->data[i];
                    }
                    
                    int out_samples = swr_convert(
                        swr_ctx, &audio_out_buf, max_audio_out_samples,
                        input_data, frame->nb_samples
                    );

                    if (out_samples > 0) {
                        size_t pcm_bytes = static_cast<size_t>(out_samples) * 2 * sizeof(int16_t);
                        std::vector<uint8_t> pcm_chunk(audio_out_buf, audio_out_buf + pcm_bytes);

                        int vol = volume.load();
                        if (vol < 100 && !pcm_chunk.empty()) {
                            int16_t* samples = reinterpret_cast<int16_t*>(pcm_chunk.data());
                            size_t sample_count = pcm_chunk.size() / sizeof(int16_t);
                            float scale = static_cast<float>(vol) / 100.0f;
                            for (size_t i = 0; i < sample_count; ++i) {
                                samples[i] = static_cast<int16_t>(
                                    std::clamp(static_cast<float>(samples[i]) * scale, -32768.0f, 32767.0f)
                                );
                            }
                        }

                        if (!pcm_chunk.empty()) {
                            const int16_t* samples = reinterpret_cast<const int16_t*>(pcm_chunk.data());
                            size_t f_count = pcm_chunk.size() / (2 * sizeof(int16_t));
                            int32_t max_l = 0;
                            int32_t max_r = 0;
                            for (size_t i = 0; i < f_count; ++i) {
                                int32_t l = std::abs(static_cast<int32_t>(samples[i * 2]));
                                int32_t r = std::abs(static_cast<int32_t>(samples[i * 2 + 1]));
                                if (l > max_l) max_l = l;
                                if (r > max_r) max_r = r;
                            }
                            float peak_l = static_cast<float>(max_l) / 32768.0f;
                            float peak_r = static_cast<float>(max_r) / 32768.0f;

                            float old_l = vu_level_l.load();
                            float old_r = vu_level_r.load();
                            vu_level_l.store(std::max(peak_l, old_l * 0.85f));
                            vu_level_r.store(std::max(peak_r, old_r * 0.85f));
                        }

                        if (push_audio_cb) {
                            push_audio_cb(pcm_chunk.data(), pcm_chunk.size());
                        }
                    }
                }
            }
        }

        av_packet_unref(packet);
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
    avformat_close_input(&format_ctx);

    is_playing = false;
    ffmpeg_running.store(false);
    player_pid = 0;
}
