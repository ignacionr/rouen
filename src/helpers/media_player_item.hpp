#pragma once

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

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#else
#include <windows.h>
#include <process.h>
#endif

#include "./imgui_include.hpp"
#include "mpv_socket.hpp"
#include "platform_utils.hpp"
#include "config_service.hpp"
#include "../registrar.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../../src/helpers/glaze_include.hpp"

struct media_player_item {
    std::string url;
#ifdef _WIN32
    DWORD player_pid{0};
#else
    int player_pid{0};
#endif
    bool is_playing{false};
    std::atomic<bool> is_paused{false};
    mpv_socket_helper mpv_socket;
    std::atomic<double> position{0.0};
    std::atomic<double> duration{0.0};
    std::thread position_thread;
    std::atomic<bool> thread_running{false};
    std::mutex data_mutex;
    std::atomic<int> volume{100};
    bool has_video{false}; // New: track if current media has video
    double start_offset{0.0};

    media_player_item() = default;
    ~media_player_item() { stopMedia(); }

    bool checkMediaStatus();
    void stopMedia();
    void startPositionTracking();
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
};

using media_player_item_map = std::unordered_map<ImGuiID, media_player_item>;

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
    }
    return isRunning;
#else
    if (player_pid <= 0) return false;
    // check process is running
    int status;
    if (waitpid(player_pid, &status, WNOHANG) == player_pid) {
        is_playing = false;
        is_paused = false;
        return false;
    }
    return true;
#endif
}

inline void media_player_item::stopMedia() {
    // 1. Try to send quit command to mpv socket for graceful termination
    if (player_pid > 0 && mpv_socket.is_connected()) {
        std::string quit_cmd = "{\"command\":[\"quit\"]}\n";
        mpv_socket.send_command(quit_cmd);
    }

    // 2. Signal thread to stop and close socket
    if (thread_running) {
        thread_running = false;
    }
    mpv_socket.close_socket();

    // 3. Join the tracking thread
    if (position_thread.joinable()) {
        position_thread.join();
    }

#ifdef _WIN32
    if (player_pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, player_pid);
        if (hProcess != NULL) {
            if (!TerminateProcess(hProcess, 0)) {
                // Handle error - could log it
            }
            CloseHandle(hProcess);
        }
        player_pid = 0;
    }
