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
#include "../../hosts/rss_host.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/debug.hpp"

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
        
        // Initialize the image cache
        image_cache = std::make_shared<helpers::ImageCache>(
            "rss_images.db",         // SQLite database for image cache
            "cache/rss_images",      // Cache directory for image files
            30                       // Expire images after 30 days
        );
        
        // Load feed information and items
        loadFeed();
    }
    
    ~rss_feed() override {
        // Clean up textures
        if (feed_image_texture) {
            SDL_DestroyTexture(feed_image_texture);
            feed_image_texture = nullptr;
        }
    }
    
    void loadFeed() {
        if (feed_id < 0 || !rss_host) return;
        
        // Get feed information
        auto feed_info = rss_host->getFeedInfo(feed_id);
        if (!feed_info) return;
        
        // Update feed details
        feed_title = feed_info->title;
        feed_url = feed_info->url;
        feed_image_url = feed_info->image_url;
        
        // Update the card title with the feed title
        name(std::format("{} - Feed", feed_title));
        
        // Load feed items
        items = rss_host->getFeedItems(feed_id);
        
        // Load the feed image if available
        loadFeedImage();
    }
    
    void loadFeedImage() {
        // Clean up previous texture if it exists
        if (feed_image_texture) {
            SDL_DestroyTexture(feed_image_texture);
            feed_image_texture = nullptr;
            feed_image_width = 0;
            feed_image_height = 0;
        }
        
        // If there's no image URL or we don't have a renderer, we're done
        if (feed_image_url.empty() || !renderer) {
            return;
        }
        
        // Use the image cache to load the image
        feed_image_texture = image_cache->getTexture(
            renderer,
            feed_image_url,
            feed_image_width,
            feed_image_height
        );
    }
    
    void set_renderer(SDL_Renderer* r) {
        renderer = r;
        
        // If we have a renderer and feed image URL, load the image
        if (renderer && !feed_image_url.empty()) {
            loadFeedImage();
        }
    }
    
    bool render() override {
        try {
            return render_window([this]() {
                try {
                    // Feed title at top
                    ImGui::TextColored(colors[2], "%s", feed_title.c_str());
                    
                    // Display the feed image if we have one
                    if (feed_image_texture && feed_image_width > 0 && feed_image_height > 0) {
                        // Set fixed height of 140.0f and scale width to maintain aspect ratio
                        float fixed_height = 140.0f;
                        float aspect_ratio = static_cast<float>(feed_image_width) / static_cast<float>(feed_image_height);
                        float display_width = fixed_height * aspect_ratio;
                        
                        // Center the image horizontally
                        float available_width = ImGui::GetContentRegionAvail().x;
                        float offset = (available_width - display_width) * 0.5f;
                        if (offset > 0.0f) {
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
                        }
                        
                        // Create ImGui image from SDL texture
                        ImGui::Image(
                            reinterpret_cast<void*>(feed_image_texture),
                            ImVec2(display_width, fixed_height)
                        );
                    }
                                
                    ImGui::Separator();
                    
                    // Items in a scrollable area
                    try {
                        // Important: BeginChild returns if content is visible, but EndChild must always be called
                        bool is_visible = ImGui::BeginChild("ItemsScrollArea", ImVec2(0, 0), true);
                        
                        if (is_visible) {
                            if (items.empty()) {
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No items in this feed");
                            } else {
                                for (const auto& item : items) {
                                    // Using scope guard pattern for ImGui pairs
                                    ImGui::PushID(item.link.c_str());
                                    
                                    try {
                                        // Item container with spacing
                                        ImGui::BeginGroup();
                                        
                                        try {
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

                                            // if there's a playable enclosure, offer media controls
                                            if (!item.enclosure.empty()) {
                                                media_player::player(item.enclosure, colors[4], "Play Audio");
                                            }
                                        }
                                        catch (const std::exception& e) {
                                            RSS_ERROR_FMT("Exception in RSS feed item rendering: {}", e.what());
                                        }
                                        
                                        // Always end the group - regardless of exceptions
                                        ImGui::EndGroup();
                                    }
                                    catch (const std::exception& e) {
                                        RSS_ERROR_FMT("Exception in RSS feed group: {}", e.what());
                                        // Make sure we end any ImGui operations that were started
                                        ImGui::EndGroup();
                                    }
                                    
                                    ImGui::Separator();
                                    
                                    // Always pop the ID
                                    ImGui::PopID();
                                }
                            }
                        }
                        
                        // Always end the child - regardless of is_visible value
                        ImGui::EndChild();
                    }
                    catch (const std::exception& e) {
                        RSS_ERROR_FMT("Exception in RSS feed items area: {}", e.what());
                        // Always ensure EndChild is called to match the BeginChild
                        ImGui::EndChild();
                    }
                }
                catch (const std::exception& e) {
                    RSS_ERROR_FMT("Exception in RSS feed rendering: {}", e.what());
                }
            });
        }
        catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception in RSS feed card: {}", e.what());
            return false;
        }
    }

    std::string get_uri() const override {
        return std::format("rss-feed:{}", feed_id);
    }
    
private:
    long long feed_id = -1;
    std::string feed_title;
    std::string feed_url;
    std::string feed_image_url;
    std::shared_ptr<rouen::hosts::RSSHost> rss_host;
    std::vector<rouen::hosts::RSSHost::FeedItem> items;
    
    // Image handling
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* feed_image_texture = nullptr;
    int feed_image_width = 0;
    int feed_image_height = 0;
    std::shared_ptr<helpers::ImageCache> image_cache;
};

} // namespace rouen::cards