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
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <SDL.h>
#include "../cards/interface/card.hpp"
#include "../helpers/debug.hpp"
#include "../helpers/texture_utils.hpp"
#include "../registrar.hpp"

namespace rouen::hosts {

/**
 * VideoFeedHost — streams a synthetic test-pattern video through ffmpeg.
 *
 * Frame generation uses an SDL_Surface (SDL2) as the underlying pixel buffer.
 * Text overlays are drawn directly onto the SDL_Surface pixels.
 *
 * ImGui Compatibility:
 *   Call get_texture_id() or get_texture_id(renderer) to receive an ImTextureID
 *   that can be rendered inside any ImGui window with ImGui::Image(...).
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
        std::lock_guard<std::mutex> lock(surface_mutex_);
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
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

    /// Return the raw SDL_Surface pointer of the current frame
    SDL_Surface* get_surface() {
        std::lock_guard<std::mutex> lock(surface_mutex_);
        return surface_;
    }

    /// Return an ImTextureID usable directly inside ImGui windows/cards
    ImTextureID get_texture_id(SDL_Renderer* renderer = nullptr) {
        std::lock_guard<std::mutex> lock(surface_mutex_);
        if (!surface_) return ImTextureID{};

        if (!renderer) {
            try {
                auto r_ptr = registrar::get<SDL_Renderer*>("main_renderer");
                if (r_ptr && *r_ptr) renderer = *r_ptr;
            } catch (...) {}
        }

        if (renderer) {
            if (!texture_) {
                texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
            }
            if (texture_) {
                SDL_UpdateTexture(texture_, nullptr, surface_->pixels, surface_->pitch);
                return rouen::helpers::sdl_texture_cast(texture_);
            }
        }
        return ImTextureID{};
    }

    // ── Scalable 8x8 bitmap font drawer for SDL_Surface ──────────────

    static void draw_char_on_surface(SDL_Surface* surface, int start_x, int start_y, char c, uint8_t r, uint8_t g, uint8_t b, int scale = 4) {
        if (!surface || c < 32 || c > 126) return;

        static const uint8_t font8x8[95][8] = {
            /* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
            /* '!' */ {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
            /* '"' */ {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},
            /* '#' */ {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
            /* '$' */ {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
            /* '%' */ {0x00,0x66,0x6C,0x18,0x30,0x66,0x46,0x00},
            /* '&' */ {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
            /* '\''*/ {0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00},
            /* '(' */ {0x18,0x30,0x60,0x60,0x60,0x30,0x18,0x00},
            /* ')' */ {0x60,0x30,0x18,0x18,0x18,0x30,0x60,0x00},
            /* '*' */ {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
            /* '+' */ {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
            /* ',' */ {0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x60},
            /* '-' */ {0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00},
            /* '.' */ {0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00},
            /* '/' */ {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
            /* '0' */ {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
            /* '1' */ {0x0C,0x1C,0x0C,0x0C,0x0C,0x0C,0x3E,0x00},
            /* '2' */ {0x3E,0x63,0x06,0x1C,0x30,0x63,0x7F,0x00},
            /* '3' */ {0x7E,0x06,0x0C,0x1C,0x06,0x63,0x3E,0x00},
            /* '4' */ {0x0C,0x1C,0x3C,0x6C,0x7F,0x0C,0x0C,0x00},
            /* '5' */ {0x7F,0x60,0x7E,0x03,0x03,0x63,0x3E,0x00},
            /* '6' */ {0x1C,0x30,0x60,0x7E,0x63,0x63,0x3E,0x00},
            /* '7' */ {0x7F,0x63,0x06,0x0C,0x18,0x18,0x18,0x00},
            /* '8' */ {0x3E,0x63,0x63,0x3E,0x63,0x63,0x3E,0x00},
            /* '9' */ {0x3E,0x63,0x63,0x3F,0x03,0x06,0x3C,0x00},
            /* ':' */ {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
            /* ';' */ {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
            /* '<' */ {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
            /* '=' */ {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
            /* '>' */ {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
            /* '?' */ {0x3E,0x63,0x0C,0x18,0x18,0x00,0x18,0x00},
            /* '@' */ {0x3E,0x63,0x6F,0x6B,0x6F,0x60,0x3E,0x00},
            /* 'A' */ {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
            /* 'B' */ {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
            /* 'C' */ {0x3E,0x63,0x60,0x60,0x60,0x63,0x3E,0x00},
            /* 'D' */ {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
            /* 'E' */ {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},
            /* 'F' */ {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},
            /* 'G' */ {0x3E,0x63,0x60,0x6E,0x63,0x63,0x3E,0x00},
            /* 'H' */ {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
            /* 'I' */ {0x3E,0x0C,0x0C,0x0C,0x0C,0x0C,0x3E,0x00},
            /* 'J' */ {0x1E,0x06,0x06,0x06,0x06,0x66,0x3C,0x00},
            /* 'K' */ {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
            /* 'L' */ {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
            /* 'M' */ {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
            /* 'N' */ {0x63,0x73,0x7B,0x6F,0x67,0x63,0x63,0x00},
            /* 'O' */ {0x3E,0x63,0x63,0x63,0x63,0x63,0x3E,0x00},
            /* 'P' */ {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
            /* 'Q' */ {0x3E,0x63,0x63,0x63,0x6B,0x37,0x1E,0x00},
            /* 'R' */ {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},
            /* 'S' */ {0x3E,0x63,0x30,0x1E,0x03,0x63,0x3E,0x00},
            /* 'T' */ {0x7F,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
            /* 'U' */ {0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x00},
            /* 'V' */ {0x63,0x63,0x63,0x63,0x63,0x36,0x1C,0x00},
            /* 'W' */ {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
            /* 'X' */ {0x63,0x63,0x36,0x1C,0x36,0x63,0x63,0x00},
            /* 'Y' */ {0x63,0x63,0x36,0x1C,0x18,0x18,0x18,0x00},
            /* 'Z' */ {0x7F,0x06,0x0C,0x18,0x30,0x60,0x7F,0x00},
            /* '[' */ {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
            /* '\'*/ {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
            /* ']' */ {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
            /* '^' */ {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},
            /* '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00},
            /* '`' */ {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
            /* 'a' */ {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3B,0x00},
            /* 'b' */ {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},
            /* 'c' */ {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00},
            /* 'd' */ {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
            /* 'e' */ {0x00,0x00,0x3E,0x66,0x7E,0x60,0x3C,0x00},
            /* 'f' */ {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00},
            /* 'g' */ {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x7C},
            /* 'h' */ {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
            /* 'i' */ {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
            /* 'j' */ {0x06,0x00,0x0E,0x06,0x06,0x06,0x66,0x3C},
            /* 'k' */ {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
            /* 'l' */ {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
            /* 'm' */ {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00},
            /* 'n' */ {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
            /* 'o' */ {0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x00},
            /* 'p' */ {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
            /* 'q' */ {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
            /* 'r' */ {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},
            /* 's' */ {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
            /* 't' */ {0x18,0x7E,0x18,0x18,0x18,0x18,0x0E,0x00},
            /* 'u' */ {0x00,0x00,0x66,0x66,0x66,0x66,0x3B,0x00},
            /* 'v' */ {0x00,0x00,0x66,0x66,0x66,0x36,0x1C,0x00},
            /* 'w' */ {0x00,0x00,0x63,0x63,0x6B,0x7F,0x36,0x00},
            /* 'x' */ {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
            /* 'y' */ {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x7C},
            /* 'z' */ {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},
            /* '{' */ {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
            /* '|' */ {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
            /* '}' */ {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
            /* '~' */ {0x3A,0x5C,0x00,0x00,0x00,0x00,0x00,0x00}
        };

        const uint8_t* glyph = font8x8[c - 32];
        uint8_t* pixels = static_cast<uint8_t*>(surface->pixels);
        int pitch = surface->pitch;

        for (int row = 0; row < 8; ++row) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; ++sy) {
                        int py = start_y + row * scale + sy;
                        if (py < 0 || py >= surface->h) continue;
                        for (int sx = 0; sx < scale; ++sx) {
                            int px = start_x + col * scale + sx;
                            if (px < 0 || px >= surface->w) continue;
                            int idx = py * pitch + px * 3;
                            pixels[idx + 0] = r;
                            pixels[idx + 1] = g;
                            pixels[idx + 2] = b;
                        }
                    }
                }
            }
        }
    }

    static void draw_string_on_surface(SDL_Surface* surface, int x, int y, std::string_view str, uint8_t r, uint8_t g, uint8_t b, int scale = 4) {
        int cur_x = x;
        for (char c : str) {
            draw_char_on_surface(surface, cur_x, y, c, r, g, b, scale);
            cur_x += 8 * scale;
        }
    }

    /// Start the ffmpeg process and the frame-generation thread.
    bool start() {
        if (running_.load()) {
            VIDEOFEED_WARN("VideoFeedHost: Already running");
            return false;
        }

#ifdef _WIN32
        VIDEOFEED_ERROR("VideoFeedHost: Not supported on Windows");
        return false;
#else
        // Ignore SIGPIPE globally to prevent process crash when writing to closed pipe/socket
        ::signal(SIGPIPE, SIG_IGN);

        // Create pipe: pipe_fds[0] = read end (ffmpeg), pipe_fds[1] = write end (us)
        int pipe_fds[2];
        if (::pipe(pipe_fds) != 0) {
            VIDEOFEED_ERROR("VideoFeedHost: pipe() failed");
            try { "notify"_sfn("Video Feed error: Failed to create pipe"); } catch (...) {}
            return false;
        }

        const int read_fd  = pipe_fds[0];
        const int write_fd = pipe_fds[1];
        const int listen_port = port_.load();

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
            ::close(write_fd);  // child doesn't write

            // Redirect read_fd to stdin
            if (read_fd != STDIN_FILENO) {
                ::dup2(read_fd, STDIN_FILENO);
                ::close(read_fd);
            }

            std::string input_size = std::format("{}x{}", kWidth, kHeight);
            std::string framerate  = std::to_string(kFps);
            std::string listen_url = std::format("http://127.0.0.1:{}", listen_port);

            // Set PATH to find ffmpeg in common locations
            ::setenv("PATH",
                     "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"
                     ":/nix/var/nix/profiles/default/bin",
                     1);

            // ffmpeg HTTP listen command
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

            // If execlp returns, exec failed
            ::_exit(1);
        }

        // ── parent ──────────────────────────────────────────────────
        ::close(read_fd);  // parent doesn't read from the pipe

        ffmpeg_pid_.store(pid);
        write_fd_.store(write_fd);
        running_.store(true);

        VIDEOFEED_INFO_FMT("VideoFeedHost: ffmpeg started (PID {}, port {})",
                           pid, listen_port);

        // Notify user via Rouen's notification service
        try {
            "notify"_sfn(std::format("Video feed started at {}", endpoint()));
        } catch (...) {}

        // Launch frame generation thread
        generator_thread_ = std::thread([this, write_fd]() {
            generate_frames(write_fd);
        });

        return true;
#endif  // _WIN32
    }

    /// Stop the feed gracefully.
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }

        VIDEOFEED_INFO("VideoFeedHost: Stopping...");

#ifndef _WIN32
        // Close write end of pipe
        int fd = write_fd_.exchange(-1);
        if (fd >= 0) {
            ::close(fd);
        }

        // Wait for generator thread to finish
        if (generator_thread_.joinable()) {
            generator_thread_.join();
        }

        // Terminate ffmpeg process
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
    // ── frame generation ────────────────────────────────────────────

    void generate_frames(int fd) {
        // Create an SDL_Surface (RGB24 format)
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, kWidth, kHeight, 24, SDL_PIXELFORMAT_RGB24);
        if (!surface) {
            VIDEOFEED_ERROR_FMT("VideoFeedHost: Failed to create SDL_Surface: {}", SDL_GetError());
            running_.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(surface_mutex_);
            surface_ = surface;
        }

        uint32_t frame_number = 0;
        const auto frame_interval = std::chrono::milliseconds(1000 / kFps);

        VIDEOFEED_INFO("VideoFeedHost: Frame generator started with SDL_Surface buffer");

        while (running_.load()) {
            auto t0 = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(surface_mutex_);
                render_test_pattern_to_surface(surface, frame_number);
            }

            // Write RGB24 pixel buffer from SDL_Surface directly to ffmpeg pipe
            const uint8_t* ptr = static_cast<const uint8_t*>(surface->pixels);
            size_t remaining = static_cast<size_t>(kWidth * kHeight * 3);
            bool write_ok = true;

            while (remaining > 0 && running_.load()) {
                auto written = ::write(fd, ptr, remaining);
                if (written <= 0) {
                    if (written < 0 && errno == EINTR) {
                        continue;
                    }
                    VIDEOFEED_WARN_FMT("VideoFeedHost: write() returned {} (errno={}) at frame {}",
                                       written, errno, frame_number);
                    write_ok = false;
                    break;
                }
                ptr += written;
                remaining -= static_cast<size_t>(written);
            }

            if (!write_ok) {
                VIDEOFEED_INFO("VideoFeedHost: Pipe closed or broken, exiting generator loop");
                running_.store(false);
                break;
            }

            ++frame_number;

            auto elapsed = std::chrono::steady_clock::now() - t0;
            auto sleep_time = frame_interval - elapsed;
            if (sleep_time > std::chrono::milliseconds(0)) {
                std::this_thread::sleep_for(sleep_time);
            }
        }

        {
            std::lock_guard<std::mutex> lock(surface_mutex_);
            surface_ = nullptr;
            SDL_FreeSurface(surface);
        }

        VIDEOFEED_INFO_FMT("VideoFeedHost: Frame generator finished after {} frames",
                           frame_number);
    }

    /// Render test pattern and drawn text overlays directly onto the SDL_Surface
    static void render_test_pattern_to_surface(SDL_Surface* surface, uint32_t frame_number) {
        if (!surface) return;

        // Smooth background hue sweep
        const float hue = std::fmod(static_cast<float>(frame_number) / 240.0f, 1.0f);
        uint8_t bg_r, bg_g, bg_b;
        hsv_to_rgb(hue, 0.6f, 0.3f, bg_r, bg_g, bg_b);

        uint8_t* pixels = static_cast<uint8_t*>(surface->pixels);
        int pitch = surface->pitch;

        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                int idx = y * pitch + x * 3;
                pixels[idx + 0] = bg_r;
                pixels[idx + 1] = bg_g;
                pixels[idx + 2] = bg_b;
            }
        }

        // Bouncing white square (120x120 px)
        constexpr int sq = 120;
        const int range_x = kWidth - sq;
        const int range_y = kHeight - sq;
        int raw_x = static_cast<int>(frame_number * 6) % (range_x * 2);
        int raw_y = static_cast<int>(frame_number * 4) % (range_y * 2);
        int sx = raw_x < range_x ? raw_x : range_x * 2 - raw_x;
        int sy = raw_y < range_y ? raw_y : range_y * 2 - raw_y;

        SDL_Rect square_rect{sx, sy, sq, sq};
        uint32_t white_pixel = SDL_MapRGB(surface->format, 255, 255, 255);
        SDL_FillRect(surface, &square_rect, white_pixel);

        // Vertical gradient bars on side edges (8 px wide)
        for (int y = 0; y < kHeight; ++y) {
            auto v = static_cast<uint8_t>((y * 255) / kHeight);
            for (int x = 0; x < 8; ++x) {
                int idx1 = y * pitch + x * 3;
                pixels[idx1 + 0] = v; pixels[idx1 + 1] = 0; pixels[idx1 + 2] = 255 - v;
                int idx2 = y * pitch + (kWidth - 1 - x) * 3;
                pixels[idx2 + 0] = 255 - v; pixels[idx2 + 1] = v; pixels[idx2 + 2] = 0;
            }
        }

        // Draw high-definition text overlays directly onto the 1080p SDL_Surface
        draw_string_on_surface(surface, 40, 40, "ROUEN 1080p LIVE STREAM", 255, 255, 255, 5);
        draw_string_on_surface(surface, 40, 95, "24 FPS | FULL HD 1920x1080", 50, 255, 100, 3);

        auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &now_t);
#else
        localtime_r(&now_t, &tm_buf);
#endif
        std::string frame_str = std::format("FRAME: {:06d}", frame_number);
        std::string time_str = std::format("{:02d}:{:02d}:{:02d}", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

        draw_string_on_surface(surface, 40, kHeight - 70, frame_str, 255, 220, 100, 4);
        draw_string_on_surface(surface, kWidth - 360, kHeight - 70, time_str, 100, 220, 255, 4);

        // Allow active deck cards to paint their custom overlays on the video surface
        try {
            auto get_cards_fn = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
            if (get_cards_fn && *get_cards_fn) {
                auto active_cards = (*get_cards_fn)();
                for (const auto& card_ptr : active_cards) {
                    if (card_ptr) {
                        card_ptr->paint_video_surface(surface, kWidth, kHeight);
                    }
                }
            }
        } catch (...) {}
    }

    static void hsv_to_rgb(float h, float s, float v,
                           uint8_t& r, uint8_t& g, uint8_t& b) {
        float c = v * s;
        float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;
        float rf, gf, bf;

        int sector = static_cast<int>(h * 6.0f) % 6;
        switch (sector) {
            case 0: rf = c; gf = x; bf = 0; break;
            case 1: rf = x; gf = c; bf = 0; break;
            case 2: rf = 0; gf = c; bf = x; break;
            case 3: rf = 0; gf = x; bf = c; break;
            case 4: rf = x; gf = 0; bf = c; break;
            default: rf = c; gf = 0; bf = x; break;
        }

        r = static_cast<uint8_t>((rf + m) * 255.0f);
        g = static_cast<uint8_t>((gf + m) * 255.0f);
        b = static_cast<uint8_t>((bf + m) * 255.0f);
    }

    // ── member data ─────────────────────────────────────────────────
    std::atomic<bool>  running_{false};
    std::atomic<int>   port_;
    std::atomic<pid_t> ffmpeg_pid_{0};
    std::atomic<int>   write_fd_{-1};
    std::thread        generator_thread_;

    std::mutex         surface_mutex_;
    SDL_Surface*       surface_{nullptr};
    SDL_Texture*       texture_{nullptr};
};

}  // namespace rouen::hosts
