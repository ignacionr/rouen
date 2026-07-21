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
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../helpers/debug.hpp"
#include "../registrar.hpp"

namespace rouen::hosts {

/**
 * VideoFeedHost — streams a synthetic test-pattern video through ffmpeg.
 *
 * Architecture:
 *   Rouen (this host)
 *       │  writes raw RGB24 frames to a pipe
 *       ▼
 *   ffmpeg child process
 *       │  reads from pipe, encodes, serves via built-in HTTP listener
 *       ▼
 *   HTTP endpoint  →  mpv / VLC / browser
 *
 * Default endpoint:  http://127.0.0.1:8889
 *   Connect with:  mpv http://127.0.0.1:8889
 */
class VideoFeedHost {
public:
    static constexpr int kWidth  = 160;
    static constexpr int kHeight = 120;
    static constexpr int kFps    = 24;
    static constexpr int kDefaultPort = 8889;

    VideoFeedHost()
        : port_(kDefaultPort) {
        VIDEOFEED_INFO("VideoFeedHost: Initialized");
    }

    ~VideoFeedHost() {
        stop();
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

            // ffmpeg HTTP listen command:
            //   -f rawvideo -pixel_format rgb24 -video_size WxH -framerate F -i pipe:0
            //   -c:v libx264 -preset ultrafast -tune zerolatency
            //   -pix_fmt yuv420p -g 4
            //   -f mpegts -listen 1 http://127.0.0.1:PORT
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

        // Launch the frame-generation thread
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
        // Close the write end of the pipe so generator thread & ffmpeg notice EOF
        int fd = write_fd_.exchange(-1);
        if (fd >= 0) {
            ::close(fd);
        }

        // Wait for generator thread to terminate
        if (generator_thread_.joinable()) {
            generator_thread_.join();
        }

        // Terminate the ffmpeg child process
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
        const size_t frame_bytes = static_cast<size_t>(kWidth * kHeight * 3);
        std::vector<uint8_t> frame(frame_bytes);
        uint32_t frame_number = 0;

        const auto frame_interval = std::chrono::milliseconds(1000 / kFps);

        VIDEOFEED_INFO("VideoFeedHost: Frame generator started");

        while (running_.load()) {
            auto t0 = std::chrono::steady_clock::now();

            render_test_pattern(frame.data(), frame_number);

            // Write raw frame to pipe
            const uint8_t* ptr = frame.data();
            size_t remaining = frame_bytes;
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

        VIDEOFEED_INFO_FMT("VideoFeedHost: Frame generator finished after {} frames",
                           frame_number);
    }

    /// Render a recognizable test pattern into an RGB24 buffer.
    static void render_test_pattern(uint8_t* rgb, uint32_t frame_number) {
        // Background: smoothly cycling hue (rainbow sweep every ~10 seconds at 24fps)
        const float hue = std::fmod(static_cast<float>(frame_number) / 240.0f, 1.0f);
        uint8_t bg_r, bg_g, bg_b;
        hsv_to_rgb(hue, 0.6f, 0.3f, bg_r, bg_g, bg_b);

        for (int i = 0; i < kWidth * kHeight; ++i) {
            rgb[i * 3 + 0] = bg_r;
            rgb[i * 3 + 1] = bg_g;
            rgb[i * 3 + 2] = bg_b;
        }

        // Bouncing white square (16x16 px)
        constexpr int sq = 16;
        const int range_x = kWidth  - sq;
        const int range_y = kHeight - sq;
        int raw_x = static_cast<int>(frame_number) % (range_x * 2);
        int raw_y = static_cast<int>(frame_number * 2) % (range_y * 2);
        int sx = raw_x < range_x ? raw_x : range_x * 2 - raw_x;
        int sy = raw_y < range_y ? raw_y : range_y * 2 - raw_y;

        for (int dy = 0; dy < sq; ++dy) {
            for (int dx = 0; dx < sq; ++dx) {
                int px = sx + dx;
                int py = sy + dy;
                if (px >= 0 && px < kWidth && py >= 0 && py < kHeight) {
                    int idx = (py * kWidth + px) * 3;
                    rgb[idx + 0] = 255;
                    rgb[idx + 1] = 255;
                    rgb[idx + 2] = 255;
                }
            }
        }

        // Frame counter binary indicator bar (top 4 rows)
        for (int bit = 0; bit < 16 && bit * 10 < kWidth; ++bit) {
            bool on = (frame_number >> bit) & 1;
            uint8_t val = on ? 255 : 40;
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 9; ++dx) {
                    int px = bit * 10 + dx;
                    int py = dy;
                    if (px < kWidth) {
                        int idx = (py * kWidth + px) * 3;
                        rgb[idx + 0] = val;
                        rgb[idx + 1] = val;
                        rgb[idx + 2] = val;
                    }
                }
            }
        }

        // Vertical gradient bars on left and right edges
        for (int y = 0; y < kHeight; ++y) {
            auto v = static_cast<uint8_t>((y * 255) / kHeight);
            for (int x = 0; x < 2; ++x) {
                int idx = (y * kWidth + x) * 3;
                rgb[idx + 0] = v;
                rgb[idx + 1] = 0;
                rgb[idx + 2] = 255 - v;
            }
            for (int x = kWidth - 2; x < kWidth; ++x) {
                int idx = (y * kWidth + x) * 3;
                rgb[idx + 0] = 255 - v;
                rgb[idx + 1] = v;
                rgb[idx + 2] = 0;
            }
        }
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
};

}  // namespace rouen::hosts
