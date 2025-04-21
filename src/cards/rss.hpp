#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <imgui/imgui.h>
#include <memory>
#include <string>
#include <vector>

#include "card.hpp"
#include "../helpers/fetch.hpp"
#include "../hosts/rss_host.hpp"
#include "../models/rss/feed.hpp"
#include "../registrar.hpp"

namespace rouen::cards {

// Main RSS card that displays all feeds
class rss : public card {
public:
    rss() {
        // Set custom colors for the RSS card
        colors[0] = {0.8f, 0.3f, 0.3f, 1.0f}; // Red primary color
        colors[1] = {0.9f, 0.4f, 0.4f, 0.7f}; // Lighter red secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(1.0f, 0.6f, 0.6f, 1.0f)); // Light red for titles
        get_color(3, ImVec4(0.4f, 0.8f, 0.4f, 1.0f)); // Green for active/positive elements
        get_color(4, ImVec4(0.8f, 0.8f, 0.3f, 1.0f)); // Yellow for warnings/highlights
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        
        name("RSS Reader");
        requested_fps = 1;  // Update once per second
        size = {400.0f, 500.0f};
        
        // Initialize the RSS host controller with a system runner
        rss_host = std::make_unique<hosts::RSSHost>([](std::string_view cmd) -> std::string {
            // Simple system runner implementation
            return ""; // Not using system commands in this implementation
        });
    }
    
    ~rss() override = default;
    
    bool render() override {
        return render_window([this]() {
            static char url_buffer[512] = "";
            
            // Add a new feed section
            ImGui::TextColored(colors[2], "Add RSS Feed:");
            ImGui::PushItemWidth(-1);
            bool url_entered = ImGui::InputText("##url", url_buffer, sizeof(url_buffer), 
                ImGuiInputTextFlags_EnterReturnsTrue);
            
            // Show placeholder text when input is empty
            if (url_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Enter RSS feed URL..."
                );
            }
            ImGui::PopItemWidth();
            
            ImGui::SameLine();
            bool add_clicked = ImGui::Button("Add");
            
            if ((url_entered || add_clicked) && url_buffer[0] != '\0') {
                // Add the feed
                if (addFeed(url_buffer)) {
                    // Clear the input field on success
                    url_buffer[0] = '\0';
                }
            }
            
            ImGui::Separator();
            
            // Feeds section title
            ImGui::TextColored(colors[2], "Your RSS Feeds:");
            
            // Create scrollable area for feeds
            if (ImGui::BeginChild("FeedsScrollArea", ImVec2(0, 0), true)) {
                auto feeds = rss_host->feeds();
                if (feeds.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No feeds added yet");
                } else {
                    // Display all feeds
                    for (const auto& feed : feeds) {
                        ImGui::PushID(feed->source_link.c_str());
                        
                        ImGui::BeginGroup();
                        
                        // Feed title with truncation if needed
                        std::string title = feed->feed_title;
                        if (title.empty()) {
                            title = feed->source_link;
                        }
                        
                        // Limit title length and add ellipsis if too long
                        if (title.length() > 40) {
                            title = title.substr(0, 37) + "...";
                        }
                        
                        if (ImGui::Selectable(title.c_str(), false)) {
                            // Open feed items in a new card
                            std::string feed_uri = std::format("rss-feed:{}", feed->repo_id);
                            "create_card"_sfn(feed_uri);
                        }
                        
                        // Item count
                        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                        ImGui::TextColored(
                            ImVec4(colors[5].x, colors[5].y, colors[5].z, colors[5].w),
                            "%zu items", feed->items.size()
                        );
                        
                        // Delete button
                        ImGui::SameLine(ImGui::GetWindowWidth() - 30);
                        if (ImGui::SmallButton("×")) {
                            feeds_to_delete.push_back(feed->source_link);
                        }
                        
                        ImGui::EndGroup();
                        
                        ImGui::PopID();
                    }
                    
                    // Process deletion requests
                    for (const auto& url : feeds_to_delete) {
                        rss_host->deleteFeed(url);
                    }
                    feeds_to_delete.clear();
                }
                
                ImGui::EndChild();
            }
        });
    }
    
