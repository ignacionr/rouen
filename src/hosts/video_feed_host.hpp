#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
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
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <SDL.h>
#include "../helpers/imgui_include.hpp"
#include "../helpers/debug.hpp"
#include "../helpers/texture_utils.hpp"
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
 * Stream Endpoint:  http://127.0.0.1:8889
 *   Connect with:  ./scripts/play_videofeed.sh
 */
class VideoFeedHost {
public:
    static constexpr int kWidth  = 1920;
    static constexpr int kHeight = 1080;
    static constexpr int kFps    = 24;
    static constexpr int kDefaultPort = 8889;

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

    /// Start the ffmpeg process.
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

        // Set non-blocking mode on write_fd so pipe writes never freeze main UI thread
        int flags = ::fcntl(write_fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(write_fd, F_SETFL, flags | O_NONBLOCK);
        }

        pid_t pid = ::fork();
        if (pid < 0) {
            VIDEOFEED_ERROR("VideoFeedHost: fork() failed");
            ::close(read_fd);
            ::close(write_fd);
            try { "notify"_sfn("Video Feed error: Failed to fork process"); } catch (...) {}
            return false;
        }

        if (pid == 0) {
            // ── child: exec ffmpeg ──────────────────────────────────
            ::close(write_fd);

            if (read_fd != STDIN_FILENO) {
                ::dup2(read_fd, STDIN_FILENO);
                ::close(read_fd);
            }

            std::string input_size = std::format("{}x{}", kWidth, kHeight);
            std::string framerate  = std::to_string(kFps);
            std::string listen_url = std::format("http://127.0.0.1:{}", listen_port);

            ::setenv("PATH",
                     "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"
                     ":/nix/var/nix/profiles/default/bin",
                     1);

            ::execlp("ffmpeg", "ffmpeg",
                     "-loglevel", "warning",
                     "-f", "rawvideo",
                     "-pixel_format", "rgb24",
                     "-video_size", input_size.c_str(),
                     "-framerate", framerate.c_str(),
                     "-i", "pipe:0",
                     "-c:v", "libx264",
                     "-preset", "ultrafast",
                     "-tune", "zerolatency",
                     "-pix_fmt", "yuv420p",
                     "-g", "4",
                     "-f", "mpegts",
                     "-listen", "1",
                     listen_url.c_str(),
                     static_cast<char*>(nullptr));

            ::_exit(1);
        }

        // ── parent ──────────────────────────────────────────────────
        ::close(read_fd);

        ffmpeg_pid_.store(pid);
        write_fd_.store(write_fd);
        running_.store(true);
        frame_number_ = 0;

        VIDEOFEED_INFO_FMT("VideoFeedHost: ffmpeg started (PID {}, port {})", pid, listen_port);

        try {
            "notify"_sfn(std::format("Video feed started at {}", endpoint()));
        } catch (...) {}

        return true;
#endif  // _WIN32
    }

    /// Perform an offscreen ImGui render pass and stream the frame to ffmpeg.
    void render_video_frame(SDL_Renderer* renderer) {
        if (!running_.load() || !renderer) return;

        int fd = write_fd_.load();
        if (fd < 0) return;

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

        // Lazy-initialize secondary offscreen ImGui context
        if (!video_imgui_ctx_) {
            ImFontAtlas* shared_fonts = orig_ctx ? orig_ctx->IO.Fonts : nullptr;
            video_imgui_ctx_ = ImGui::CreateContext(shared_fonts);
        }

        // Switch to secondary ImGui context
        ImGui::SetCurrentContext(video_imgui_ctx_);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
        io.DeltaTime = 1.0f / static_cast<float>(kFps);

        // Set render target to offscreen texture
        SDL_SetRenderTarget(renderer, offscreen_texture_);
        SDL_SetRenderDrawColor(renderer, 15, 20, 30, 255);
        SDL_RenderClear(renderer);

        // Start offscreen ImGui frame
        ImGui::NewFrame();

        // 1. Render Video Feed Header & Base UI
        render_video_base_ui();

        // 2. Render Active Cards' Video UI
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

        // Render ImGui draw data onto offscreen_texture_
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());

        // Read pixels back to RAM buffer (RGB24 format for ffmpeg)
        if (pixel_buffer_.size() != static_cast<size_t>(kWidth * kHeight * 3)) {
            pixel_buffer_.resize(static_cast<size_t>(kWidth * kHeight * 3));
        }

        SDL_RenderReadPixels(
            renderer,
            nullptr,
            SDL_PIXELFORMAT_RGB24,
            pixel_buffer_.data(),
            kWidth * 3
        );

        // Restore original render target and ImGui context
        SDL_SetRenderTarget(renderer, orig_target);
        ImGui::SetCurrentContext(orig_ctx);

        ++frame_number_;

        // Write raw RGB24 frame to non-blocking ffmpeg pipe
        const uint8_t* ptr = pixel_buffer_.data();
        size_t remaining = pixel_buffer_.size();
        while (remaining > 0 && running_.load()) {
            auto written = ::write(fd, ptr, remaining);
            if (written <= 0) {
                if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Pipe buffer full or ffmpeg waiting for client connection; drop frame without blocking UI
                    break;
                }
                if (written < 0 && errno == EINTR) continue;
                break;
            }
            ptr += written;
            remaining -= static_cast<size_t>(written);
        }
    }

    /// Stop the feed gracefully.
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }

        VIDEOFEED_INFO("VideoFeedHost: Stopping...");

#ifndef _WIN32
        int fd = write_fd_.exchange(-1);
        if (fd >= 0) {
            ::close(fd);
        }

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
    }

private:
    void render_video_base_ui() {
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

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

        // Top ImGui Header Window
        ImGui::SetNextWindowPos(ImVec2(40, 30));
        ImGui::SetNextWindowSize(ImVec2(700, 110));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.12f, 0.20f, 0.85f));

        if (ImGui::Begin("##VideoHeader", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
            ImGui::SetWindowFontScale(2.2f);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "ROUEN MULTI-MODAL UI");
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.6f, 1.0f), "24 FPS LIVE STREAM  |  FULL HD 1920x1080");
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // Bottom ImGui Footer Window
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

    void cleanup_imgui_context() {
        std::lock_guard<std::mutex> lock(video_mutex_);
        if (video_imgui_ctx_) {
            ImGuiContext* orig_ctx = ImGui::GetCurrentContext();
            if (orig_ctx == video_imgui_ctx_) {
                ImGui::SetCurrentContext(nullptr);
            }
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
    std::vector<uint8_t>  pixel_buffer_;
    uint32_t              frame_number_{0};
    std::chrono::steady_clock::time_point last_frame_time_;
};

}  // namespace rouen::hosts
