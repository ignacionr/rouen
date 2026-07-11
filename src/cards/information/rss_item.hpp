#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include "../../helpers/imgui_include.hpp"
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <set>
#include <mutex>

#include "../interface/card.hpp"
#include "rss.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/platform_utils.hpp"
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
        
        // Parse the feed_id,link,title info (separated by ||| or fallback to comma)
        size_t first_delim = item_info.find("|||");
        size_t second_delim = (first_delim != std::string::npos) ? item_info.find("|||", first_delim + 3) : std::string::npos;
        
        if (first_delim != std::string::npos && second_delim != std::string::npos) {
            try {
                feed_id = std::stoll(item_info.substr(0, first_delim));
                item_link = item_info.substr(first_delim + 3, second_delim - (first_delim + 3));
                item_title = item_info.substr(second_delim + 3);
                
                // Get the RSS host controller
                rss_host = rss::getHost();
                
                // Load the item
                loadItem();
            } catch (...) {
                name("RSS Item");
            }
        } else {
            // Fallback to comma parsing for compatibility with legacy URLs
            size_t comma_pos = item_info.find(',');
            if (comma_pos != std::string::npos) {
                try {
                    feed_id = std::stoll(item_info.substr(0, comma_pos));
                    item_link = item_info.substr(comma_pos + 1);
                    rss_host = rss::getHost();
                    loadItem();
                } catch (...) {
                    name("RSS Item");
                }
            } else {
                name("RSS Item");
            }
        }
        
        // Initialize the image cache with paths in user data directory
        auto db_path = rouen::platform::get_user_data_path("rss_images.db").string();
        auto cache_dir = rouen::platform::get_user_data_path("cache/rss_images").string();
        
        image_cache = std::make_shared<::helpers::ImageCache>(
            db_path,          // SQLite database for image cache in user's data directory
            cache_dir,        // Cache directory for image files in user's data directory
            30                // Expire images after 30 days
        );

        // Adjust size to be larger for content display
        width *= 2.0f;
        
        // Set refresh rate to check media playback status
        requested_fps = 1;
    }
    
    ~rss_item() override {
        // Make sure to stop playback when card is closed
        if (!item.enclosure.empty()) {
            media.stopMedia();
        }
        clear_item_textures();
    }

    void clear_item_textures() {
        for (auto& [url, lt] : item_textures) {
            if (lt.texture) {
                SDL_DestroyTexture(lt.texture);
            }
        }
        item_textures.clear();
    }

    void set_renderer(SDL_Renderer *r) {
        renderer = r;
    }

    void calculate_cover_uvs(float target_w, float target_h, float tex_w, float tex_h, ImVec2& uv0, ImVec2& uv1) {
        if (tex_w <= 0.0f || tex_h <= 0.0f || target_w <= 0.0f || target_h <= 0.0f) {
            uv0 = ImVec2(0.0f, 0.0f);
            uv1 = ImVec2(1.0f, 1.0f);
            return;
        }
        float target_aspect = target_w / target_h;
        float tex_aspect = tex_w / tex_h;

        if (tex_aspect > target_aspect) {
            float f = target_aspect / tex_aspect;
            float c = (1.0f - f) * 0.5f;
            uv0 = ImVec2(c, 0.0f);
            uv1 = ImVec2(1.0f - c, 1.0f);
        } else {
            float f = tex_aspect / target_aspect;
            float c = (1.0f - f) * 0.5f;
            uv0 = ImVec2(0.0f, c);
            uv1 = ImVec2(1.0f, 1.0f - c);
        }
    }

    void request_image_download(const std::string& url) {
        static std::set<std::string> downloading_urls;
        static std::mutex downloading_mutex;

        {
            std::lock_guard<std::mutex> lock(downloading_mutex);
            if (downloading_urls.contains(url)) {
                return; // Already downloading
            }
            downloading_urls.insert(url);
        }

        auto cache = image_cache;
        std::thread([cache, url]() {
            try {
                cache->downloadAndCache(url);
            } catch (...) {}
            
            {
                std::lock_guard<std::mutex> lock(downloading_mutex);
                downloading_urls.erase(url);
            }
        }).detach();
    }
    
    void loadItem() {
        if (feed_id < 0 || item_link.empty() || !rss_host) return;
        
        // Get the item from the controller
        auto found_item = rss_host->getFeedItem(feed_id, item_link, item_title);
        if (!found_item) return;
        
        // Store the item
        item = *found_item;
        
        // Update the card title
        name(std::format("{} - Article", item.title));
        
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
                    
                    // Original URL link
                    ImGui::TextColored(colors[1], "Source: ");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Open in Browser")) {
                        // Open URL in browser using platform-specific command
                        auto command = rouen::platform::open_file(item_link, true);
                        [[maybe_unused]] int system_result = std::system(command.c_str());
                    }
                    if (feed_id >= 0) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Open Feed")) {
                            "create_card"_sfn(std::format("rss-feed:{}", feed_id));
                        }
                    }
                    
                    // Date and time
                    auto time = std::chrono::system_clock::to_time_t(item.publish_date);
                    std::tm* tm = std::localtime(&time);
                    char date_str[64];
                    std::strftime(date_str, sizeof(date_str), "%A, %d %B %Y %H:%M", tm);
                    ImGui::TextColored(colors[1], "Published: %s", date_str);
                    
                    // Media enclosure playback controls
                    if (!item.enclosure.empty()) {
                        ImGui::Separator();
                        
                        // Use the media_player helper for playback controls
                        try {
                            media_player::player(item.enclosure, colors[0], "Play Audio", item.feed_id, item.link, item.title, item.watermark, true);
                        } catch (const std::exception& e) {
                            RSS_ERROR_FMT("Exception in media player: {}", e.what());
                        }
                        
                        ImGui::Separator();
                    }
                    // Enhanced: Check for extracted media URLs if no direct enclosure
                    else if (!item.extracted_media_urls.empty()) {
                        ImGui::Separator();
                        
                        // Show all available media options
                        ImGui::TextColored(colors[0], "Media Content:");
                        
                        for (size_t i = 0; i < item.extracted_media_urls.size(); ++i) {
                            const auto& extracted_media = item.extracted_media_urls[i];
                            
                            std::string media_title = std::format("Play {} ({})", 
                                extracted_media.type == "video" ? "Video" : 
                                extracted_media.type == "audio" ? "Audio" : "Media",
                                extracted_media.format);
                            
                            try {
                                media_player::player(extracted_media.url, colors[0], media_title, item.feed_id, item.link, item.title, item.watermark, true);
                                if (i < item.extracted_media_urls.size() - 1) {
                                    ImGui::Spacing();
                                }
                            } catch (const std::exception& e) {
                                RSS_ERROR_FMT("Exception in extracted media player: {}", e.what());
                            }
                        }
                        
                        ImGui::Separator();
                    }
                    // Enhanced: Check if this is a YouTube/Vimeo link without enclosure
                    else if (item.link.find("youtube.com") != std::string::npos || 
                             item.link.find("youtu.be") != std::string::npos ||
                             item.link.find("vimeo.com") != std::string::npos) {
                        ImGui::Separator();
                        
                        try {
                            media_player::player(item.link, colors[0], "Play Video", item.feed_id, item.link, item.title, item.watermark, true);
                        } catch (const std::exception& e) {
                            RSS_ERROR_FMT("Exception in video link player: {}", e.what());
                        }
                        
                        ImGui::Separator();
                    }
                    
                    // Content in a scrollable area
                    try {
                        // Note: BeginChild returns if content is visible, but EndChild must always be called
                        bool is_visible = ImGui::BeginChild("ContentScrollArea", ImVec2(0, 0), true);
                        
                        if (is_visible) {
                            // Try to load item image if available
                            // BUT: Only show image if not playing media
                            bool is_playing_media = !item.enclosure.empty() || !item.extracted_media_urls.empty() ||
                                                    (item.link.find("youtube.com") != std::string::npos || 
                                                     item.link.find("youtu.be") != std::string::npos ||
                                                     item.link.find("vimeo.com") != std::string::npos);
                            
                            if (!is_playing_media) {
                                SDL_Texture* item_tex = nullptr;
                                int item_tex_w = 0, item_tex_h = 0;
                                if (renderer && image_cache && !item.image_url.empty()) {
                                    if (item_textures.contains(item.image_url)) {
                                        auto& lt = item_textures[item.image_url];
                                        item_tex = lt.texture;
                                        item_tex_w = lt.width;
                                        item_tex_h = lt.height;
                                    } else {
                                        int cached_w = 0, cached_h = 0;
                                        if (image_cache->isCached(item.image_url, cached_w, cached_h)) {
                                            item_tex = image_cache->getTexture(renderer, item.image_url, item_tex_w, item_tex_h);
                                            if (item_tex) {
                                                item_textures[item.image_url] = {item_tex, item_tex_w, item_tex_h};
                                            }
                                        } else {
                                            request_image_download(item.image_url);
                                        }
                                    }
                                }

                                if (item_tex) {
                                    float avail_w = ImGui::GetContentRegionAvail().x;
                                    ImVec2 banner_size(avail_w, 240.0f);
                                    ImVec2 banner_pos = ImGui::GetCursorScreenPos();
                                    
                                    ImVec2 uv0, uv1;
                                    calculate_cover_uvs(banner_size.x, banner_size.y, static_cast<float>(item_tex_w), static_cast<float>(item_tex_h), uv0, uv1);
                                    
                                    ImGui::GetWindowDrawList()->AddImage(
                                        rouen::helpers::texture_id_cast(item_tex),
                                        banner_pos,
                                        ImVec2(banner_pos.x + banner_size.x, banner_pos.y + banner_size.y),
                                        uv0,
                                        uv1
                                    );
                                    ImGui::Dummy(banner_size);
                                    ImGui::Spacing();
                                    ImGui::Separator();
                                    ImGui::Spacing();
                                }
                            }

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

    std::string get_uri() const override
    {
        return std::format("rss-item:{}|||{}|||{}", feed_id, item_link, item_title);
    }
    
private:
    long long feed_id = -1;
    std::string item_link;
    std::string item_title;
    bool item_loaded = false;
    std::shared_ptr<hosts::RSSHost> rss_host;
    hosts::RSSHost::FeedItem item; // Use the FeedItem from the controller
    
    // Use the media_player helper for media playback
    media_player::item media;

    SDL_Renderer* renderer = nullptr;
    std::shared_ptr<::helpers::ImageCache> image_cache;

    struct LoadedItemTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, LoadedItemTexture> item_textures;
};

} // namespace rouen::cards
