#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <signal.h>
#include <unistd.h>

#include <imgui/imgui.h>
#include "../registrar.hpp"
#include "mpv_socket.hpp"

struct media_player {
    struct item {
        std::string url;
        int player_pid{0};
        bool is_playing{false};
        mpv_socket_helper mpv_socket;
        std::atomic<double> position{0.0};
        std::atomic<double> duration{0.0};
        std::thread position_thread;
        std::atomic<bool> thread_running{false};
        std::mutex data_mutex;  // Add mutex to protect data access

        item() = default;
        
        ~item() {
            stopMedia();
        }

        bool checkMediaStatus() {
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
            
            // Trim whitespace
            result.erase(0, result.find_first_not_of(" \n\r\t"));
            result.erase(result.find_last_not_of(" \n\r\t") + 1);
            
            is_playing = (result == "1");
            return is_playing;
        }
        
        void stopMedia() {
            // Stop the position tracking thread if it's running
            if (thread_running) {
                thread_running = false;
                if (position_thread.joinable()) {
                    position_thread.join();
                }
            }
            
            // Close socket via helper
            mpv_socket.close_socket();
            
            if (player_pid > 0) {
                // Kill the process using the stored PID
                if (kill(player_pid, SIGTERM) == -1) {
                    perror("Failed to terminate process");
                }
                player_pid = 0;
                is_playing = false;
            }
            
            // Reset playback info
            position = 0.0;
            duration = 0.0;
        }
        
