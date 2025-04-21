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

} // namespace rouen::cards