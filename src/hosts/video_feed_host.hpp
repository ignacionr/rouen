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
        }
        return instance;
    }

    // ── public API ──────────────────────────────────────────────────

    bool is_running() const { return running_.load(); }

    int port() const { return port_.load(); }

    std::string endpoint() const {
        return std::format("http://127.0.0.1:{}", port_.load());
    }

    void set_port(int p) {
        if (!running_.load()) {
            port_.store(p);
        }
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

        const int read_fd  = pipe_fds[0];
        const int write_fd = pipe_fds[1];
        const int listen_port = port_.load();

        // Clean up any leftover orphaned ffmpeg process listening on port from previous runs/crashes
        std::string kill_cmd = std::format("pkill -9 -f 'ffmpeg.*listen.*{}' >/dev/null 2>&1", listen_port);
        ::system(kill_cmd.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Use posix_spawn instead of fork() to be 100% thread-safe and Cocoa-compliant on macOS
        posix_spawn_file_actions_t file_actions;
        posix_spawn_file_actions_init(&file_actions);
        posix_spawn_file_actions_adddup2(&file_actions, read_fd, STDIN_FILENO);
        posix_spawn_file_actions_addclose(&file_actions, write_fd);

        std::string input_size = std::format("{}x{}", kWidth, kHeight);
        std::string framerate  = std::to_string(kFps);
        std::string listen_url = std::format("http://127.0.0.1:{}", listen_port);

        char* argv[] = {
            const_cast<char*>("ffmpeg"),
            const_cast<char*>("-loglevel"), const_cast<char*>("warning"),
            const_cast<char*>("-f"), const_cast<char*>("rawvideo"),
            const_cast<char*>("-pixel_format"), const_cast<char*>("rgb24"),
            const_cast<char*>("-video_size"), const_cast<char*>(input_size.c_str()),
            const_cast<char*>("-framerate"), const_cast<char*>(framerate.c_str()),
            const_cast<char*>("-i"), const_cast<char*>("pipe:0"),
            const_cast<char*>("-c:v"), const_cast<char*>("libx264"),
            const_cast<char*>("-preset"), const_cast<char*>("ultrafast"),
            const_cast<char*>("-tune"), const_cast<char*>("zerolatency"),
            const_cast<char*>("-pix_fmt"), const_cast<char*>("yuv420p"),
            const_cast<char*>("-g"), const_cast<char*>("4"),
            const_cast<char*>("-f"), const_cast<char*>("mpegts"),
            const_cast<char*>("-listen"), const_cast<char*>("1"),
            const_cast<char*>(listen_url.c_str()),
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

        if (status != 0) {
            VIDEOFEED_ERROR_FMT("VideoFeedHost: posix_spawnp failed with status {}", status);
            ::close(write_fd);
            try { "notify"_sfn("Video Feed error: Failed to spawn ffmpeg"); } catch (...) {}
            return false;
        }

        ffmpeg_pid_.store(pid);
        write_fd_.store(write_fd);
        running_.store(true);
        frame_number_ = 0;
        new_frame_ready_ = false;

        // Spawn background writer thread
        writer_thread_ = std::thread(&VideoFeedHost::writer_loop, this, write_fd);

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

        if (writer_thread_.joinable()) {
            writer_thread_.join();
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

private:
    void writer_loop(int fd) {
        using clock = std::chrono::steady_clock;
        constexpr auto kFrameDuration = std::chrono::microseconds(1000000 / kFps);
        auto next_frame = clock::now();

        std::vector<uint8_t> local_buffer;

        while (running_.load()) {
            next_frame += kFrameDuration;

            {
                std::unique_lock<std::mutex> lock(frame_mutex_);
                frame_cv_.wait_until(lock, next_frame, [this]() {
                    return !running_.load() || new_frame_ready_;
                });

                if (!running_.load()) break;

                if (new_frame_ready_) {
                    local_buffer = stream_buffer_;
                    new_frame_ready_ = false;
                }
            }

            if (!local_buffer.empty()) {
                const uint8_t* ptr = local_buffer.data();
                size_t remaining = local_buffer.size();

                while (remaining > 0 && running_.load()) {
                    auto written = ::write(fd, ptr, remaining);
                    if (written <= 0) {
                        if (written < 0 && errno == EINTR) continue;
                        break;
                    }
                    ptr += written;
                    remaining -= static_cast<size_t>(written);
                }
            }

            std::this_thread::sleep_until(next_frame);
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
            ImGui::SetNextWindowSize(ImVec2(700, 110));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.12f, 0.20f, 0.85f));

            if (ImGui::Begin("##VideoHeader", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
                ImGui::SetWindowFontScale(2.2f);
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "ROUEN MULTI-MODAL UI");
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.6f, 1.0f), "30 FPS LIVE STREAM  |  FULL HD 1920x1080");
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

    std::mutex            video_mutex_;
    ImGuiContext*         video_imgui_ctx_{nullptr};
    SDL_Texture*          offscreen_texture_{nullptr};
    std::vector<uint8_t>  render_buffer_;

    std::mutex            frame_mutex_;
    std::condition_variable frame_cv_;
    std::vector<uint8_t>  stream_buffer_;
    bool                  new_frame_ready_{false};

    std::thread           writer_thread_;
    uint32_t              frame_number_{0};
    std::chrono::steady_clock::time_point last_frame_time_;
};

}  // namespace rouen::hosts
