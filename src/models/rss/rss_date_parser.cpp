#include "rss_date_parser.hpp"
#include <iomanip>
#include <sstream>

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
}
