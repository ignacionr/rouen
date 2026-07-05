#include "rss_date_parser.hpp"
#include <iomanip>
#include <sstream>
#include <format>

namespace media::rss {
    std::chrono::system_clock::time_point parse_rss_date(const char* date_str) {
        if (!date_str || !*date_str) {
            return std::chrono::system_clock::now();
        }
        std::tm tm = {};
        bool parsed = false;
        std::string date_string = date_str;
        // Try RFC 822
        std::istringstream ss1(date_string);
        ss1 >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
        if (!ss1.fail()) parsed = true;
        // Try ISO 8601
        if (!parsed) {
            std::istringstream ss2(date_string);
            ss2 >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (!ss2.fail()) parsed = true;
        }
        if (parsed) {
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
        return std::chrono::system_clock::now();
    }

    std::string format_rss_age(std::chrono::system_clock::time_point const& publish_date) {
        auto const now = std::chrono::system_clock::now();
        if (publish_date > now) {
            return "just now";
        }
        
        auto const diff = now - publish_date;
        auto const diff_sec = std::chrono::duration_cast<std::chrono::seconds>(diff).count();
        
        if (diff_sec < 60) {
            return std::format("{}s ago", diff_sec);
        }
        
        auto const diff_min = std::chrono::duration_cast<std::chrono::minutes>(diff).count();
        if (diff_min < 60) {
            return std::format("{}m ago", diff_min);
        }
        
        auto const diff_hours = std::chrono::duration_cast<std::chrono::hours>(diff).count();
        if (diff_hours < 24) {
            return std::format("{}h ago", diff_hours);
        }
        
        auto const diff_days = diff_hours / 24;
        if (diff_days < 30) {
            return std::format("{}d ago", diff_days);
        }
        
        auto const diff_months = diff_days / 30;
        if (diff_months < 12) {
            return std::format("{}mo ago", diff_months);
        }
        
        auto const diff_years = diff_days / 365;
        return std::format("{}y ago", diff_years);
    }
}
