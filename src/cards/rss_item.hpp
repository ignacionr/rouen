#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <imgui/imgui.h>
#include <memory>
#include <string>
#include <vector>

#include "card.hpp"
#include "rss.hpp"
#include "../hosts/rss_host.hpp"

namespace rouen::cards {

// Card to display a single RSS item
class rss_item : public card {
public:
    rss_item(const std::string& item_info) {
        // Set custom colors
        colors[0] = {0.3f, 0.7f, 0.5f, 1.0f}; // Green primary color
        colors[1] = {0.4f, 0.8f, 0.6f, 0.7f}; // Lighter green secondary color
        
        get_color(2, ImVec4(0.6f, 1.0f, 0.8f, 1.0f)); // Light green for titles
        get_color(3, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // Light gray for descriptions
        get_color(4, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Green for playing status
        get_color(5, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red for stop/error status
        
        // Parse the feed_id,link info
        size_t comma_pos = item_info.find(',');
        if (comma_pos != std::string::npos) {
            try {
                feed_id = std::stoll(item_info.substr(0, comma_pos));
                item_link = item_info.substr(comma_pos + 1);
                
                // Get the RSS host controller
                rss_host = rss::getHost();
                
                // Load the item
                loadItem();
            } catch (...) {
                // Handle parsing error
                name("RSS Item");
            }
        } else {
            name("RSS Item");
        }
        
        // Adjust size to be larger for content display
        size = {600.0f, 400.0f};
        
        // Set refresh rate to check media playback status
        requested_fps = 1;
    }
    
    ~rss_item() override {
        // Make sure to stop playback when card is closed
        stopMedia();
    }
    
    // Stop media playback
    void stopMedia() {
        if (player_pid > 0) {
            // Kill the process using the stored PID
            std::string command = "kill " + std::to_string(player_pid) + " 2>/dev/null || true";
            std::system(command.c_str());
            player_pid = 0;
            is_playing = false;
        }
    }
    
    // Play media enclosure
    bool playMedia() {
        if (item.enclosure.empty()) return false;
        
        try {
            // Get the enclosure URL
            const std::string& url = item.enclosure;
            
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
    
    // Check if the media player process is still running
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
    
    void loadItem() {
        if (feed_id < 0 || item_link.empty() || !rss_host) return;
        
        // Get the item from the controller
        auto found_item = rss_host->getFeedItem(feed_id, item_link);
        if (!found_item) return;
        
        // Store the item
        item = *found_item;
        
        // Update the card title
        name(std::format("Article: {}", item.title));
        
        item_loaded = true;
    }
    
    bool render() override {
        return render_window([this]() {
            if (!item_loaded) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load item");
                return;
            }
            
            // Title at top
            ImGui::TextColored(colors[2], "%s", item.title.c_str());
            ImGui::Separator();
            
            // Original URL link
            ImGui::TextColored(colors[4], "Source: ");
            ImGui::SameLine();
            if (ImGui::SmallButton("Open in Browser")) {
                // Open URL in browser
                auto command = std::format("xdg-open \"{}\" &", item_link);
                std::system(command.c_str());
            }
            
            // Date and time
            auto time = std::chrono::system_clock::to_time_t(item.publish_date);
            std::tm* tm = std::localtime(&time);
            char date_str[64];
            std::strftime(date_str, sizeof(date_str), "%A, %d %B %Y %H:%M", tm);
            ImGui::TextColored(colors[3], "Published: %s", date_str);
            
            // Media enclosure playback controls
            if (!item.enclosure.empty()) {
                ImGui::Separator();
                ImGui::TextColored(colors[2], "Media:");
                
                // Check if media is currently playing
                if (player_pid > 0) {
                    checkMediaStatus(); // Update playback status
                }
                
                if (is_playing) {
                    ImGui::TextColored(colors[4], "Playing...");
                    if (ImGui::Button("Stop Playback")) {
                        stopMedia();
                    }
                } else {
                    // Play button
                    if (ImGui::Button("Play")) {
                        playMedia();
                    }
                    // Display enclosure info
                    ImGui::TextWrapped("Media URL: %s", item.enclosure.c_str());
                }
                
                ImGui::Separator();
            }
            
            // Content in a scrollable area
            if (ImGui::BeginChild("ContentScrollArea", ImVec2(0, 0), true)) {
                // Use description as content
                std::string content = item.description;
                
                if (!content.empty()) {
                    // Very basic HTML tag removal
                    // In a real implementation, you'd want a proper HTML parser/renderer
                    std::string plainText = content;
                    
                    // Remove HTML tags (very basic approach)
                    size_t tagStart = 0;
                    while ((tagStart = plainText.find('<', tagStart)) != std::string::npos) {
                        size_t tagEnd = plainText.find('>', tagStart);
                        if (tagEnd != std::string::npos) {
                            plainText.erase(tagStart, tagEnd - tagStart + 1);
                        } else {
                            break;
                        }
                    }
                    
                    // Replace common HTML entities
                    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
                        size_t pos = 0;
                        while ((pos = str.find(from, pos)) != std::string::npos) {
                            str.replace(pos, from.length(), to);
                            pos += to.length();
                        }
                    };
                    
                    replaceAll(plainText, "&nbsp;", " ");
                    replaceAll(plainText, "&lt;", "<");
                    replaceAll(plainText, "&gt;", ">");
                    replaceAll(plainText, "&amp;", "&");
                    replaceAll(plainText, "&quot;", "\"");
                    
                    // Display the text with wrapping
                    ImGui::TextWrapped("%s", plainText.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No content available");
                }
                
                ImGui::EndChild();
            }
        });
    }
    
private:
    long long feed_id = -1;
    std::string item_link;
    bool item_loaded = false;
    std::shared_ptr<hosts::RSSHost> rss_host;
    hosts::RSSHost::FeedItem item; // Use the FeedItem from the controller
    
    // Media playback state
    int player_pid = 0;     // Process ID of the media player
    bool is_playing = false; // Flag to track whether media is currently playing
};

} // namespace rouen::cards