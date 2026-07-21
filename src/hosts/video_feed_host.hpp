#pragma once

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

#include <SDL.h>
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

    VideoFeedHost()
        : port_(kDefaultPort) {
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
        constexpr size_t kMaxAudioQueueBytes = 1764000; // ~10 sec stereo PCM at 44.1kHz
        if (audio_queue_.size() + size > kMaxAudioQueueBytes) {
            size_t overflow = (audio_queue_.size() + size) - kMaxAudioQueueBytes;
            if (overflow < audio_queue_.size()) {
                audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + overflow);
            } else {
                audio_queue_.clear();
            }
        }
        audio_queue_.insert(audio_queue_.end(), data, data + size);
    }

    /// Return an ImTextureID usable directly inside Rouen's main desktop ImGui windows
    ImTextureID get_texture_id(SDL_Renderer* renderer = nullptr) {
        std::lock_guard<std::mutex> lock(video_mutex_);
        if (offscreen_texture_) {
            return rouen::helpers::sdl_texture_cast(offscreen_texture_);
        }
        if (!renderer) {
            try {
                auto r_ptr = registrar::get<SDL_Renderer*>("main_renderer");
                if (r_ptr && *r_ptr) renderer = *r_ptr;
            } catch (...) {}
        }
        if (renderer && offscreen_texture_) {
            return rouen::helpers::sdl_texture_cast(offscreen_texture_);
        }
        return ImTextureID{};
    }

    /// Start the ffmpeg process safely using posix_spawn.
    bool start() {
        if (running_.load()) {
            VIDEOFEED_WARN("VideoFeedHost: Already running");
            return false;
        }

#ifdef _WIN32
        VIDEOFEED_ERROR("VideoFeedHost: Not supported on Windows");
        return false;
#else
        // Ignore SIGPIPE globally to prevent process crash on pipe disconnect
        ::signal(SIGPIPE, SIG_IGN);

        int pipe_fds[2];
        if (::pipe(pipe_fds) != 0) {
            VIDEOFEED_ERROR("VideoFeedHost: pipe() failed");
            try { "notify"_sfn("Video Feed error: Failed to create pipe"); } catch (...) {}
            return false;
        }

        int audio_pipe_fds[2];
        if (::pipe(audio_pipe_fds) != 0) {
            VIDEOFEED_ERROR("VideoFeedHost: audio pipe() failed");
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
            try { "notify"_sfn("Video Feed error: Failed to create audio pipe"); } catch (...) {}
            return false;
        }

        const int read_fd        = pipe_fds[0];
        const int write_fd       = pipe_fds[1];
        const int audio_read_fd  = audio_pipe_fds[0];
        const int audio_write_fd = audio_pipe_fds[1];
        const int listen_port    = port_.load();

        // Clean up any leftover orphaned ffmpeg process listening on port from previous runs/crashes
        std::string kill_cmd = std::format("pkill -9 -f 'ffmpeg.*listen.*{}' >/dev/null 2>&1", listen_port);
        ::system(kill_cmd.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Use posix_spawn file actions to map read_fd -> 0 (STDIN) and audio_read_fd -> 3
        posix_spawn_file_actions_t file_actions;
        posix_spawn_file_actions_init(&file_actions);
        posix_spawn_file_actions_adddup2(&file_actions, read_fd, STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&file_actions, audio_read_fd, 3);

        if (write_fd != STDIN_FILENO && write_fd != 3) {
            posix_spawn_file_actions_addclose(&file_actions, write_fd);
        }
        if (audio_write_fd != STDIN_FILENO && audio_write_fd != 3) {
            posix_spawn_file_actions_addclose(&file_actions, audio_write_fd);
        }

        std::string input_size = std::format("{}x{}", kWidth, kHeight);
        std::string framerate  = std::to_string(kFps);
        std::string udp_url = std::format("udp://127.0.0.1:{}?pkt_size=1316", listen_port);

        char* argv[] = {
            const_cast<char*>("ffmpeg"),
            const_cast<char*>("-loglevel"), const_cast<char*>("warning"),
            const_cast<char*>("-threads"), const_cast<char*>("4"),
            // Raw Video Input on pipe:0
            const_cast<char*>("-f"), const_cast<char*>("rawvideo"),
            const_cast<char*>("-pixel_format"), const_cast<char*>("rgb24"),
            const_cast<char*>("-video_size"), const_cast<char*>(input_size.c_str()),
            const_cast<char*>("-framerate"), const_cast<char*>(framerate.c_str()),
            const_cast<char*>("-i"), const_cast<char*>("pipe:0"),
            // Raw Stereo PCM Audio Input on pipe:3
            const_cast<char*>("-f"), const_cast<char*>("s16le"),
            const_cast<char*>("-ac"), const_cast<char*>("2"),
            const_cast<char*>("-ar"), const_cast<char*>("44100"),
            const_cast<char*>("-analyzeduration"), const_cast<char*>("100000"),
            const_cast<char*>("-probesize"), const_cast<char*>("10000"),
            const_cast<char*>("-i"), const_cast<char*>("pipe:3"),
            // Video Encoding
            const_cast<char*>("-c:v"), const_cast<char*>("libx264"),
            const_cast<char*>("-preset"), const_cast<char*>("ultrafast"),
            const_cast<char*>("-tune"), const_cast<char*>("zerolatency"),
            const_cast<char*>("-pix_fmt"), const_cast<char*>("yuv420p"),
            const_cast<char*>("-g"), const_cast<char*>("4"),
            // Audio Encoding
            const_cast<char*>("-c:a"), const_cast<char*>("aac"),
            const_cast<char*>("-b:a"), const_cast<char*>("128k"),
            const_cast<char*>("-ac"), const_cast<char*>("2"),
            const_cast<char*>("-ar"), const_cast<char*>("44100"),
            // Output MPEG-TS via UDP
            const_cast<char*>("-f"), const_cast<char*>("mpegts"),
            const_cast<char*>(udp_url.c_str()),
            nullptr
        };

        // Ensure PATH includes Homebrew, Nix, and standard system paths for posix_spawnp
        ::setenv("PATH",
                 "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"
                 ":/nix/var/nix/profiles/default/bin",
                 1);

        // Redirect ffmpeg stderr to /tmp/rouen_ffmpeg.log for diagnostic logging
        int log_fd = ::open("/tmp/rouen_ffmpeg.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0) {
            posix_spawn_file_actions_adddup2(&file_actions, log_fd, STDERR_FILENO);
            posix_spawn_file_actions_addclose(&file_actions, log_fd);
        }

        pid_t pid;
        int status = ::posix_spawnp(&pid, "ffmpeg", &file_actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&file_actions);
        if (log_fd >= 0) {
            ::close(log_fd);
        }
        ::close(read_fd);
        ::close(audio_read_fd);

        if (status != 0) {
            VIDEOFEED_ERROR_FMT("VideoFeedHost: posix_spawnp failed with status {}", status);
            ::close(write_fd);
            ::close(audio_write_fd);
            try { "notify"_sfn("Video Feed error: Failed to spawn ffmpeg"); } catch (...) {}
            return false;
        }

        ffmpeg_pid_.store(pid);
#ifndef _WIN32
        ::setpriority(PRIO_PROCESS, pid, -10);
#endif
        write_fd_.store(write_fd);
        audio_write_fd_.store(audio_write_fd);
        running_.store(true);
        frame_number_ = 0;
        new_frame_ready_ = false;

        // Set pipe write descriptors to O_NONBLOCK to prevent OS pipe deadlock between dual streams
        fcntl(write_fd, F_SETFL, fcntl(write_fd, F_GETFL, 0) | O_NONBLOCK);
        fcntl(audio_write_fd, F_SETFL, fcntl(audio_write_fd, F_GETFL, 0) | O_NONBLOCK);

        // Pre-fill initial audio and video buffers so FFmpeg stream demuxers open immediately
        std::vector<uint8_t> init_audio(60000, 0);
        generate_pink_noise(reinterpret_cast<int16_t*>(init_audio.data()), init_audio.size() / 2);
        std::vector<uint8_t> init_video(static_cast<size_t>(kWidth * kHeight * 3), 0);
        write_full_frame(audio_write_fd, init_audio.data(), init_audio.size(), running_);
        write_full_frame(write_fd, init_video.data(), init_video.size(), running_);

        // Spawn dedicated background writer threads for video and audio
        video_writer_thread_ = std::thread(&VideoFeedHost::video_writer_loop, this, write_fd);
        audio_writer_thread_ = std::thread(&VideoFeedHost::audio_writer_loop, this, audio_write_fd);

        VIDEOFEED_INFO_FMT("VideoFeedHost: ffmpeg started via posix_spawn (PID {}, port {})", pid, listen_port);

        try {
            "notify"_sfn(std::format("Video feed started at {}", endpoint()));
        } catch (...) {}

        return true;
#endif  // _WIN32
    }

    /// Perform an offscreen ImGui render pass on the main thread and queue the frame.
    void render_video_frame(SDL_Renderer* renderer) {
        if (!running_.load() || !renderer) return;

        // Rate limit to 24 fps (41ms interval)
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time_).count();
        if (elapsed_ms < (1000 / kFps)) {
            return;
        }
        last_frame_time_ = now;

        std::lock_guard<std::mutex> lock(video_mutex_);

        // Save original context and render target
        ImGuiContext* orig_ctx = ImGui::GetCurrentContext();
        if (!orig_ctx) return;
        SDL_Texture* orig_target = SDL_GetRenderTarget(renderer);

        // Lazy-initialize offscreen target texture
        if (!offscreen_texture_) {
            offscreen_texture_ = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET,
                kWidth, kHeight
            );
            if (!offscreen_texture_) {
                VIDEOFEED_ERROR_FMT("Failed to create offscreen_texture_: {}", SDL_GetError());
                return;
            }
        }

        // Lazy-initialize secondary offscreen ImGui context sharing main font atlas
        if (!video_imgui_ctx_) {
            ImFontAtlas* shared_fonts = orig_ctx->IO.Fonts;
            video_imgui_ctx_ = ImGui::CreateContext(shared_fonts);
        }

        // Copy backend data pointers so SDL2 renderer backend works in secondary context
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

        // Set render target to offscreen texture
        SDL_SetRenderTarget(renderer, offscreen_texture_);
        SDL_SetRenderDrawColor(renderer, 15, 20, 30, 255);
        SDL_RenderClear(renderer);

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

            // Render Active Media Player Video Stream on Cast
            try {
                std::lock_guard<std::recursive_mutex> map_lock(media_player::items_mutex());
                for (auto& [id, item_ptr] : media_player::items()) {
                    if (item_ptr && item_ptr->is_playing && item_ptr->has_video) {
                        ImTextureID media_tex = item_ptr->get_texture_id(renderer);
                        if (media_tex) {
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
                            break;
                        }
                    }
                }
            } catch (...) {}
        }

        // Render ImGui draw data onto offscreen_texture_
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());

        // Read pixels back to RAM buffer (RGB24 format for ffmpeg)
        if (render_buffer_.size() != static_cast<size_t>(kWidth * kHeight * 3)) {
            render_buffer_.resize(static_cast<size_t>(kWidth * kHeight * 3));
        }

        SDL_RenderReadPixels(
            renderer,
            nullptr,
            SDL_PIXELFORMAT_RGB24,
            render_buffer_.data(),
            kWidth * 3
        );

        // Restore original render target and ImGui context
        SDL_SetRenderTarget(renderer, orig_target);
        ImGui::SetCurrentContext(orig_ctx);

        ++frame_number_;

        // Push frame to double-buffered queue and notify background writer thread
        {
            std::lock_guard<std::mutex> frame_lock(frame_mutex_);
            stream_buffer_ = render_buffer_;
            new_frame_ready_ = true;
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

        int fd = write_fd_.exchange(-1);
        if (fd >= 0) {
            ::close(fd);
        }
        int afd = audio_write_fd_.exchange(-1);
        if (afd >= 0) {
            ::close(afd);
        }

        if (video_writer_thread_.joinable()) {
            video_writer_thread_.join();
        }
        if (audio_writer_thread_.joinable()) {
            audio_writer_thread_.join();
        }

#ifndef _WIN32
        pid_t pid = ffmpeg_pid_.exchange(0);
        if (pid > 0) {
            int status = 0;
            if (::waitpid(pid, &status, WNOHANG) == 0) {
                ::kill(pid, SIGTERM);
                for (int i = 0; i < 10 && ::waitpid(pid, &status, WNOHANG) == 0; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (::waitpid(pid, &status, WNOHANG) == 0) {
                    ::kill(pid, SIGKILL);
                    ::waitpid(pid, &status, 0);
                }
            }
            VIDEOFEED_INFO_FMT("VideoFeedHost: ffmpeg (PID {}) stopped", pid);
        }

        try {
            "notify"_sfn("Video feed stopped");
        } catch (...) {}
#endif

        cleanup_imgui_context();
    }

    static bool write_full_frame(int fd, const uint8_t* data, size_t size, std::atomic<bool>& running) {
        if (fd < 0 || !data || size == 0) return false;
        size_t remaining = size;
        const uint8_t* ptr = data;
        constexpr size_t kChunkSize = 65536; // 64KB chunks matching kernel pipe capacity

        while (remaining > 0 && running.load()) {
            size_t to_write = std::min(remaining, kChunkSize);
            ssize_t written = ::write(fd, ptr, to_write);
            if (written > 0) {
                ptr += written;
                remaining -= static_cast<size_t>(written);
            } else if (written < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue;
                }
                return false;
            } else {
                return false;
            }
        }
        return remaining == 0;
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
    void audio_writer_loop(int audio_fd) {
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
                if (!audio_queue_.empty()) {
                    copied = std::min(audio_queue_.size(), kAudioBytesPerFrame);
                    std::copy(audio_queue_.begin(), audio_queue_.begin() + copied, audio_chunk.begin());
                    audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + copied);
                }
            }

            if (copied < kAudioBytesPerFrame) {
                int16_t* fill_ptr = reinterpret_cast<int16_t*>(audio_chunk.data() + copied);
                size_t fill_samples = (kAudioBytesPerFrame - copied) / sizeof(int16_t);
                generate_pink_noise(fill_ptr, fill_samples);
            }

            if (audio_fd >= 0) {
                write_full_frame(audio_fd, audio_chunk.data(), audio_chunk.size(), running_);
            }

            auto now = clock::now();
            if (next_frame > now) {
                std::this_thread::sleep_for(next_frame - now);
            } else {
                next_frame = now;
            }
        }
    }

    void video_writer_loop(int video_fd) {
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

            if (video_fd >= 0 && !local_buffer.empty()) {
                write_full_frame(video_fd, local_buffer.data(), local_buffer.size(), running_);
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
        if (offscreen_texture_) {
            SDL_DestroyTexture(offscreen_texture_);
            offscreen_texture_ = nullptr;
        }
    }

    // ── member data ─────────────────────────────────────────────────
    std::atomic<bool>     running_{false};
    std::atomic<int>      port_;
    std::atomic<pid_t>    ffmpeg_pid_{0};
    std::atomic<int>      write_fd_{-1};
    std::atomic<int>      audio_write_fd_{-1};

    std::mutex            video_mutex_;
    ImGuiContext*         video_imgui_ctx_{nullptr};
    SDL_Texture*          offscreen_texture_{nullptr};
    std::vector<uint8_t>  render_buffer_;

    std::mutex            frame_mutex_;
    std::condition_variable frame_cv_;
    std::vector<uint8_t>  stream_buffer_;
    bool                  new_frame_ready_{false};

    std::mutex            audio_mutex_;
    std::vector<uint8_t>  audio_queue_;

    std::thread           video_writer_thread_;
    std::thread           audio_writer_thread_;
    uint32_t              frame_number_{0};
    std::chrono::steady_clock::time_point last_frame_time_;
};

}  // namespace rouen::hosts
