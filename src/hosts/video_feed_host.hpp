#pragma once

#include "../helpers/sdl_compat.hpp"
#include "../helpers/config_service.hpp"
#include "../helpers/stream_encoder.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <ctime>
#include <deque>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include <SDL3/SDL.h>
#include "../helpers/imgui_include.hpp"
#include "../helpers/debug.hpp"
#include "../helpers/texture_utils.hpp"
#include "../helpers/media_player.hpp"
#include "../cards/interface/card.hpp"
#include "../registrar.hpp"

namespace rouen::hosts {

/**
 * VideoFeedHost — streams a high-definition 1080p video feed through ffmpeg.
 *
 * ImGui Offscreen Context Architecture:
 *   Maintains a secondary ImGuiContext (video_imgui_ctx_) that renders onto an
 *   offscreen target texture (1920x1080).
 *
 * Multithreaded Frame Streaming:
 *   Offscreen ImGui rendering runs safely on the main thread.
 *   Rendered frames are pushed to a double-buffered queue and streamed to ffmpeg
 *   via a background worker thread.
 *
 * Stream Endpoint:  http://127.0.0.1:8889
 *   Connect with:  ./scripts/play_videofeed.sh
 */
class VideoFeedHost {
public:
    static constexpr int kWidth  = 1920;
    static constexpr int kHeight = 1080;
    static constexpr int kFps    = 30;
    static constexpr int kDefaultPort = 8889;

    std::atomic<bool> show_header{true};
    std::atomic<bool> show_footer{true};
    std::atomic<bool> show_bg_animation{true};
    std::atomic<bool> show_card_overlays{true};
    std::atomic<bool> enable_pink_noise{true};
    std::atomic<bool> full_screen_media{false};
 
    VideoFeedHost()
        : port_(kDefaultPort) {
        try {
            auto config = rouen::helpers::ConfigService::instance();
            show_header.store(config->get_typed<bool>("CAST_SHOW_HEADER").value_or(true));
            show_footer.store(config->get_typed<bool>("CAST_SHOW_FOOTER").value_or(true));
            show_bg_animation.store(config->get_typed<bool>("CAST_SHOW_BG_ANIMATION").value_or(true));
            show_card_overlays.store(config->get_typed<bool>("CAST_SHOW_CARD_OVERLAYS").value_or(true));
            enable_pink_noise.store(config->get_typed<bool>("CAST_ENABLE_PINK_NOISE").value_or(true));
            full_screen_media.store(config->get_typed<bool>("CAST_FULL_SCREEN_MEDIA").value_or(false));
        } catch (...) {}
        VIDEOFEED_INFO("VideoFeedHost: Initialized");
    }

    ~VideoFeedHost() {
        stop();
        cleanup_imgui_context();
    }

    // ── singleton accessor ──────────────────────────────────────────
    static std::shared_ptr<VideoFeedHost> get_host() {
        static std::mutex host_mutex;
        static std::shared_ptr<VideoFeedHost> instance = nullptr;
        std::lock_guard<std::mutex> lock(host_mutex);
        if (!instance) {
            instance = std::make_shared<VideoFeedHost>();
            try {
                registrar::add("video_feed_host", instance);
            } catch (...) {}
            media_player_item::push_audio_cb = [](const uint8_t* data, size_t size) {
                auto h = get_host();
                if (h) h->push_audio_pcm(data, size);
            };
            media_player_item::reset_sync_cb = []() {
                auto h = get_host();
                if (h) h->reset_sync_queues();
            };
            media_player_item::get_cast_queue_size_cb = []() -> size_t {
                auto h = get_host();
                return h ? h->get_cast_queued_bytes() : 0;
            };
            media_player_item::is_offscreen_ctx_cb = []() -> bool {
                auto h = get_host();
                return h ? (ImGui::GetCurrentContext() == h->video_imgui_ctx_) : false;
            };
        }
        return instance;
    }

    // ── public API ──────────────────────────────────────────────────

    bool is_running() const { return running_.load(); }
    bool is_starting() const { return starting_.load(); }

    int port() const { return port_.load(); }

    std::string endpoint() const {
        return std::format("tcp://127.0.0.1:{}", port_.load());
    }

    void set_port(int p) {
        if (!running_.load()) {
            port_.store(p);
        }
    }

