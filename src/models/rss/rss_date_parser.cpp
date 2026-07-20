#include "rss_date_parser.hpp"
#include <iomanip>
#include <sstream>
#include <format>
#include <regex>

namespace media::rss {
    namespace {
        long parse_tz_offset(const std::string& date_string) {
            // Trim trailing whitespace
            std::string s = date_string;
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
            
            // 1. Check for numeric offset: +hhmm, -hhmm, +hh:mm, -hh:mm at the end
            std::regex num_tz_regex(R"raw(([+-])(\d{2}):?(\d{2})$)raw");
            std::smatch match;
            if (std::regex_search(s, match, num_tz_regex)) {
                char sign = match.str(1)[0];
                int hours = std::stoi(match.str(2));
                int minutes = std::stoi(match.str(3));
                long offset_sec = ((hours * 3600) + (minutes * 60));
                return (sign == '-') ? -offset_sec : offset_sec;
            }
            
            // 2. Check for named timezone abbreviations at the end
            std::regex alpha_tz_regex(R"raw(([A-Z]{3,4})$)raw");
            if (std::regex_search(s, match, alpha_tz_regex)) {
                std::string tz = match.str(1);
                if (tz == "GMT" || tz == "UTC" || tz == "UT" || tz == "Z") return 0;
                if (tz == "EST") return -5 * 3600;
                if (tz == "EDT") return -4 * 3600;
                if (tz == "CST") return -6 * 3600;
                if (tz == "CDT") return -5 * 3600;
                if (tz == "MST") return -7 * 3600;
                if (tz == "MDT") return -6 * 3600;
                if (tz == "PST") return -8 * 3600;
                if (tz == "PDT") return -7 * 3600;
                if (tz == "BST") return 1 * 3600;
                if (tz == "CET") return 1 * 3600;
                if (tz == "CEST") return 2 * 3600;
            }
            
            // Check if it ends with 'Z' (ISO 8601)
            if (!s.empty() && s.back() == 'Z') {
                return 0;
            }
            
            return 0; // Default to 0 offset (UTC)
        }
    }

    std::chrono::system_clock::time_point parse_rss_date(const char* date_str) {
        if (!date_str || !*date_str) {
            return std::chrono::system_clock::now();
        }
        std::tm tm = {};
        bool parsed = false;
        std::string date_string = date_str;
        // Try RFC 822
        std::istringstream ss1(date_string);
        ss1.imbue(std::locale::classic());
        ss1 >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
        if (!ss1.fail()) parsed = true;
        // Try ISO 8601
        if (!parsed) {
            std::istringstream ss2(date_string);
            ss2.imbue(std::locale::classic());
            ss2 >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (!ss2.fail()) parsed = true;
        }
        if (parsed) {
            // Convert tm to UTC time_point using C++20 calendar types
            using namespace std::chrono;
            auto date = year{tm.tm_year + 1900}/(tm.tm_mon + 1)/tm.tm_mday;
            auto time = hours{tm.tm_hour} + minutes{tm.tm_min} + seconds{tm.tm_sec};
            auto parsed_tp = sys_days{date} + time;
            
            // Adjust by the parsed timezone offset (utc_time = local_time - offset)
            long offset_sec = parse_tz_offset(date_string);
            return parsed_tp - seconds{offset_sec};
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
