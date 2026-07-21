#pragma once

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

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <spawn.h>
extern char **environ;
#else
#include <windows.h>
#include <process.h>
#endif

#include <SDL.h>
#include "./imgui_include.hpp"
#include "texture_utils.hpp"
#include "platform_utils.hpp"
#include "config_service.hpp"
#include "process_helper.hpp"
#include "../registrar.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../../src/helpers/glaze_include.hpp"

/**
 * media_player_item — FFmpeg-based internal video decoding & frame streaming engine.
 *
 * Replaces external mpv window manipulation with an in-engine FFmpeg pipeline:
 *  - Spawns ffmpeg to decode media directly into raw RGB24 frames.
 *  - Renders video natively inside Rouen's ImGui cards using SDL_Texture.
 *  - Integrates seamlessly with Rouen's VideoFeedHost 1080p live cast stream.
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
#ifdef _WIN32
    DWORD player_pid{0};
#else
    int player_pid{0};
#endif
    bool is_playing{false};
    std::atomic<bool> is_paused{false};
    std::atomic<double> position{0.0};
    std::atomic<double> duration{0.0};
    std::atomic<int> volume{100};
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

    // FFmpeg Engine Members
    int pipe_read_fd{-1};
    std::thread ffmpeg_thread;
    std::atomic<bool> ffmpeg_running{false};
    std::mutex frame_mutex;
    std::vector<uint8_t> back_pixels;
    std::atomic<bool> new_frame_ready{false};
    SDL_Texture* video_texture{nullptr};
    std::mutex data_mutex;
    std::mutex texture_mutex;
    media_player_item() = default;
    media_player_item(const media_player_item&) = delete;
    media_player_item& operator=(const media_player_item&) = delete;
    media_player_item(media_player_item&&) = delete;
    media_player_item& operator=(media_player_item&&) = delete;
    ~media_player_item() { stopMedia(); }

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

    ImTextureID get_texture_id(SDL_Renderer* renderer = nullptr) {
        if (!renderer) {
            try {
                auto r_ptr = registrar::get<SDL_Renderer*>("main_renderer");
                if (r_ptr && *r_ptr) renderer = *r_ptr;
            } catch (...) {}
        }
        if (!renderer) return ImTextureID{};

        std::lock_guard<std::mutex> lock(texture_mutex);
        if (!video_texture) {
            video_texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_RGB24,
                SDL_TEXTUREACCESS_STREAMING,
                kWidth, kHeight
            );
        }
        if (new_frame_ready.load()) {
            std::vector<uint8_t> local_pixels;
            {
                std::lock_guard<std::mutex> clock(frame_mutex);
                local_pixels = back_pixels;
                new_frame_ready.store(false);
            }
            if (!local_pixels.empty()) {
                SDL_UpdateTexture(video_texture, nullptr, local_pixels.data(), kWidth * 3);
                has_video = true;
            }
        }
        return rouen::helpers::sdl_texture_cast(video_texture);
    }
};

using media_player_item_map = std::unordered_map<ImGuiID, std::shared_ptr<media_player_item>>;

// --- Implementation ---

inline bool media_player_item::checkMediaStatus() {
#ifdef _WIN32
    if (player_pid == 0) return false;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, player_pid);
    if (hProcess == NULL) return false;
    DWORD exitCode;
    bool isRunning = GetExitCodeProcess(hProcess, &exitCode) && (exitCode == STILL_ACTIVE);
    CloseHandle(hProcess);
    if (!isRunning) {
        is_playing = false;
        is_paused = false;
        player_pid = 0;
    }
    return isRunning;
#else
    if (player_pid <= 0) return false;
    int status;
    int res = waitpid(player_pid, &status, WNOHANG);
    if (res == player_pid || (res == -1 && errno == ECHILD)) {
        is_playing = false;
        is_paused = false;
        player_pid = 0;
        return false;
    }
    return true;
#endif
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

    if (pipe_read_fd >= 0) {
        ::close(pipe_read_fd);
        pipe_read_fd = -1;
    }

    if (ffmpeg_thread.joinable()) {
        ffmpeg_thread.join();
    }

#ifdef _WIN32
    if (player_pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, player_pid);
        if (hProcess != NULL) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
        player_pid = 0;
    }
#else
    if (player_pid > 0) {
        if (kill(player_pid, SIGTERM) != -1) {
            for (int i = 0; i < 10; ++i) {
                int res = waitpid(player_pid, nullptr, WNOHANG);
                if (res == player_pid || (res == -1 && errno == ECHILD)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            kill(player_pid, SIGKILL);
            waitpid(player_pid, nullptr, 0);
        }
        player_pid = 0;
    }
#endif

    {
        std::lock_guard<std::mutex> lock(texture_mutex);
        if (video_texture) {
            SDL_DestroyTexture(video_texture);
            video_texture = nullptr;
        }
    }

    is_playing = false;
    is_paused = false;
    has_video = false;
    position = 0.0;
    duration = 0.0;
    start_offset = 0.0;
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
            // Probe duration using ffprobe on resolved media_target
            if (duration.load() <= 0.0) {
                std::string probe_cmd = std::format(
                    "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"{}\"",
                    media_target
                );
                std::string probe_out = ProcessHelper::executeCommand(probe_cmd);
                try {
                    if (!probe_out.empty()) {
                        double dur = std::stod(probe_out);
                        if (dur > 0.0) duration.store(dur);
                    }
                } catch (...) {}
            }
        }
    }

#ifndef _WIN32
    ::signal(SIGPIPE, SIG_IGN);

    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        return false;
    }

    pipe_read_fd = pipe_fds[0];
    const int write_fd = pipe_fds[1];

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, write_fd, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, pipe_read_fd);

    // Redirect ffmpeg stderr to /tmp/rouen_player_ffmpeg.log for diagnostics
    int log_fd = ::open("/tmp/rouen_player_ffmpeg.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd >= 0) {
        posix_spawn_file_actions_adddup2(&file_actions, log_fd, STDERR_FILENO);
        posix_spawn_file_actions_addclose(&file_actions, log_fd);
    }

    std::string start_opt = std::format("{:.2f}", start_offset);
    std::string scale_filter = std::format("scale={}:{}:force_original_aspect_ratio=decrease,pad={}:{}:(ow-iw)/2:(oh-ih)/2",
                                           kWidth, kHeight, kWidth, kHeight);

    std::vector<std::string> args_str;
    args_str.push_back("ffmpeg");
    args_str.push_back("-loglevel");
    args_str.push_back("info");
    args_str.push_back("-re");
    if (start_offset > 0.05) {
        args_str.push_back("-ss");
        args_str.push_back(start_opt);
    }
    args_str.push_back("-i");
    args_str.push_back(media_target);
    args_str.push_back("-vf");
    args_str.push_back(scale_filter);
    args_str.push_back("-r");
    args_str.push_back("30");
    args_str.push_back("-f");
    args_str.push_back("rawvideo");
    args_str.push_back("-pix_fmt");
    args_str.push_back("rgb24");
    args_str.push_back("pipe:1");

    std::vector<char*> argv;
    for (auto& s : args_str) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    ::setenv("PATH",
             "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"
             ":/nix/var/nix/profiles/default/bin",
             1);

    pid_t pid;
    int status = ::posix_spawnp(&pid, "ffmpeg", &file_actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&file_actions);
    if (log_fd >= 0) {
        ::close(log_fd);
    }
    ::close(write_fd);

    if (status != 0) {
        std::cerr << "[ERROR media_player_item] posix_spawnp failed status: " << status << std::endl;
        try { "notify"_sfn(std::format("FFmpeg player spawn error: status {}", status)); } catch (...) {}
        ::close(pipe_read_fd);
        pipe_read_fd = -1;
        return false;
    }

    std::cerr << "[INFO media_player_item] FFmpeg player started PID: " << pid << " URL: " << media_target << std::endl;
    try { "notify"_sfn(std::format("FFmpeg player started (PID {})", pid)); } catch (...) {}

    player_pid = pid;
    ffmpeg_running.store(true);
    is_playing = true;
    position.store(start_offset);

    // Spawn background reader thread to process video frames from ffmpeg stdout pipe
    ffmpeg_thread = std::thread([this]() {
        const size_t frame_bytes = static_cast<size_t>(kWidth * kHeight * 3);
        std::vector<uint8_t> buffer(frame_bytes);

        while (ffmpeg_running.load() && pipe_read_fd >= 0) {
            size_t total_read = 0;
            while (total_read < frame_bytes && ffmpeg_running.load()) {
                ssize_t bytes = ::read(pipe_read_fd, buffer.data() + total_read, frame_bytes - total_read);
                if (bytes <= 0) {
                    if (bytes < 0 && errno == EINTR) continue;
                    break;
                }
                total_read += static_cast<size_t>(bytes);
            }

            if (total_read < frame_bytes) {
                break; // Stream ended or stopped
            }

            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                back_pixels = buffer;
                new_frame_ready.store(true);
                has_video = true;
            }

            if (!is_paused.load()) {
                position.store(position.load() + (1.0 / static_cast<double>(kFps)));
            }
        }
    });

    return true;
#else
    return false;
#endif
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
    if (player_pid <= 0) {
        return false;
    }
    start_offset = position_seconds;
    return playMedia();
}

inline bool media_player_item::setVolume(int new_volume) {
    volume = std::clamp(new_volume, 0, 100);
    return true;
}

inline bool media_player_item::setPaused(bool paused) {
    if (player_pid <= 0) return false;

#ifndef _WIN32
    if (paused) {
        ::kill(player_pid, SIGSTOP);
    } else {
        ::kill(player_pid, SIGCONT);
    }
    is_paused = paused;
    is_playing = !paused;
    return true;
#else
    return false;
#endif
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
