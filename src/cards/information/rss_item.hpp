#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <imgui/imgui.h>
#include <memory>
#include <string>
#include <vector>

#include "../interface/card.hpp"
#include "rss.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/debug.hpp"
#include "../../hosts/rss_host.hpp"

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
        width = 600.0f;
        
        // Set refresh rate to check media playback status
        requested_fps = 1;
    }
    
    ~rss_item() override {
        // Make sure to stop playback when card is closed
        if (!item.enclosure.empty()) {
            media.stopMedia();
        }
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
        
        // Configure media player with enclosure URL if available
        if (!item.enclosure.empty()) {
            media.url = item.enclosure;
        }
        
        item_loaded = true;
    }
    
    bool render() override {
        try {
            return render_window([this]() {
                try {
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
                        
                        // Use the media_player helper for playback controls
                        try {
                            media_player::player(item.enclosure, colors[4]);
                        } catch (const std::exception& e) {
                            RSS_ERROR_FMT("Exception in media player: {}", e.what());
                        }
                        
                        ImGui::Separator();
                    }
                    
                    // Content in a scrollable area
                    try {
                        // Note: BeginChild returns if content is visible, but EndChild must always be called
                        bool is_visible = ImGui::BeginChild("ContentScrollArea", ImVec2(0, 0), true);
                        
                        if (is_visible) {
                            // Use description as content
                            std::string content = item.description;
                            
                            if (!content.empty()) {
                                try {
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
                                } catch (const std::exception& e) {
                                    RSS_ERROR_FMT("Exception in content processing: {}", e.what());
                                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Error processing content");
                                }
                            } else {
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No content available");
                            }
                        }
                        
                        // Always end the child - regardless of is_visible value
                        ImGui::EndChild();
                    } catch (const std::exception& e) {
                        RSS_ERROR_FMT("Exception in content scroll area: {}", e.what());
                        // Always ensure EndChild is called to match the BeginChild
                        ImGui::EndChild();
                    }
                } catch (const std::exception& e) {
                    RSS_ERROR_FMT("Exception in RSS item rendering: {}", e.what());
                }
            });
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception in RSS item card: {}", e.what());
            return false;
        }
    }
    
private:
    long long feed_id = -1;
    std::string item_link;
    bool item_loaded = false;
    std::shared_ptr<hosts::RSSHost> rss_host;
    hosts::RSSHost::FeedItem item; // Use the FeedItem from the controller
    
    // Use the media_player helper for media playback
    media_player::item media;
};

} // namespace rouen::cards