#else
    if (player_pid > 0) {
        if (kill(player_pid, SIGTERM) != -1) {
            // Wait up to 100ms for the process to exit gracefully
            for (int i = 0; i < 10; ++i) {
                int res = waitpid(player_pid, nullptr, WNOHANG);
                if (res == player_pid || (res == -1 && errno == ECHILD)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // Send SIGKILL if it's still running
            kill(player_pid, SIGKILL);
            waitpid(player_pid, nullptr, 0); // Reap the zombie
        }
        player_pid = 0;
    }
#endif
    is_playing = false;
    is_paused = false;
    position = 0.0;
    duration = 0.0;
    start_offset = 0.0;
}

inline void media_player_item::startPositionTracking() {
    thread_running = true;
    position_thread = std::thread([this]() {
        while (thread_running) {
            if (!mpv_socket.is_connected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // Request multiple properties in one command, including video tracks
            const char* cmd = "{\"command\":[\"get_property\",\"playback-time\"], \"request_id\":1}\n"
                                "{\"command\":[\"get_property\",\"duration\"], \"request_id\":2}\n"
                                "{\"command\":[\"get_property\",\"pause\"], \"request_id\":3}\n"
                                "{\"command\":[\"get_property\",\"track-list\"], \"request_id\":10}\n";
            mpv_socket.send_command(cmd);
            char buffer[4096];
            if (mpv_socket.receive_response(buffer, sizeof(buffer))) {
                std::string response(buffer);
                std::stringstream ss(response);
                std::string line;
                while (std::getline(ss, line)) {
                    try {
                        glz::json_t resp;
                        auto ec = glz::read_json(resp, line);
                        if (ec) continue;
                        int request_id = 0;
                        if (resp.contains("request_id") && resp["request_id"].is_number()) {
                            request_id = static_cast<int>(resp["request_id"].get<double>());
                        } else {
                            continue;
                        }
                        if (!resp.contains("data")) continue;
                        auto& data = resp["data"];
                        switch (request_id) {
                            case 1:
                                if (data.is_number())
                                    position = data.get<double>();
                                break;
                            case 2:
                                if (data.is_number())
                                    duration = data.get<double>();
                                break;
                            case 3:
                                if (data.is_boolean()) {
                                    is_paused = data.get<bool>();
                                    is_playing = !is_paused.load();
                                }
                                break;
                            case 10:
                                has_video = false;
                                if (data.is_array()) {
                                    const auto& arr = data.get_array();
                                    for (const auto& track : arr) {
                                        if (track.is_object()) {
                                            const auto& obj = track.get_object();
                                            if (obj.contains("type") && obj.at("type").is_string() && obj.at("type").get<std::string>() == "video") {
                                                has_video = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                break;
                        }
                    } catch (...) { /* Ignore parse errors */ }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
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
        }
        else if (encoded[i] == '+') {
            decoded += ' ';
        }
        else {
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
    
    // Only URL decode if the URL appears to be encoded
    if (isUrlEncoded(input_url)) {
        sanitized_url = urlDecode(input_url);
    }
    
    // For HTTP URLs, ensure they're properly formatted
    if (sanitized_url.find("http://") == 0 || sanitized_url.find("https://") == 0) {
        // URL is already properly formatted for HTTP/HTTPS
        return sanitized_url;
    }
    
    // For other URLs, apply additional sanitization if needed
    return sanitized_url;
}

inline bool media_player_item::playMedia() {
    stopMedia(); // Ensure any previous media is stopped
    has_video = false;
    is_paused = false;
    
    // Validate that MPV is available
    std::string mpv_path = CONFIG_SERVICE()->get_mpv_path();
    std::string validated_path;
    bool mpv_found = rouen::platform::check_mpv_availability(validated_path);
    if (!mpv_found) {
        try { "notify"_sfn("Cannot play media: MPV not found. Please install MPV or configure MPV_PATH."); } catch (...) {}
        return false;
    }
    if (!validated_path.empty()) {
        mpv_path = validated_path;
    }

    // Sanitize the URL to handle encoding issues
    std::string sanitized_url = sanitizeURL(url);
    
#ifdef _WIN32
    // Windows implementation using named pipes instead of Unix sockets
    // Create a unique pipe name to avoid conflicts
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::string pipe_name = "\\\\.\\pipe\\mpvsocket_" + std::to_string(millis);
    
    // Add headers for better compatibility with protected content (like RT.com)
    std::string headers = "--http-header-fields=\"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" --http-header-fields=\"Referer: https://www.rt.com/\"";
    std::string extra_opts = "--cache=yes --demuxer-max-bytes=50M --demuxer-readahead-secs=20";
    std::string ytdl_opts;
    std::string cookies_browser = CONFIG_SERVICE()->get_env("ROUEN_COOKIES_BROWSER");
    if (!cookies_browser.empty()) {
        ytdl_opts = " --ytdl-raw-options=cookies-from-browser=" + cookies_browser;
    }
    std::string start_opt;
    if (start_offset > 0.0) {
        start_opt = " --start=" + std::to_string(start_offset);
    }
    std::string cmd = "\"" + mpv_path + "\" --no-terminal --input-ipc-server=" + pipe_name + " " + headers + " " + extra_opts + start_opt + ytdl_opts + " \"" + sanitized_url + "\"";
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Create the process
    if (CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        player_pid = pi.dwProcessId;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        mpv_socket.init_socket(pipe_name);
        startPositionTracking();
        is_playing = true;
        return true;
    } else {
        try { "notify"_sfn("Failed to start MPV process."); } catch (...) {}
        return false;
    }
#else
    // Unix implementation
    // Create a unique socket path to avoid conflicts between multiple instances
    std::string socket_path = mpv_socket.create_socket_path();
    
    player_pid = fork();
    if (player_pid == 0) {
        // Child process
        
        // 1. Update PATH to ensure opt/homebrew/bin and usr/local/bin are included
        std::string new_path = "/opt/homebrew/bin:/usr/local/bin";
        char* current_path = getenv("PATH");
        if (current_path) {
            new_path = std::string(current_path) + ":" + new_path;
        }
        setenv("PATH", new_path.c_str(), 1);
        
        // 2. Put the process in its own session
        setsid();
        
        // 3. Build arguments for mpv
        std::vector<std::string> args_str;
        args_str.push_back(mpv_path);
        args_str.push_back("--no-terminal");
        args_str.push_back("--log-file=/tmp/rouen_mpv_err.log");
        args_str.push_back("--input-ipc-server=" + socket_path);
        args_str.push_back("--http-header-fields=User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        args_str.push_back("--http-header-fields=Referer: https://www.rt.com/");
        args_str.push_back("--cache=yes");
        args_str.push_back("--demuxer-max-bytes=50M");
        args_str.push_back("--demuxer-readahead-secs=20");
        
        std::string cookies_browser = CONFIG_SERVICE()->get_env("ROUEN_COOKIES_BROWSER");
        if (!cookies_browser.empty()) {
            args_str.push_back("--ytdl-raw-options=cookies-from-browser=" + cookies_browser);
        }
        if (start_offset > 0.0) {
            args_str.push_back("--start=" + std::to_string(start_offset));
        }
        args_str.push_back(sanitized_url);
        
        // Convert to char* vector for execvp
        std::vector<const char*> args;
        for (const auto& arg : args_str) {
            args.push_back(arg.c_str());
        }
        args.push_back(nullptr);
        
        // 4. Execute mpv
        execvp(mpv_path.c_str(), const_cast<char* const*>(args.data()));
        perror("execvp failed to launch mpv");
        exit(1);
    }
    // Parent process
    if (!mpv_socket.init_socket(socket_path)) {
        try { "notify"_sfn("Failed to connect to MPV player socket."); } catch (...) {}
        stopMedia();
        return false;
    }
    startPositionTracking();
    is_playing = true;
    return player_pid > 0;
#endif
}

inline std::string media_player_item::formatTime(double seconds) const {
    int hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, secs);
    return std::string(buffer);
}

inline bool media_player_item::seekTo(double position_seconds) {
    if (player_pid <= 0 || !mpv_socket.is_connected()) {
        return false;
    }
    std::string seek_cmd = std::format("{{\"command\":[\"set_property\",\"playback-time\",{:.2f}],\"request_id\":3}}\n", position_seconds);
    return mpv_socket.send_command(seek_cmd);
}

inline bool media_player_item::setVolume(int new_volume) {
    if (player_pid <= 0 || !mpv_socket.is_connected()) {
        return false;
    }
    // Clamp volume to valid range
    if (new_volume < 0) new_volume = 0;
    if (new_volume > 100) new_volume = 100;
    
    std::string cmd = std::format("{{\"command\":[\"set_property\",\"volume\",{}],\"request_id\":4}}\n", new_volume);
    bool ok = mpv_socket.send_command(cmd);
    if (ok) volume = new_volume;
    return ok;
}

inline bool media_player_item::setPaused(bool paused) {
    if (player_pid <= 0 || !mpv_socket.is_connected()) {
        return false;
    }

    std::string cmd = std::format("{{\"command\":[\"set_property\",\"pause\",{}],\"request_id\":{}}}\n",
                                  paused ? "true" : "false",
                                  paused ? 5 : 6);
    bool ok = mpv_socket.send_command(cmd);
    if (ok) {
        is_paused = paused;
        is_playing = !paused;
    }
    return ok;
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