    // Add a new feed by URL
    bool addFeed(const std::string& url) {
        try {
            // Use the RSSHost controller to add the feed
            return rss_host->addFeed(url);
        } catch (const std::exception& e) {
            // Handle error (could show in UI)
            return false;
        }
    }
    
    // Get access to the RSS host controller (needed for other RSS card classes)
    static std::shared_ptr<hosts::RSSHost> getHost() {
        static std::shared_ptr<hosts::RSSHost> shared_host = nullptr;
        
        if (!shared_host) {
            shared_host = std::make_shared<hosts::RSSHost>([](std::string_view cmd) -> std::string {
                return ""; // Not using system commands in this implementation
            });
        }
        
        return shared_host;
    }
    
private:
    std::unique_ptr<hosts::RSSHost> rss_host;
    std::vector<std::string> feeds_to_delete;
};

// Card to display items from a specific feed
class rss_feed : public card {
public:
    rss_feed(const std::string& feed_id_str) {
        // Set custom colors for the feed card
        colors[0] = {0.3f, 0.5f, 0.8f, 1.0f}; // Blue primary color
        colors[1] = {0.4f, 0.6f, 0.9f, 0.7f}; // Lighter blue secondary color
        
        // Additional colors
        get_color(2, ImVec4(0.6f, 0.8f, 1.0f, 1.0f)); // Light blue for titles
        get_color(3, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)); // Light gray for descriptions
        get_color(4, ImVec4(0.6f, 0.9f, 0.6f, 1.0f)); // Light green for links
        
        // Parse the feed ID
        try {
            feed_id = std::stoll(feed_id_str);
        } catch (...) {
            feed_id = -1;
        }
        
        // Initialize with a temporary name
        name("Feed Items");
        
        // Get the RSS host controller
        rss_host = rss::getHost();
        
        // Load feed information and items
        loadFeed();
    }
    
    ~rss_feed() override = default;
    
    void loadFeed() {
        if (feed_id < 0 || !rss_host) return;
        
        // Get feed information
        auto feed_info = rss_host->getFeedInfo(feed_id);
        if (!feed_info) return;
        
        // Update feed details
        feed_title = feed_info->title;
        feed_url = feed_info->url;
        
        // Update the card title with the feed title
        name(std::format("Feed: {}", feed_title));
        
        // Load feed items
        items = rss_host->getFeedItems(feed_id);
    }
    
    bool render() override {
        return render_window([this]() {
            // Feed title at top
            ImGui::TextColored(colors[2], "%s", feed_title.c_str());
            
            // Original URL
            ImGui::SameLine(ImGui::GetWindowWidth() - 80);
            if (ImGui::SmallButton("URL")) {
                // Open URL in browser
                auto command = std::format("xdg-open \"{}\" &", feed_url);
                std::system(command.c_str());
            }
            
            ImGui::Separator();
            
            // Items in a scrollable area
            if (ImGui::BeginChild("ItemsScrollArea", ImVec2(0, 0), true)) {
                if (items.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No items in this feed");
                } else {
                    for (const auto& item : items) {
                        ImGui::PushID(item.link.c_str());
                        
                        // Item container with spacing
                        ImGui::BeginGroup();
                        
                        // Title (selectable to open item)
                        if (ImGui::Selectable(item.title.c_str(), false)) {
                            // Open item in a new card
                            std::string item_uri = std::format("rss-item:{},{}", feed_id, item.link);
                            "create_card"_sfn(item_uri);
                        }
                        
                        // Date - format as "Day Month Year Hour:Minute"
                        auto time = std::chrono::system_clock::to_time_t(item.publish_date);
                        std::tm* tm = std::localtime(&time);
                        char date_str[64];
                        std::strftime(date_str, sizeof(date_str), "%d %b %Y %H:%M", tm);
                        
                        ImGui::TextColored(colors[3], "%s", date_str);
                        
                        // Truncated description (if available)
                        if (!item.description.empty()) {
                            std::string desc = item.description;
                            // Strip HTML tags (basic approach)
                            desc.erase(std::remove_if(desc.begin(), desc.end(), 
                                [](char c) { return c == '<' || c == '>'; }), desc.end());
                            
                            // Limit length
                            if (desc.length() > 100) {
                                desc = desc.substr(0, 97) + "...";
                            }
                            
                            ImGui::TextWrapped("%s", desc.c_str());
                        }
                        
                        ImGui::EndGroup();
                        ImGui::Separator();
                        
                        ImGui::PopID();
                    }
                }
                
                ImGui::EndChild();
            }
        });
    }
    
