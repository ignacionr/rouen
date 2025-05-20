#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include "../../helpers/imgui_include.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <iostream>

#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../hosts/rss_host.hpp"
#include "../../models/rss/feed.hpp"
#include "../../registrar.hpp"

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
        
        // Feed freshness colors
        get_color(6, ImVec4(0.0f, 0.8f, 0.0f, 1.0f)); // Fresh (less than 1 hour)
        get_color(7, ImVec4(0.6f, 0.8f, 0.0f, 1.0f)); // Recent (less than 1 day)
        get_color(8, ImVec4(0.8f, 0.6f, 0.0f, 1.0f)); // Stale (less than 3 days)
        get_color(9, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Old (older than 3 days)
        
        name("RSS Reader");
        requested_fps = 1;  // Update once per second
        width = 430.0f;
        
        // Use the shared host instance instead of creating a new one
        rss_host = getHost();

        // Register handler for cache invalidation messages
        registrar::add<std::function<void(std::string const&)>>(
            "invalidate_freshness_cache",
            std::make_shared<std::function<void(std::string const&)>>(
                [this](std::string const& feed_url) {
                    this->invalidate_freshness_cache(feed_url);
                }
            )
        );
    }
    
    ~rss() override = default;

    std::string get_uri() const override
    {
        return "rss";
    }
    
    void render_add_feed() {
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
    }

    bool render() override {
        // Periodically invalidate the cache to ensure freshness colors update
        // We do this based on frame count to avoid doing it every single frame
        static int frame_counter = 0;
        frame_counter++;
        
        // Every 60 frames (roughly every minute at 1 FPS), clear the entire cache
        // This ensures that even if feeds are refreshed in the background, we'll
        // eventually pick up the new update times
        if (frame_counter >= 60) {
            invalidate_freshness_cache();
            frame_counter = 0;
        }
        
        return render_window([this] {
            // Feeds section title
            ImGui::TextColored(colors[2], "Your RSS Feeds:");
            
            // Search functionality
            static char search_buffer[256] = "";
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.2f, 0.6f));
            ImGui::PushItemWidth(-1);
            
            ImGui::InputText("##search", search_buffer, IM_ARRAYSIZE(search_buffer));
            
            // Show placeholder text when input is empty
            if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Search feeds... (Type to filter)"
                );
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(); // Pop FrameBg
            
            ImGui::Separator();
            
            // Create scrollable area for feeds
            auto available_size = ImGui::GetContentRegionAvail();
            ImVec2 scroll_area_size = ImVec2(available_size.x, available_size.y - 53);
            if (ImGui::BeginChild("FeedsScrollArea", scroll_area_size, true)) {
                auto feeds = rss_host->feeds();
                if (feeds.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No feeds added yet");
                } else {
                    std::string search_text = search_buffer;
                    bool has_matches = false;

                    render_feed_list(feeds, search_text, has_matches);

                    // Show message when no feeds match the search
                    if (!search_text.empty() && !has_matches) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                            "No feeds match your search");
                    }
                    
                    // Process deletion requests
                    for (const auto& url : feeds_to_delete) {
                        rss_host->deleteFeed(url);
                    }
                    feeds_to_delete.clear();
                }
            }
            ImGui::EndChild();

            ImGui::Separator();

            render_add_feed();
        });
    }

    void render_feed_list(auto &feeds, std::string &search_text, bool &has_matches)
    {
        // Setup ImGui table for feeds
        if (ImGui::BeginTable("FeedsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Feed", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
            ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthFixed, 80.0f, 1);
            
            // Get current time once for all feeds instead of per feed
            auto now = std::chrono::system_clock::now();

            for (const auto &feed : feeds)
            {
                ImGui::PushID(feed->source_link.c_str());

                std::string title = feed->feed_title.empty() ? feed->source_link : feed->feed_title;

                // Filter based on search query if search text is present
                if (!search_text.empty() &&
                    !::helpers::StringHelper::contains_case_insensitive(title, search_text) &&
                    !::helpers::StringHelper::contains_case_insensitive(feed->source_link, search_text))
                {
                    ImGui::PopID();
                    continue; // Skip items that don't match the search
                }

                has_matches = true;

                ImGui::TableNextRow();

                // Feed title (with truncation)
                ImGui::TableSetColumnIndex(0);
                
                // Get color based on feed freshness
                ImVec4 freshness_color = get_freshness_color(feed, now);
                
                // Debug output to check which color is being applied
                if (!feed->items.empty()) {
                    RSS_DEBUG_FMT("Feed '{}' has {} items, newest item is {} hours old, color: [{:.1f}, {:.1f}, {:.1f}]", 
                        title, feed->items.size(), 
                        std::chrono::duration_cast<std::chrono::hours>(now - feed->items.front().updated).count(),
                        freshness_color.x, freshness_color.y, freshness_color.z);
                }
                
                // Apply the freshness color
                ImGui::PushStyleColor(ImGuiCol_Text, freshness_color);
                
                // Add a small colored circle to visually indicate freshness
                ImGui::TextColored(freshness_color, "● ");
                ImGui::SameLine(0, 0); // No spacing
                
                if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                {
                    std::string feed_uri = std::format("rss-feed:{}", feed->repo_id);
                    "create_card"_sfn(feed_uri);
                }
                
                ImGui::PopStyleColor(); // Restore normal text color

                // Item count
                ImGui::TableSetColumnIndex(1);
                // Use the fixed-width font
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGui::TextColored(
                    ImVec4(colors[5].x, colors[5].y, colors[5].z, colors[5].w),
                    "%7zu", feed->items.size());
                ImGui::PopFont();
                ImGui::SameLine();
                if (ImGui::SmallButton("×"))
                {
                    feeds_to_delete.push_back(feed->source_link);
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
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
        static std::mutex host_mutex;
        static std::weak_ptr<hosts::RSSHost> weak_host;
        
        RSS_DEBUG("Entering getHost(), acquiring lock...");
        std::lock_guard<std::mutex> lock(host_mutex);
        RSS_DEBUG("Lock acquired in getHost()");
        
        // Try to get a shared_ptr from the weak_ptr
        auto shared_host = weak_host.lock();
        
        // If the weak_ptr has expired or was never initialized, create a new instance
        if (!shared_host) {
            RSS_INFO("Creating new shared RSSHost instance");
            shared_host = std::make_shared<hosts::RSSHost>();
            weak_host = shared_host; // Store a weak_ptr, not keeping the object alive
            RSS_INFO("Shared RSSHost instance created");
        } else {
            RSS_DEBUG("Reusing existing shared RSSHost instance");
        }
        
        RSS_DEBUG("Exiting getHost()");
        return shared_host;
    }
    
    // Force a cache refresh for a specific feed or all feeds
    void invalidate_freshness_cache(std::string_view feed_url = "") {
        if (feed_url.empty()) {
            // Invalidate all cache entries by clearing the cache
            RSS_DEBUG("Invalidating all freshness cache entries");
            freshness_cache.clear();
        } else {
            // Invalidate a specific feed's cache entry
            RSS_DEBUG_FMT("Invalidating freshness cache for feed: {}", feed_url);
            freshness_cache.erase(std::string(feed_url));
        }
    }

private:
    // Cache for feed freshness colors to avoid recalculating every frame
    mutable std::unordered_map<std::string, std::pair<ImVec4, std::chrono::system_clock::time_point>> freshness_cache;
    static constexpr size_t MAX_CACHE_SIZE = 1000; // Limit cache size to prevent unbounded growth
    
    // Helper method to determine the freshness level of a feed based on most recent item
    // Optimized with caching to reduce CPU usage but forces refresh when requested
    ImVec4 get_freshness_color(const std::shared_ptr<media::rss::feed>& feed, 
                               const std::chrono::system_clock::time_point& now) const {
        // Check if feed has been modified since last cache update by comparing item count
        // or if we need to force a refresh based on time
        auto cache_it = freshness_cache.find(feed->source_link);
        bool should_refresh = true;
        
        if (cache_it != freshness_cache.end()) {
            // In normal cases, refresh cache every minute instead of every 5 minutes
            // to ensure UI feels responsive to feed updates
            auto cache_age = now - cache_it->second.second;
            if (std::chrono::duration_cast<std::chrono::minutes>(cache_age).count() < 1) {
                should_refresh = false;
            }
        }
        
        // If we don't need to refresh, return the cached color
        if (!should_refresh && cache_it != freshness_cache.end()) {
            return cache_it->second.first;
        }
        
        // Calculate freshness color
        ImVec4 color;
        
        if (feed->items.empty()) {
            color = colors[9]; // Old color for feeds with no items
        } else {
            // Find the newest item in the feed
            auto newest_time = std::chrono::system_clock::time_point::min();
            
            // In most RSS feeds items are already sorted by time (newest first)
            // so we first check if the front item is the newest
            if (!feed->items.empty()) {
                newest_time = feed->items.front().updated;
                
                // Only scan through the rest if we have more than one item
                if (feed->items.size() > 1) {
                    for (const auto& item : feed->items) {
                        if (item.updated > newest_time) {
                            newest_time = item.updated;
                        }
                    }
                }
            }
            
            // Calculate time difference
            auto diff = now - newest_time;
            auto hours = std::chrono::duration_cast<std::chrono::hours>(diff).count();
            
            // Determine color based on freshness thresholds
            if (hours < 1) {
                color = colors[6]; // Fresh - less than 1 hour old
            } else if (hours < 24) {
                color = colors[7]; // Recent - less than 1 day old
            } else if (hours < 72) {
                color = colors[8]; // Stale - less than 3 days old
            } else {
                color = colors[9]; // Old - more than 3 days old
            }
        }
        
        // Update cache with size management
        if (freshness_cache.size() >= MAX_CACHE_SIZE && cache_it == freshness_cache.end()) {
            // Cache is full and this is a new entry, remove oldest entry
            auto oldest_it = freshness_cache.begin();
            auto oldest_time = oldest_it->second.second;
            
            // Find the oldest entry
            for (auto it = freshness_cache.begin(); it != freshness_cache.end(); ++it) {
                if (it->second.second < oldest_time) {
                    oldest_time = it->second.second;
                    oldest_it = it;
                }
            }
            
            // Remove oldest entry
            freshness_cache.erase(oldest_it);
        }
        
        // Add or update entry
        freshness_cache[feed->source_link] = {color, now};
        return color;
    }

    std::shared_ptr<hosts::RSSHost> rss_host;
    std::vector<std::string> feeds_to_delete;
};

} // namespace rouen::cards
