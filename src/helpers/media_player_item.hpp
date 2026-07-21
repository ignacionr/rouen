#pragma once

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
    int pipe_read_fd{-1};
    int audio_pipe_read_fd{-1};
    std::thread ffmpeg_thread;
    std::thread ffmpeg_audio_thread;
    SDL_AudioDeviceID audio_device{0};
    std::atomic<bool> ffmpeg_running{false};
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
                    const uint8_t* src = local_pixels.data();
                    uint8_t* dst = map;
                    for (int i = 0; i < kWidth * kHeight; ++i) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 255;
                        src += 3;
                        dst += 4;
                    }
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
    vu_level_l.store(0.0f);
    vu_level_r.store(0.0f);
    if (reset_sync_cb) {
        reset_sync_cb();
    }

    if (pipe_read_fd >= 0) {
        ::close(pipe_read_fd);
        pipe_read_fd = -1;
    }
    if (audio_pipe_read_fd >= 0) {
        ::close(audio_pipe_read_fd);
        audio_pipe_read_fd = -1;
    }

    if (ffmpeg_thread.joinable()) {
        ffmpeg_thread.join();
    }
    if (ffmpeg_audio_thread.joinable()) {
        ffmpeg_audio_thread.join();
    }

    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
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
    int audio_pipe_fds[2];
    if (::pipe(audio_pipe_fds) != 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return false;
    }

    pipe_read_fd = pipe_fds[0];
    const int write_fd = pipe_fds[1];

    audio_pipe_read_fd = audio_pipe_fds[0];
    const int audio_write_fd = audio_pipe_fds[1];

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, write_fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, audio_write_fd, 3);
    posix_spawn_file_actions_addclose(&file_actions, pipe_read_fd);
    posix_spawn_file_actions_addclose(&file_actions, audio_pipe_read_fd);

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
    args_str.push_back("-threads");
    args_str.push_back("4");
    args_str.push_back("-re");
    if (start_offset > 0.05) {
        args_str.push_back("-ss");
        args_str.push_back(start_opt);
    }
    args_str.push_back("-i");
    args_str.push_back(media_target);
    // Video output stream -> rawvideo rgb24 on pipe:1 (stdout)
    args_str.push_back("-map");
    args_str.push_back("0:v:0?");
    args_str.push_back("-vf");
    args_str.push_back(scale_filter);
    args_str.push_back("-r");
    args_str.push_back("30");
    args_str.push_back("-f");
    args_str.push_back("rawvideo");
    args_str.push_back("-pix_fmt");
    args_str.push_back("rgb24");
    args_str.push_back("pipe:1");
    // Stereo audio output stream -> s16le 44100Hz on pipe:3
    args_str.push_back("-map");
    args_str.push_back("0:a:0?");
    args_str.push_back("-f");
    args_str.push_back("s16le");
    args_str.push_back("-ac");
    args_str.push_back("2");
    args_str.push_back("-ar");
    args_str.push_back("44100");
    args_str.push_back("pipe:3");

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
    ::close(audio_write_fd);

    if (status != 0) {
        std::cerr << "[ERROR media_player_item] posix_spawnp failed status: " << status << std::endl;
        try { "notify"_sfn(std::format("FFmpeg player spawn error: status {}", status)); } catch (...) {}
        ::close(pipe_read_fd);
        ::close(audio_pipe_read_fd);
        pipe_read_fd = -1;
        audio_pipe_read_fd = -1;
        return false;
    }

    std::cerr << "[INFO media_player_item] FFmpeg player started PID: " << pid << " URL: " << media_target << std::endl;
    try { "notify"_sfn(std::format("FFmpeg player started (PID {})", pid)); } catch (...) {}

    player_pid = pid;
#ifndef _WIN32
    if (pid > 0) {
        ::setpriority(PRIO_PROCESS, pid, -10);
    }
#endif
    ffmpeg_running.store(true);
    is_playing = true;
    position.store(start_offset);
    if (reset_sync_cb) {
        reset_sync_cb();
    }

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

    // Spawn background reader thread to process stereo audio PCM from ffmpeg audio pipe
    ffmpeg_audio_thread = std::thread([this]() {
        constexpr size_t chunk_size = 4096;
        std::vector<uint8_t> buffer(chunk_size);

        while (ffmpeg_running.load() && audio_pipe_read_fd >= 0) {
            ssize_t bytes = ::read(audio_pipe_read_fd, buffer.data(), chunk_size);
            if (bytes <= 0) {
                if (bytes < 0 && errno == EINTR) continue;
                break;
            }

            if (is_paused.load()) {
                vu_level_l.store(0.0f);
                vu_level_r.store(0.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            std::vector<uint8_t> pcm_chunk(buffer.begin(), buffer.begin() + bytes);
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

            // Calculate stereo peak VU meter levels
            if (!pcm_chunk.empty()) {
                const int16_t* samples = reinterpret_cast<const int16_t*>(pcm_chunk.data());
                size_t frame_count = pcm_chunk.size() / (2 * sizeof(int16_t));
                int32_t max_l = 0;
                int32_t max_r = 0;
                for (size_t i = 0; i < frame_count; ++i) {
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

            // Push original content's stereo audio solely into Rouen's constant broadcast feed (not repeated locally)
            if (push_audio_cb) {
                push_audio_cb(pcm_chunk.data(), pcm_chunk.size());
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
    if (reset_sync_cb) {
        reset_sync_cb();
    }
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
