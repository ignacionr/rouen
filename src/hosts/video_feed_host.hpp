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
    std::atomic<int> audio_delay_ms{0};

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
            audio_delay_ms.store(config->get_typed<int>("CAST_AUDIO_DELAY_MS").value_or(0));
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
        }
        return instance;
    }

    // ── public API ──────────────────────────────────────────────────

    bool is_running() const { return running_.load(); }

    int port() const { return port_.load(); }

    std::string endpoint() const {
        return std::format("udp://127.0.0.1:{}", port_.load());
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
        
        int delay_ms = audio_delay_ms.load();
        if (delay_ms <= 0) {
            audio_queue_.insert(audio_queue_.end(), data, data + size);
            audio_delay_queue_.clear();
        } else {
            delayed_audio_block block;
            block.timestamp = std::chrono::steady_clock::now();
            block.data.assign(data, data + size);
            audio_delay_queue_.push_back(std::move(block));
        }

        // Drain aged blocks if audio delay is active
        auto now = std::chrono::steady_clock::now();
        if (delay_ms > 0) {
            auto it = audio_delay_queue_.begin();
            while (it != audio_delay_queue_.end()) {
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->timestamp).count();
                if (age >= delay_ms) {
                    audio_queue_.insert(audio_queue_.end(), it->data.begin(), it->data.end());
                    it = audio_delay_queue_.erase(it);
                } else {
                    break;
                }
            }
        }

        // Cap audio queue to ~5 seconds max to prevent memory leakage
        constexpr size_t kMaxAudioQueueBytes = 882000; 
        if (audio_queue_.size() > kMaxAudioQueueBytes) {
            audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + (audio_queue_.size() - kMaxAudioQueueBytes));
        }
        if (audio_delay_queue_.size() > 500) {
            audio_delay_queue_.erase(audio_delay_queue_.begin());
        }
    }

    void reset_sync_queues() {
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_queue_.clear();
            audio_delay_queue_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            video_delay_queue_.clear();
            new_frame_ready_ = false;
        }
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

    /// Start the video feed natively.
    bool start() {
        if (running_.load()) {
            VIDEOFEED_WARN("VideoFeedHost: Already running");
            return false;
        }

        const int listen_port = port_.load();
        std::string udp_url = std::format("udp://127.0.0.1:{}?pkt_size=1316", listen_port);

        if (!streamer_.init(udp_url, kWidth, kHeight, kFps)) {
            VIDEOFEED_ERROR("VideoFeedHost: Failed to initialize NativeStreamEncoder");
            try { "notify"_sfn("Video Feed error: Failed to initialize native stream encoder"); } catch (...) {}
            return false;
        }

        running_.store(true);
        frame_number_ = 0;
        new_frame_ready_ = false;

        // Spawn dedicated background writer threads for video and audio
        video_writer_thread_ = std::thread(&VideoFeedHost::video_writer_loop, this);
        audio_writer_thread_ = std::thread(&VideoFeedHost::audio_writer_loop, this);

        VIDEOFEED_INFO_FMT("VideoFeedHost: NativeStreamEncoder started on port {}", listen_port);

        try {
            "notify"_sfn(std::format("Video feed started at {}", endpoint()));
        } catch (...) {}

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

            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
            if (fence) {
                SDL_WaitForGPUFences(device, true, &fence, 1);
                SDL_ReleaseGPUFence(device, fence);
            }

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

        // Push frame to double-buffered queue or delay queue based on offset direction using 0-copy swap/move
        {
            std::lock_guard<std::mutex> frame_lock(frame_mutex_);
            int delay_ms = audio_delay_ms.load();
            if (delay_ms >= 0) {
                std::swap(stream_buffer_, render_buffer_);
                new_frame_ready_ = true;
                video_delay_queue_.clear();
            } else {
                delayed_video_block block;
                block.timestamp = now;
                block.data = std::move(render_buffer_);
                video_delay_queue_.push_back(std::move(block));

                // Drain aged video blocks
                int abs_delay = -delay_ms;
                auto it = video_delay_queue_.begin();
                while (it != video_delay_queue_.end()) {
                    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->timestamp).count();
                    if (age >= abs_delay) {
                        stream_buffer_ = std::move(it->data);
                        new_frame_ready_ = true;
                        it = video_delay_queue_.erase(it);
                    } else {
                        break;
                    }
                }

                // Cap video delay queue to prevent memory leak (e.g. max 150 frames = 5 seconds)
                if (video_delay_queue_.size() > 150) {
                    video_delay_queue_.erase(video_delay_queue_.begin());
                }
            }
        }
        frame_cv_.notify_one();
    }

    /// Stop the feed gracefully.
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }

        VIDEOFEED_INFO("VideoFeedHost: Stopping...");

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

        auto next_frame = clock::now();

        while (running_.load()) {
            next_frame += kAudioFrameDuration;

            size_t copied = 0;
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                
                // Drain aged delay blocks if active
                auto now_t = clock::now();
                int delay_ms = audio_delay_ms.load();
                if (delay_ms > 0) {
                    auto it = audio_delay_queue_.begin();
                    while (it != audio_delay_queue_.end()) {
                        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now_t - it->timestamp).count();
                        if (age >= delay_ms) {
                            audio_queue_.insert(audio_queue_.end(), it->data.begin(), it->data.end());
                            it = audio_delay_queue_.erase(it);
                        } else {
                            break;
                        }
                    }
                } else {
                    audio_delay_queue_.clear();
                }

                if (!audio_queue_.empty()) {
                    copied = std::min(audio_queue_.size(), kAudioBytesPerFrame);
                    std::copy(audio_queue_.begin(), audio_queue_.begin() + copied, audio_chunk.begin());
                    audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + copied);
                }
            }

            if (copied < kAudioBytesPerFrame) {
                int16_t* fill_ptr = reinterpret_cast<int16_t*>(audio_chunk.data() + copied);
                size_t fill_samples = (kAudioBytesPerFrame - copied) / sizeof(int16_t);
                if (enable_pink_noise.load()) {
                    generate_pink_noise(fill_ptr, fill_samples);
                } else {
                    std::fill_n(fill_ptr, fill_samples, 0);
                }
            }

            streamer_.encode_audio(audio_chunk.data(), audio_chunk.size());

            auto now = clock::now();
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

        auto next_frame = clock::now();

        while (running_.load()) {
            next_frame += kVideoFrameDuration;

            {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                if (new_frame_ready_) {
                    local_buffer = stream_buffer_;
                    new_frame_ready_ = false;
                }
            }

            if (!local_buffer.empty()) {
                streamer_.encode_video(local_buffer.data());
            }

            auto now = clock::now();
            if (next_frame > now) {
                std::this_thread::sleep_for(next_frame - now);
            } else {
                next_frame = now;
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
            ImGui::SetNextWindowSize(ImVec2(650, 80));
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
                std::string status_str = std::format("FRAME: {:06d}   |   {:02d}:{:02d}:{:02d}",
                                                     frame_number_, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
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
    std::atomic<int>      port_;
    NativeStreamEncoder   streamer_;

    std::mutex            video_mutex_;
    ImGuiContext*         video_imgui_ctx_{nullptr};
    RouenGPUTexture*      offscreen_texture_{nullptr};
    SDL_GPUTransferBuffer* download_buffer_{nullptr};
    std::vector<uint8_t>  render_buffer_;

    std::mutex            frame_mutex_;
    std::condition_variable frame_cv_;
    std::vector<uint8_t>  stream_buffer_;
    bool                  new_frame_ready_{false};

    std::mutex            audio_mutex_;
    std::vector<uint8_t>  audio_queue_;

    struct delayed_audio_block {
        std::chrono::steady_clock::time_point timestamp;
        std::vector<uint8_t> data;
    };
    std::vector<delayed_audio_block> audio_delay_queue_;

    struct delayed_video_block {
        std::chrono::steady_clock::time_point timestamp;
        std::vector<uint8_t> data;
    };
    std::vector<delayed_video_block> video_delay_queue_;

    std::thread           video_writer_thread_;
    std::thread           audio_writer_thread_;
    uint32_t              frame_number_{0};
    std::chrono::steady_clock::time_point last_frame_time_;
};

}  // namespace rouen::hosts
