#pragma once
#include <chrono>
#include <string>
#include <string_view>
#include <ctime>

namespace media::rss {
    std::chrono::system_clock::time_point parse_rss_date(const char* date_str);
    std::string format_rss_age(std::chrono::system_clock::time_point const& publish_date);
}
