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
