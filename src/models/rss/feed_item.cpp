#include "feed_item.hpp"
#include "../../helpers/fetch.hpp"
#include "../../registrar.hpp"
#include <exception>
#include <functional>

namespace media::rss {
    feed_item::feed_item(std::string_view title, std::string_view link, std::string_view description,
                 std::string_view enclosure, std::string_view image_url,
                 std::chrono::system_clock::time_point updated)
        : title(title), link(link), description(description),
          enclosure(enclosure), image_url(image_url), updated(updated) {}

    void feed_item::refresh_summary() noexcept {
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
    std::string_view feed_item::summary() noexcept {
        if (summary_.empty()) {
            refresh_summary();
        }
        return summary_;
    }
}
