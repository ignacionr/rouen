module;

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "../../helpers/html_media_extractor.hpp"

export module rouen.models.rss.feed_item;

export namespace media::rss {

class feed_item {
public:
    std::string title;
    std::string link;
    std::string description;
    std::string enclosure;
    std::string image_url;
    std::chrono::system_clock::time_point updated;
    std::vector<media::html::extracted_media> extracted_media_urls;
    std::optional<double> watermark;
    std::optional<double> media_duration_seconds;
    
    feed_item() = default;
    feed_item(std::string_view title_param, std::string_view link_param, std::string_view description_param,
             std::string_view enclosure_param, std::string_view image_url_param,
             std::chrono::system_clock::time_point updated_param,
             std::optional<double> media_duration_param = std::nullopt);
    void refresh_summary();
    [[nodiscard]] std::string_view summary();
    
    std::string get_best_media_url() const;
    bool has_media() const;
private:
    std::string summary_;
};

} // namespace media::rss