        // Start a thread to periodically update position information
        void startPositionTracking() {
            // Stop any existing thread
            if (thread_running) {
                thread_running = false;
                if (position_thread.joinable()) {
                    position_thread.join();
                }
            }
            
            thread_running = true;
            position_thread = std::thread([this]() {
                // Keep track of consecutive errors
                int consecutive_errors = 0;
                
                // Store persistent duration value - we only need to get it once reliably
                double persistent_duration = 0.0;
                bool duration_initialized = false;
                
                while (thread_running) {
                    bool is_running = false;
                    
                    // First check if the player is still running
                    if (player_pid > 0) {
                        is_running = checkMediaStatus();
                        if (!is_running) {
                            // Player has stopped
                            mpv_socket.close_socket();
                            is_playing = false;
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            continue;
                        }
                    } else {
                        // No valid PID, can't continue
                        is_playing = false;
                        mpv_socket.close_socket();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        continue;
                    }
                    
                    // Only proceed if socket is valid
                    if (mpv_socket.is_connected()) {
                        // First, if duration is not initialized yet, get it
                        if (!duration_initialized || persistent_duration <= 0) {
                            // Request duration only
                            if (mpv_socket.send_command("{\"command\":[\"get_property\",\"duration\"],\"request_id\":2}\n")) {
                                // Wait for response
                                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                
                                // Get the duration response
                                char buffer[4096];
                                memset(buffer, 0, sizeof(buffer));
                                
                                if (mpv_socket.receive_response(buffer, sizeof(buffer))) {
                                    std::string response(buffer);
                                    
                                    // Only look for duration response
                                    if (response.find("\"request_id\":2") != std::string::npos) {
                                        size_t pos = response.find("\"data\":");
                                        if (pos != std::string::npos) {
                                            std::string value = response.substr(pos + 7);
                                            size_t end = value.find_first_of(",}");
                                            if (end != std::string::npos) {
                                                try {
                                                    double dur_value = std::stod(value.substr(0, end));
                                                    if (dur_value > 0) {
                                                        persistent_duration = dur_value;
                                                        duration_initialized = true;
                                                        
                                                        // Update shared duration once we have a good value
                                                        std::lock_guard<std::mutex> lock(data_mutex);
                                                        duration = persistent_duration;
                                                    }
                                                } catch (...) {
                                                    // Error parsing, ignore
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            
                            // If we couldn't get the duration, wait and try again
                            if (!duration_initialized) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                continue;
                            }
                        }
                        
                        // Now separately request playback position
                        if (mpv_socket.send_command("{\"command\":[\"get_property\",\"playback-time\"],\"request_id\":1}\n")) {
                            // Wait for response
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            
                            // Process position response
                            char buffer[4096];
                            memset(buffer, 0, sizeof(buffer));
                            
                            double current_position = 0.0;
                            bool position_updated = false;
                            
                            if (mpv_socket.receive_response(buffer, sizeof(buffer))) {
                                std::string response(buffer);
                                
                                // Check for playback end events
                                if (response.find("\"event\":\"end-file\"") != std::string::npos || 
                                    response.find("\"event\":\"idle\"") != std::string::npos) {
                                    // Playback has ended
                                    is_playing = false;
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                                
                                // Look for position response only
                                if (response.find("\"request_id\":1") != std::string::npos) {
                                    size_t pos = response.find("\"data\":");
                                    if (pos != std::string::npos) {
                                        std::string value = response.substr(pos + 7);
                                        size_t end = value.find_first_of(",}");
                                        if (end != std::string::npos) {
                                            try {
                                                current_position = std::stod(value.substr(0, end));
                                                position_updated = true;
                                            } catch (...) {
                                                // Error parsing, ignore
                                            }
                                        }
                                    }
                                }
                            } else {
                                // Failed to receive response
                                consecutive_errors++;
                                if (consecutive_errors > 5) {
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                            }
                            
                            // Update position if we got a valid value
                            if (position_updated) {
                                std::lock_guard<std::mutex> lock(data_mutex);
                                position = current_position;
                                
                                // Always reassert the persistent duration to make sure it doesn't get overwritten
                                if (duration_initialized && persistent_duration > 0) {
                                    duration = persistent_duration;
                                }
                                
                                // Check if we've reached the end of playback
                                if (current_position >= persistent_duration - 0.5 && persistent_duration > 0) {
                                    is_playing = false;
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                            } else {
                                consecutive_errors++;
                                if (consecutive_errors > 5) {
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                            }
                        } else {
                            // Failed to send command
                            consecutive_errors++;
                            if (consecutive_errors > 5) {
                                mpv_socket.close_socket();
                            }
                        }
                    }
                    
                    // Reset error counter on success
                    if (mpv_socket.is_connected()) {
                        consecutive_errors = 0;
                    }
                    
                    // Sleep between updates
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            });
        }
        
        bool playMedia() {
            try {
                // Stop any current playback
                stopMedia();
                
                // Create a unique socket path using the helper
                std::string socket_path = mpv_socket.create_socket_path();
                
                // Build the command to play the media
                // Use mpv with JSON IPC socket for position reporting
                std::string command = "nohup mpv --no-video --really-quiet --input-ipc-server=" + socket_path + " \"" + url + "\" > /dev/null 2>&1 & echo $!";
                
                // Execute the command and get the PID
                std::string result = "";
                FILE* pipe = popen(command.c_str(), "r");
                if (pipe) {
                    char buffer[128];
                    while (!feof(pipe)) {
                        if (fgets(buffer, 128, pipe) != nullptr)
                            result += buffer;
                    }
                    pclose(pipe);
                }
                
                // Trim whitespace
                result.erase(0, result.find_first_not_of(" \n\r\t"));
                result.erase(result.find_last_not_of(" \n\r\t") + 1);
                
                // Store the process ID for later termination
                try {
                    player_pid = std::stoi(result);
                    is_playing = true;
                    
                    // Initialize the socket connection and start position tracking
                    if (mpv_socket.init_socket(socket_path)) {
                        startPositionTracking();
                    }
                    
                    return true;
                } catch (...) {
                    player_pid = 0;
                    is_playing = false;
                    return false;
                }
            } catch (const std::exception& e) {
                player_pid = 0;
                is_playing = false;
                return false;
            }
        }
        
        // Format time in MM:SS format
        std::string formatTime(double seconds) {
            int mins = static_cast<int>(seconds) / 60;
            int secs = static_cast<int>(seconds) % 60;
            return std::format("{:02d}:{:02d}", mins, secs);
        }
    
    };

    using item_map = std::unordered_map<ImGuiID, item>;

    static item_map & items() {
        static item_map items_;
        return items_;
    }

    static void stopAll() {
        for (auto &[k,v]: items()) {
            v.stopMedia();
        }
    }

    static void player(std::string_view url, auto info_color) noexcept {
        try {
            auto &item {items()[ImGui::GetID(url.data())]};
            item.url = url;
            // Check if media is currently playing
            if (item.player_pid > 0) {
                item.checkMediaStatus(); // Update playback status
            }
            
            if (item.is_playing) {
                // Get safe copies of position and duration values with mutex protection
                double current_pos, current_dur;
                {
                    std::lock_guard<std::mutex> lock(item.data_mutex);
                    current_pos = item.position;
                    current_dur = item.duration;
                }
                
                // Show playback position if available
                if (current_dur > 0) {
                    ImGui::TextColored(info_color, "Playing: %s / %s", 
                        item.formatTime(current_pos).c_str(),
                        item.formatTime(current_dur).c_str());
                    
                    // Draw a progress bar with safe calculation
                    float progress = current_pos > 0 && current_dur > 0 ? 
                        static_cast<float>(current_pos / current_dur) : 0.0f;
                    
                    // Clamp progress to 0.0-1.0 range
                    progress = std::max(0.0f, std::min(1.0f, progress));
                    ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
                } else {
                    ImGui::TextColored(info_color, "Playing: %s", 
                        item.formatTime(current_pos).c_str());
                    ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Loading...");
                }
                
                if (ImGui::Button("Stop Playback")) {
                    item.stopMedia();
                }
            } else {
                // Play button
                if (ImGui::Button("Play")) {
                    stopAll();
                    item.playMedia();
                }
            }
        }
        catch (const std::exception& e) {
            "notify"_sfn(e.what());
        }
    }
};