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

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <windows.h>
#include <process.h>
#endif

#include "./imgui_include.hpp"
#include "mpv_socket.hpp"
#include "platform_utils.hpp"
#include "../../external/IconsMaterialDesign.h"

struct media_player_item {
    std::string url;
    int player_pid{0};
    bool is_playing{false};
    mpv_socket_helper mpv_socket;
    std::atomic<double> position{0.0};
    std::atomic<double> duration{0.0};
    std::thread position_thread;
    std::atomic<bool> thread_running{false};
    std::mutex data_mutex;
    std::atomic<int> volume{100};

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
};

using media_player_item_map = std::unordered_map<ImGuiID, media_player_item>;

// --- Implementation ---

inline bool media_player_item::checkMediaStatus() {
    if (player_pid <= 0) return false;
    std::string command = "ps -p " + std::to_string(player_pid) + " > /dev/null 2>&1 && echo 1 || echo 0";
    FILE* pipe = popen(command.c_str(), "r");
    std::string result = "";
    if (pipe) {
        char buffer[128];
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != nullptr)
                result += buffer;
        }
        pclose(pipe);
    }
    result.erase(0, result.find_first_not_of(" \n\r\t"));
    result.erase(result.find_last_not_of(" \n\r\t") + 1);
    is_playing = (result == "1");
    return is_playing;
}

inline void media_player_item::stopMedia() {
    if (thread_running) {
        thread_running = false;
        if (position_thread.joinable()) {
            position_thread.join();
        }
    }
    mpv_socket.close_socket();
    if (player_pid > 0) {
        if (kill(player_pid, SIGTERM) == -1) {
            perror("Failed to terminate process");
        }
        player_pid = 0;
        is_playing = false;
    }
    position = 0.0;
    duration = 0.0;
}

inline void media_player_item::startPositionTracking() {
    thread_running = true;
    position_thread = std::thread([this]() {
        while (thread_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // Optionally, update position from mpv if needed, but do not call set_property here.
        }
    });
}

inline std::string media_player_item::urlDecode(const std::string& encoded) {
    std::string decoded;
    char ch;
    int j;
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
    std::string sanitized_url = urlDecode(input_url);
    // Remove unwanted characters, etc.
    return sanitized_url;
}

inline bool media_player_item::playMedia() {
    stopMedia(); // Ensure any previous media is stopped
    std::string socket_path = "/tmp/mpvsocket";
    player_pid = fork();
    if (player_pid == 0) {
        // Child process
        std::string cmd = "mpv --no-terminal --input-ipc-server=" + socket_path + " \"" + url + "\"";
        execlp("sh", "sh", "-c", cmd.c_str(), (char*)NULL);
        perror("execlp failed");
        exit(1);
    }
    // Parent process
    mpv_socket.init_socket(socket_path);
    startPositionTracking();
    is_playing = true;
    return true;
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
    if (!is_playing || !mpv_socket.is_connected()) {
        return false;
    }
    std::string seek_cmd = std::format("{{\"command\":[\"set_property\",\"playback-time\",{:.2f}],\"request_id\":3}}\n", position_seconds);
    return mpv_socket.send_command(seek_cmd);
}

inline bool media_player_item::setVolume(int new_volume) {
    if (!is_playing || !mpv_socket.is_connected()) {
        return false;
    }
    new_volume = std::clamp(new_volume, 0, 100);
    std::string cmd = std::format("{{\"command\":[\"set_property\",\"volume\",{}],\"request_id\":4}}\n", new_volume);
    bool ok = mpv_socket.send_command(cmd);
    if (ok) volume = new_volume;
    return ok;
}