    /// Push raw stereo 16-bit 44.1kHz PCM audio into the video feed stream
    void push_audio_pcm(const uint8_t* data, size_t size) {
        if (!running_.load() || !data || size == 0) return;
        std::lock_guard<std::mutex> lock(audio_mutex_);
        
        audio_queue_.insert(audio_queue_.end(), data, data + size);

        // Cap audio queue to ~5 seconds max to prevent memory leakage
        constexpr size_t kMaxAudioQueueBytes = 882000; 
        if (audio_queue_.size() > kMaxAudioQueueBytes) {
            auto drop_count = static_cast<std::ptrdiff_t>(audio_queue_.size() - kMaxAudioQueueBytes);
            audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + drop_count);
        }
    }

    size_t get_audio_queue_size() {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        return audio_queue_.size();
    }

    size_t get_cast_queued_bytes() {
        size_t q = 0;
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            q = audio_queue_.size();
        }
        return q + streamer_.get_audio_buf_size();
    }

    void reset_sync_queues() {
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_queue_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            video_frame_queue_.clear();
            new_frame_ready_ = false;
        }
        prebuffer_established_.store(false);
        audio_prebuffer_established_.store(false);
        first_media_video_pts_time.store(-1.0);
        audio_aligned_to_video.store(false);
        streamer_.reset_audio_buffers();
        VIDEOFEED_INFO("VideoFeedHost: Reset sync queues");
    }

    /// Return an ImTextureID usable directly inside Rouen's main desktop ImGui windows
    ImTextureID get_texture_id(SDL_GPUDevice* /*device*/ = nullptr) {
        std::lock_guard<std::mutex> lock(video_mutex_);
        if (offscreen_texture_) {
            return rouen::helpers::texture_id_cast(offscreen_texture_);
        }
        return ImTextureID{};
    }

    bool start() {
        if (running_.load() || starting_.load()) {
            VIDEOFEED_WARN("VideoFeedHost: Already running or starting");
            return false;
        }

        starting_.store(true);
        const int listen_port = port_.load();

        std::thread([this, listen_port]() {
            std::string tcp_listen_url = std::format("tcp://0.0.0.0:{}?listen=1", listen_port);
            VIDEOFEED_INFO_FMT("VideoFeedHost: Listening for TCP connection on {}", tcp_listen_url);
            
            if (!streamer_.init(tcp_listen_url, kWidth, kHeight, kFps)) {
                VIDEOFEED_ERROR("VideoFeedHost: Failed to initialize NativeStreamEncoder on TCP");
                starting_.store(false);
                try { "notify"_sfn("Video Feed error: Failed to initialize TCP stream"); } catch (...) {}
                return;
            }

            VIDEOFEED_INFO("VideoFeedHost: TCP Client connected! Starting loops.");
            media_player_item::is_cast_active.store(true);
            running_.store(true);
            starting_.store(false);
            frame_number_ = 0;
            new_frame_ready_ = false;
            stream_start_time_ = std::chrono::steady_clock::now();
            last_frame_time_ = stream_start_time_;

            video_writer_thread_ = std::thread(&VideoFeedHost::video_writer_loop, this);
            audio_writer_thread_ = std::thread(&VideoFeedHost::audio_writer_loop, this);

            try {
                "notify"_sfn(std::format("Video feed active at {}", endpoint()));
            } catch (...) {}
        }).detach();

        return true;
    }

    bool has_active_media_player_video() {
        try {
            std::lock_guard<std::recursive_mutex> map_lock(media_player::items_mutex());
            for (auto& [id, item_ptr] : media_player::items()) {
                if (item_ptr && item_ptr->is_playing && item_ptr->has_video) {
                    return true;
                }
            }
        } catch (...) {}
        return false;
    }

    /// Perform an offscreen ImGui render pass on the main thread and queue the frame.
    void render_video_frame(SDL_GPUDevice* device) {
        if (!running_.load() || !device) return;

        // Rate limit to 24 fps (41ms interval)
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time_).count();
        if (elapsed_ms < (1000 / kFps)) {
            return;
        }
        last_frame_time_ = now;

        std::lock_guard<std::mutex> lock(video_mutex_);

        // Save original context
        ImGuiContext* orig_ctx = ImGui::GetCurrentContext();
        if (!orig_ctx) return;

        // Lazy-initialize offscreen target texture
        if (!offscreen_texture_) {
            SDL_GPUTextureCreateInfo texture_info = {};
            texture_info.type = SDL_GPU_TEXTURETYPE_2D;
            texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            texture_info.width = kWidth;
            texture_info.height = kHeight;
            texture_info.layer_count_or_depth = 1;
            texture_info.num_levels = 1;
            SDL_GPUTexture* raw_texture = SDL_CreateGPUTexture(device, &texture_info);
            if (raw_texture) {
                offscreen_texture_ = new RouenGPUTexture();
                offscreen_texture_->binding.texture = raw_texture;
                offscreen_texture_->binding.sampler = TextureHelper::getDefaultSampler(device);
                offscreen_texture_->width = kWidth;
                offscreen_texture_->height = kHeight;
            } else {
                VIDEOFEED_ERROR_FMT("Failed to create offscreen_texture_: {}", SDL_GetError());
                return;
            }
        }

        // Lazy-initialize secondary offscreen ImGui context sharing main font atlas
        if (!video_imgui_ctx_) {
            ImFontAtlas* shared_fonts = orig_ctx->IO.Fonts;
            video_imgui_ctx_ = ImGui::CreateContext(shared_fonts);
        }

        // Copy backend data pointers so SDL3 GPU backend works in secondary context
        video_imgui_ctx_->IO.BackendRendererUserData = orig_ctx->IO.BackendRendererUserData;
        video_imgui_ctx_->IO.BackendPlatformUserData = orig_ctx->IO.BackendPlatformUserData;

        // Switch to secondary ImGui context
        ImGui::SetCurrentContext(video_imgui_ctx_);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
        io.DeltaTime = 1.0f / static_cast<float>(kFps);

        // Ensure font texture ID matches main context's SDL texture
        if (orig_ctx->IO.Fonts && orig_ctx->IO.Fonts->TexID) {
            io.Fonts->TexID = orig_ctx->IO.Fonts->TexID;
        }

        // Start offscreen ImGui frame
        ImGui::NewFrame();

        // 1. Render Video Feed Header & Base UI
        render_video_base_ui();

        // 2. Render Active Cards' Video UI
        if (show_card_overlays.load()) {
            try {
                auto get_cards_fn = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
                if (get_cards_fn && *get_cards_fn) {
                    auto active_cards = (*get_cards_fn)();
                    for (const auto& card_ptr : active_cards) {
                        if (card_ptr) {
                            card_ptr->render_video_ui();
                        }
                    }
                }
            } catch (...) {}
        }

        // 3. Render Active Media Player Video Stream on Cast
        try {
            std::lock_guard<std::recursive_mutex> map_lock(media_player::items_mutex());
            for (auto& [id, item_ptr] : media_player::items()) {
                if (item_ptr && item_ptr->is_playing && item_ptr->has_video) {
                    ImTextureID media_tex = item_ptr->get_texture_id(device);
                    if (media_tex) {
                        if (full_screen_media.load()) {
                            ImGui::GetBackgroundDrawList()->AddImage(
                                media_tex,
                                ImVec2(0.0f, 0.0f),
                                ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight))
                            );
                        } else if (show_card_overlays.load()) {
                            ImGui::SetNextWindowPos(ImVec2(100, 160));
                            ImGui::SetNextWindowSize(ImVec2(1280, 720));
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
                            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.95f));

                            std::string win_title = std::format("Media Stream: {}##CastMediaWin", item_ptr->item_title.empty() ? "Video" : item_ptr->item_title);
                            if (ImGui::Begin(win_title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
                                ImGui::Image(media_tex, ImVec2(1250, 650));
                            }
                            ImGui::End();
                            ImGui::PopStyleColor();
                            ImGui::PopStyleVar();
                        }
                        break;
                    }
                }
            }
        } catch (...) {}

        // Render ImGui draw data
        ImGui::Render();

        SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        if (cmdbuf) {
            Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdbuf);
            SDL_GPUColorTargetInfo color_target = {};
            color_target.texture = offscreen_texture_->binding.texture;
            color_target.clear_color = SDL_FColor{ 15.0f/255.0f, 20.0f/255.0f, 30.0f/255.0f, 1.0f };
            color_target.load_op = SDL_GPU_LOADOP_CLEAR;
            color_target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, nullptr);
            if (render_pass) {
                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmdbuf, render_pass);
                SDL_EndGPURenderPass(render_pass);
            }

            if (!download_buffer_) {
                SDL_GPUTransferBufferCreateInfo transferInfo = {};
                transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                transferInfo.size = kWidth * kHeight * 4;
                download_buffer_ = SDL_CreateGPUTransferBuffer(device, &transferInfo);
            }

            if (download_buffer_) {
                SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdbuf);
                if (copyPass) {
                    SDL_GPUTextureRegion sourceRegion = {};
                    sourceRegion.texture = offscreen_texture_->binding.texture;
                    sourceRegion.w = kWidth;
                    sourceRegion.h = kHeight;
                    sourceRegion.d = 1;

                    SDL_GPUTextureTransferInfo destInfo = {};
                    destInfo.transfer_buffer = download_buffer_;
                    destInfo.offset = 0;
                    destInfo.pixels_per_row = kWidth;
                    destInfo.rows_per_layer = kHeight;

                    SDL_DownloadFromGPUTexture(copyPass, &sourceRegion, &destInfo);
                    SDL_EndGPUCopyPass(copyPass);
                }
            }

            SDL_SubmitGPUCommandBuffer(cmdbuf);

            if (download_buffer_) {
                void* map = SDL_MapGPUTransferBuffer(device, download_buffer_, false);
                if (map) {
                    if (render_buffer_.size() != static_cast<size_t>(kWidth * kHeight * 4)) {
                        render_buffer_.resize(static_cast<size_t>(kWidth * kHeight * 4));
                    }
                    std::memcpy(render_buffer_.data(), map, kWidth * kHeight * 4);
                    SDL_UnmapGPUTransferBuffer(device, download_buffer_);
                }
            }
        }

        // Restore original ImGui context
        ImGui::SetCurrentContext(orig_ctx);

        ++frame_number_;

        if (has_active_media_player_video()) {
            if (first_media_video_pts_time.load() < 0.0) {
                first_media_video_pts_time.store(streamer_.get_video_pts_seconds());
            }
        }

        // Push frame to pre-buffer queue
        {
            std::lock_guard<std::mutex> frame_lock(frame_mutex_);
            video_frame_queue_.push_back(render_buffer_);
            if (video_frame_queue_.size() > 75) {
                video_frame_queue_.pop_front();
            }
            new_frame_ready_ = true;
        }
        frame_cv_.notify_one();
    }

    void stop() {
        if (!running_.exchange(false) && !starting_.exchange(false)) {
            return;
        }

        VIDEOFEED_INFO("VideoFeedHost: Stopping...");
        media_player_item::is_cast_active.store(false);

        frame_cv_.notify_all();

        if (video_writer_thread_.joinable()) {
            video_writer_thread_.join();
        }
        if (audio_writer_thread_.joinable()) {
            audio_writer_thread_.join();
        }

        streamer_.close();

        try {
            "notify"_sfn("Video feed stopped");
        } catch (...) {}

        cleanup_imgui_context();
    }

    static void generate_pink_noise(int16_t* samples, size_t num_samples) {
        static float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        static uint32_t seed = 12345;
        for (size_t i = 0; i < num_samples; ++i) {
            seed = seed * 1664525u + 1013904223u;
            float white = (static_cast<float>(seed >> 16) / 32768.0f) - 1.0f;
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;
            pink *= 0.04f; // Soft ambient volume
            int32_t val = static_cast<int32_t>(pink * 32767.0f);
            samples[i] = static_cast<int16_t>(std::clamp<int32_t>(val, -32768, 32767));
        }
    }