private:
    long long feed_id = -1;
    std::string feed_title;
    std::string feed_url;
    std::shared_ptr<hosts::RSSHost> rss_host;
    std::vector<hosts::RSSHost::FeedItem> items;
};

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
        // Stop any existing playback first
        stopMedia();
        
        if (item.enclosure.empty()) {
            return false;
        }
        
        try {
            // Use system() with nohup to properly detach the process
            // mpv can handle various media types including audio and video
            std::string command = "nohup mpv --no-video \"" + item.enclosure + "\" > /dev/null 2>&1 & echo $!";
            
            // Execute command and get process ID
            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe) {
                return false;
            }
            
            char buffer[128];
            std::string result = "";
            while (!feof(pipe)) {
                if (fgets(buffer, 128, pipe) != nullptr)
                    result += buffer;
            }
            pclose(pipe);
            
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
        if (player_pid <= 0) {
            is_playing = false;
            return false;
        }
        
        // Check if process is still running
        std::string command = "ps -p " + std::to_string(player_pid) + " > /dev/null";
        int status = std::system(command.c_str());
        
        // If process is not running, update status
        if (status != 0) {
            player_pid = 0;
            is_playing = false;
            return false;
        }
        
        return true;
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
        // Check media playback status
        if (is_playing) {
            checkMediaStatus();
        }
        
        return render_window([this]() {
            if (!item_loaded) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load item");
                return;
            }
            
            // Title
            ImGui::TextColored(colors[2], "%s", item.title.c_str());
            
            // Button to open the original link
            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            if (ImGui::SmallButton("Open in Browser")) {
                // use xdg-open
                auto command = std::format("xdg-open \"{}\" &", item.link);
                std::system(command.c_str());
            }
            
            ImGui::Separator();
            
            // Date
            auto time = std::chrono::system_clock::to_time_t(item.publish_date);
            std::tm* tm = std::localtime(&time);
            char date_str[64];
            std::strftime(date_str, sizeof(date_str), "%d %b %Y %H:%M", tm);
            ImGui::TextColored(colors[3], "Published: %s", date_str);
            
            ImGui::Separator();
            
            // Content in a scrollable area
            if (ImGui::BeginChild("ContentScrollArea", ImVec2(0, 0), true)) {
                // If there's an image, display it
                if (!item.image_url.empty()) {
                    ImGui::Text("Image: %s", item.image_url.c_str());
                    // Could load and display the actual image
                    ImGui::Separator();
                }
                
                // Description/content
                if (!item.description.empty()) {
                    // Basic HTML stripping
                    std::string clean_desc = item.description;
                    // Remove HTML tags (this is very basic, a real implementation would use a proper HTML parser)
                    clean_desc.erase(std::remove_if(clean_desc.begin(), clean_desc.end(), 
                        [](char c) { return c == '<' || c == '>'; }), clean_desc.end());
                    
                    ImGui::TextWrapped("%s", clean_desc.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No content available");
                }
                
                // If there's an enclosure (e.g., podcast link), show it with play/stop buttons
                if (!item.enclosure.empty()) {
                    ImGui::Separator();
                    
                    // Display media link with play/stop controls
                    ImGui::TextColored(ImVec4(0.4f, 0.6f, 0.8f, 1.0f), "Media: %s", item.enclosure.c_str());
                    
                    if (is_playing) {
                        // Show stop button with status
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertFloat4ToU32(colors[5]));
                        if (ImGui::Button("Stop Media")) {
                            stopMedia();
                        }
                        ImGui::PopStyleColor();
                        
                        // Show playing indicator
                        ImGui::SameLine();
                        ImGui::TextColored(colors[4], "Playing");
                    } else {
                        // Show play button
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertFloat4ToU32(colors[4]));
                        if (ImGui::Button("Play Media")) {
                            playMedia();
                        }
                        ImGui::PopStyleColor();
                    }
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