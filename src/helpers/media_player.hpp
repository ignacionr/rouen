#pragma once

#include <string>
#include <unordered_map>

#include <imgui/imgui.h>

struct media_player {
    struct item {
        std::string url;
        int player_pid;
        bool is_playing{false};

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
            if (player_pid > 0) {
                // Kill the process using the stored PID
                std::string command = "kill " + std::to_string(player_pid) + " 2>/dev/null || true";
                std::system(command.c_str());
                player_pid = 0;
                is_playing = false;
            }
        }
        bool playMedia() {
            try {
                // Stop any current playback
                stopMedia();
                
                // Build the command to play the media
                // Use mpv for audio/video content
                std::string command = "nohup mpv --no-video --really-quiet \"" + url + "\" > /dev/null 2>&1 & echo $!";
                
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
    
    };

    using item_map = std::unordered_map<ImGuiID, item>;

    static item_map & items() {
        static item_map items_;
        return items_;
    }

    static void player(std::string_view url, auto info_color) {
        auto &item {items()[ImGui::GetItemID()]};
        item.url = url;
        // Check if media is currently playing
        if (item.player_pid > 0) {
            item.checkMediaStatus(); // Update playback status
        }
        
        if (item.is_playing) {
            ImGui::TextColored(info_color, "Playing...");
            if (ImGui::Button("Stop Playback")) {
                item.stopMedia();
            }
        } else {
            // Play button
            if (ImGui::Button("Play")) {
                item.playMedia();
            }
            // Display enclosure info
            ImGui::TextWrapped("Media URL: %s", item.url.c_str());
        }
    }
};