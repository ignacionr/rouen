#pragma once
#include <string>
#include <chrono>
#include <string_view>
#include <vector>
#include "../../helpers/html_media_extractor.hpp"

namespace media::rss {
    class feed_item {
    public:
        std::string title;
        std::string link;
        std::string description;
        std::string enclosure;
        std::string image_url;
        std::chrono::system_clock::time_point updated;
        std::vector<media::html::extracted_media> extracted_media_urls; // Enhanced: extracted media from content
        
        feed_item() = default;
        feed_item(std::string_view title, std::string_view link, std::string_view description,
                 std::string_view enclosure, std::string_view image_url,
                 std::chrono::system_clock::time_point updated);
        void refresh_summary();
        [[nodiscard]] std::string_view summary();
        
        // Enhanced: Get the best available media URL for playback
        std::string get_best_media_url() const;
        
        // Enhanced: Check if this item has playable media
        bool has_media() const;
    private:
        std::string summary_;
    };
}
