#pragma once
#include <string>
#include <chrono>
#include <string_view>
#include <vector>
#include <optional>
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
        std::optional<double> watermark; // playback watermark
        std::optional<double> media_duration_seconds; // media duration in seconds
        
        feed_item() = default;
        feed_item(std::string_view title_param, std::string_view link_param, std::string_view description_param,
                 std::string_view enclosure_param, std::string_view image_url_param,
                 std::chrono::system_clock::time_point updated_param,
                 std::optional<double> media_duration_param = std::nullopt);
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
