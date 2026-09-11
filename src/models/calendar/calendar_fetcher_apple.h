#pragma once
#include <string>
#include <vector>
#include "event.hpp"

namespace calendar {
    std::vector<event> fetch_events_apple(const std::string& start_date_iso, const std::string& end_date_iso, std::string& out_error);
    bool create_event_apple(const std::string& calendar_name, const std::string& summary, const std::string& description, const std::string& location,
                            int start_year, int start_month, int start_day, int start_hour, int start_min,
                            int end_year, int end_month, int end_day, int end_hour, int end_min,
                            bool is_all_day, std::string& out_error);
}
