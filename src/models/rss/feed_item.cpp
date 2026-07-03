#include "feed_item.hpp"
#include "../../helpers/fetch.hpp"
#include "../../registrar.hpp"
#include <exception>
#include <functional>

namespace media::rss {
    feed_item::feed_item(std::string_view title_param, std::string_view link_param, std::string_view description_param,
                 std::string_view enclosure_param, std::string_view image_url_param,
                 std::chrono::system_clock::time_point updated_param)
        : title(title_param), link(link_param), description(description_param),
          enclosure(enclosure_param), image_url(image_url_param), updated(updated_param) {}

    void feed_item::refresh_summary() {
        try {
            auto const link_contents = http::fetch{}(link);
            auto const summarize = registrar::get<std::function<std::string(std::string_view)>>({});
            auto text = (*summarize)(link_contents);
            summary_ = text;
        }
        catch (std::exception const &e) {
            summary_ = e.what();
        }
        catch (...) {
            summary_ = "Failed to summarize";
        }
    }
    std::string_view feed_item::summary() {
        if (summary_.empty()) {
            refresh_summary();
        }
        return summary_;
    }
    
    std::string feed_item::get_best_media_url() const {
        // Priority order:
        // 1. Direct enclosure URL (highest priority)
        // 2. Video content from extracted media
        // 3. Audio content from extracted media
        // 4. Any other media content
        // 5. YouTube/Vimeo links as fallback
        
        if (!enclosure.empty()) {
            return enclosure;
        }
        
        // Look for video content first
        for (const auto& media : extracted_media_urls) {
            if (media.type == "video" && 
                (media.format == "mp4" || media.format == "webm" || media.format == "youtube" || media.format == "vimeo")) {
                return media.url;
            }
        }
        
        // Then audio content
        for (const auto& media : extracted_media_urls) {
            if (media.type == "audio" && 
                (media.format == "mp3" || media.format == "wav" || media.format == "ogg" || media.format == "aac")) {
                return media.url;
            }
        }
        
        // Any other media
        for (const auto& media : extracted_media_urls) {
            if (!media.url.empty()) {
                return media.url;
            }
        }
        
        // If this is a YouTube link, return it directly
        if (link.find("youtube.com") != std::string::npos || link.find("youtu.be") != std::string::npos) {
            return link;
        }
        
        return "";
    }
    
    bool feed_item::has_media() const {
        return !enclosure.empty() || 
               !extracted_media_urls.empty() || 
               link.find("youtube.com") != std::string::npos ||
               link.find("youtu.be") != std::string::npos ||
               link.find("vimeo.com") != std::string::npos;
    }
}
