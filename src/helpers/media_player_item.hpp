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
struct media_player_item : public std::enable_shared_from_this<media_player_item> {
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
    std::atomic<bool> has_audio{false};
    std::atomic<float> current_luminance{0.0f};
    std::atomic<float> current_audio_peak_l{0.0f};
    std::atomic<float> current_audio_peak_r{0.0f};
    std::atomic<bool> has_presented_first_frame{false};
    std::atomic<bool> is_adlib_item{false};
    std::vector<uint8_t> get_current_adlib_frame_pixels() const;
    std::atomic<double> initial_pts_offset{0.0};
    std::atomic<double> first_video_pts{-1.0};
    std::atomic<double> first_audio_pts{-1.0};
    std::atomic<int> audio_sample_rate{44100};
    std::atomic<double> start_offset{0.0};
    mutable std::chrono::steady_clock::time_point playback_start_time{};
    mutable std::chrono::steady_clock::time_point baseline_tp{};
    mutable std::atomic<bool> baseline_set{false};
    mutable std::atomic<double> baseline_start_pts{-1.0};
    mutable std::chrono::steady_clock::time_point pause_start_tp{};
    mutable std::chrono::steady_clock::time_point last_video_present_time{};
    mutable double current_frame_duration{1.0 / 30.0};
    mutable std::atomic<double> last_presented_pts{-1.0};
    mutable std::atomic<double> last_av_sync_delta_ms{0.0};  // (video_pts - audio_pts) in ms at last frame present
    mutable std::atomic<int64_t> frames_presented{0};
    mutable std::atomic<int64_t> frames_dropped{0};
    mutable std::atomic<int64_t> frames_held{0};
    mutable std::atomic<int64_t> gpu_frames_rendered{0};
    mutable std::atomic<float> actual_rendering_fps{0.0f};
    mutable std::chrono::steady_clock::time_point last_gpu_upload_time{};
    std::atomic<bool> audio_clock_initialized{false};
    std::atomic<double> audio_pts_at_callback{0.0};
    mutable std::chrono::steady_clock::time_point audio_callback_time{};
    mutable std::chrono::steady_clock::time_point last_bg_pop_tp{};
    mutable std::mutex seek_mutex;

    double get_speaker_audio_pts() const;
    long long feed_id{-1};
    std::string item_link;
    std::string item_title;
    std::string rss_image_url;
    RouenGPUTexture* rss_image_texture{nullptr};
    int rss_image_width{0};
    int rss_image_height{0};
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
    std::function<void(const uint8_t* pcm_s16_data, size_t size_in_bytes)> on_audio_pcm_cb;

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
    
    ~media_player_item();

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
    double get_current_position() const;
    bool setPaused(bool paused);
    void update_watermark();
    void update_vu_levels();
    float get_vu_level_l();
    float get_vu_level_r();
    float get_vu_watermark_l();
    float get_vu_watermark_r();
    static int decode_interrupt_cb(void* ctx);

    void decode_loop(std::string video_target, std::string audio_target, double start_offset);

    ImTextureID get_texture_id(SDL_GPUDevice* device = nullptr, SDL_GPUCommandBuffer* existing_cmdbuf = nullptr);
};

using media_player_item_map = std::unordered_map<ImGuiID, std::shared_ptr<media_player_item>>;
