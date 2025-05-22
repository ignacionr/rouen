#pragma once
#include <string>
#include <chrono>
#include <string_view>

namespace media::rss {
    class feed_item {
    public:
        std::string title;
        std::string link;
        std::string description;
        std::string enclosure;
        std::string image_url;
        std::chrono::system_clock::time_point updated;
        feed_item() = default;
        feed_item(std::string_view title, std::string_view link, std::string_view description,
                 std::string_view enclosure, std::string_view image_url,
                 std::chrono::system_clock::time_point updated);
        void refresh_summary() noexcept;
        [[nodiscard]] std::string_view summary() noexcept;
    private:
        std::string summary_;
    };
}