private:
    void audio_writer_loop() {
        using clock = std::chrono::steady_clock;
        constexpr auto kAudioFrameDuration = std::chrono::microseconds(1000000 / kFps);
        constexpr size_t kAudioBytesPerFrame = (44100 * 2 * 2) / kFps; // 5880 bytes
        std::vector<uint8_t> audio_chunk(kAudioBytesPerFrame, 0);

        auto last_real_audio_time = clock::now();
        auto next_frame = clock::now();

        while (running_.load()) {
            next_frame += kAudioFrameDuration;

            bool has_active_media = false;
            try {
                std::lock_guard<std::recursive_mutex> map_lock(media_player::items_mutex());
                for (const auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && item_ptr->is_playing) {
                        has_active_media = true;
                        break;
                    }
                }
            } catch (...) {}

            auto now = clock::now();
            bool encode_silence = false;

            if (has_active_media) {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                if (audio_queue_.size() >= kAudioBytesPerFrame) {
                    audio_chunk.assign(audio_queue_.begin(), audio_queue_.begin() + kAudioBytesPerFrame);
                    audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + kAudioBytesPerFrame);
                    
                    if (!audio_aligned_to_video.load()) {
                        streamer_.sync_audio_pts_with_video_pts();
                        audio_aligned_to_video.store(true);
                    }

                    streamer_.encode_audio(audio_chunk.data(), audio_chunk.size());
                    last_real_audio_time = now;
                } else {
                    if (audio_aligned_to_video.load() && (now - last_real_audio_time >= std::chrono::milliseconds(150))) {
                        encode_silence = true;
                    }
                }
            } else {
                // No active media: clear queue and send silence
                {
                    std::lock_guard<std::mutex> lock(audio_mutex_);
                    audio_queue_.clear();
                }
                encode_silence = true;
            }

            if (encode_silence) {
                audio_chunk.resize(kAudioBytesPerFrame);
                int16_t* fill_ptr = reinterpret_cast<int16_t*>(audio_chunk.data());
                size_t fill_samples = kAudioBytesPerFrame / sizeof(int16_t);

                if (!has_active_media && enable_pink_noise.load()) {
                    generate_pink_noise(fill_ptr, fill_samples);
                } else {
                    std::fill_n(fill_ptr, fill_samples, 0);
                }
                streamer_.encode_audio(audio_chunk.data(), audio_chunk.size());
            }

            // Lock-step pacing sleep
            now = clock::now();
            if (next_frame > now) {
                std::this_thread::sleep_for(next_frame - now);
            } else {
                next_frame = now;
            }
        }
    }

    void video_writer_loop() {
        using clock = std::chrono::steady_clock;
        constexpr auto kVideoFrameDuration = std::chrono::microseconds(1000000 / kFps);
        std::vector<uint8_t> local_buffer;
        std::vector<uint8_t> last_frame(kWidth * kHeight * 4, 0);

        while (running_.load()) {
            auto frame_start = clock::now();

            {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                if (!video_frame_queue_.empty()) {
                    local_buffer = std::move(video_frame_queue_.front());
                    video_frame_queue_.pop_front();
                    last_frame = local_buffer;
                } else {
                    local_buffer = last_frame;
                }
            }

            if (!local_buffer.empty()) {
                streamer_.encode_video(local_buffer.data());
            }

            auto elapsed = clock::now() - frame_start;
            auto sleep_dur = kVideoFrameDuration - elapsed;
            if (sleep_dur > std::chrono::nanoseconds(0)) {
                std::this_thread::sleep_for(sleep_dur);
            }
        }
    }

    void render_video_base_ui() {
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

        bool fs_media_active = full_screen_media.load() && has_active_media_player_video();

        if (!fs_media_active) {
            if (show_bg_animation.load()) {
                // Animated hue sweep background
                const float hue = std::fmod(static_cast<float>(frame_number_) / 240.0f, 1.0f);
                ImVec4 bg_col;
                ImGui::ColorConvertHSVtoRGB(hue, 0.5f, 0.25f, bg_col.x, bg_col.y, bg_col.z);
                bg_col.w = 1.0f;
                draw_list->AddRectFilled(ImVec2(0, 0), ImVec2(kWidth, kHeight), ImGui::GetColorU32(bg_col));

                // Animated bouncing indicator square
                constexpr float sq = 120.0f;
                const float range_x = kWidth - sq;
                const float range_y = kHeight - sq;
                float raw_x = std::fmod(static_cast<float>(frame_number_ * 6), range_x * 2.0f);
                float raw_y = std::fmod(static_cast<float>(frame_number_ * 4), range_y * 2.0f);
                float sx = raw_x < range_x ? raw_x : range_x * 2.0f - raw_x;
                float sy = raw_y < range_y ? raw_y : range_y * 2.0f - raw_y;

                draw_list->AddRectFilled(
                    ImVec2(sx, sy), ImVec2(sx + sq, sy + sq),
                    IM_COL32(255, 255, 255, 40), 16.0f
                );
            } else {
                // Clean dark slate static background
                draw_list->AddRectFilled(ImVec2(0, 0), ImVec2(kWidth, kHeight), IM_COL32(15, 20, 30, 255));
            }
        }

        // Top ImGui Header Window
        if (show_header.load()) {
            ImGui::SetNextWindowPos(ImVec2(40, 30));
            ImGui::SetNextWindowSize(ImVec2(800, 110));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.12f, 0.20f, 0.85f));

            if (ImGui::Begin("##VideoHeader", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
                ImGui::SetWindowFontScale(2.2f);
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "ROUEN MULTI-MODAL UI");
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.6f, 1.0f), "30 FPS LIVE STREAM  |  FULL HD 1920x1080  |  STEREO AUDIO");
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // Bottom ImGui Footer Window
        if (show_footer.load()) {
            ImGui::SetNextWindowPos(ImVec2(40, kHeight - 110));
            ImGui::SetNextWindowSize(ImVec2(1100, 80));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.12f, 0.20f, 0.85f));

            if (ImGui::Begin("##VideoFooter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
                auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                std::tm tm_buf{};
#ifdef _WIN32
                localtime_s(&tm_buf, &now_t);
#else
                localtime_r(&now_t, &tm_buf);
#endif
                auto elapsed = std::chrono::steady_clock::now() - stream_start_time_;
                double elapsed_sec = std::chrono::duration<double>(elapsed).count();
                int elapsed_min = static_cast<int>(elapsed_sec) / 60;
                int elapsed_rem_sec = static_cast<int>(elapsed_sec) % 60;
                double avg_fps = elapsed_sec > 0.1 ? (static_cast<double>(frame_number_) / elapsed_sec) : 0.0;

                std::string status_str = std::format("FRAME: {:06d}   |   {:02d}:{:02d}:{:02d}   |   ELAPSED: {:02d}:{:02d}   |   FPS: {:.2f}",
                                                     frame_number_, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                                                     elapsed_min, elapsed_rem_sec, avg_fps);
                ImGui::SetWindowFontScale(1.6f);
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", status_str.c_str());
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
    }

    void cleanup_imgui_context() {
        std::lock_guard<std::mutex> lock(video_mutex_);
        if (video_imgui_ctx_) {
            ImGuiContext* orig_ctx = ImGui::GetCurrentContext();
            if (orig_ctx == video_imgui_ctx_) {
                ImGui::SetCurrentContext(nullptr);
            }
            video_imgui_ctx_->FontAtlasOwnedByContext = false;
            ImGui::DestroyContext(video_imgui_ctx_);
            video_imgui_ctx_ = nullptr;
        }
        if (offscreen_texture_ || download_buffer_) {
            SDL_GPUDevice* device = nullptr;
            try {
                auto device_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
                if (device_ptr && *device_ptr) device = *device_ptr;
            } catch (...) {}
            if (offscreen_texture_) {
                TextureHelper::destroyTexture(offscreen_texture_);
            }
            if (device && download_buffer_) {
                SDL_ReleaseGPUTransferBuffer(device, download_buffer_);
                download_buffer_ = nullptr;
            }
        }
    }

    // ── member data ─────────────────────────────────────────────────
    std::atomic<bool>     running_{false};
    std::atomic<bool>     starting_{false};
    std::atomic<int>      port_;
    NativeStreamEncoder   streamer_;

    std::mutex            video_mutex_;
    ImGuiContext*         video_imgui_ctx_{nullptr};
    RouenGPUTexture*      offscreen_texture_{nullptr};
    SDL_GPUTransferBuffer* download_buffer_{nullptr};
    std::vector<uint8_t>  render_buffer_;

    std::mutex            frame_mutex_;
    std::condition_variable frame_cv_;
    std::deque<std::vector<uint8_t>> video_frame_queue_;
    bool                  new_frame_ready_{false};

    std::mutex            audio_mutex_;
    std::vector<uint8_t>  audio_queue_;
    std::atomic<bool>     prebuffer_established_{false};
    std::atomic<bool>     audio_prebuffer_established_{false};
    std::atomic<double>   first_media_video_pts_time{-1.0};
    std::atomic<bool>     audio_aligned_to_video{false};



    std::thread           video_writer_thread_;
    std::thread           audio_writer_thread_;
    uint32_t              frame_number_{0};
    std::chrono::steady_clock::time_point last_frame_time_;
    std::chrono::steady_clock::time_point stream_start_time_{};
};

}  // namespace rouen::hosts
