module;

#include <utility>
#include <chrono>
#include <string>

export module rouen.models.rss.rss_date_parser;

export namespace media::rss {
    std::chrono::system_clock::time_point parse_rss_date(const char* date_str);
    std::string format_rss_age(std::chrono::system_clock::time_point const& publish_date);
}
