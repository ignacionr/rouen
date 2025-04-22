#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace calendar {
    struct event {
        std::string id;
        std::string summary;
        std::string description;
        std::string location;
        std::string htmlLink;
        std::string start;
        std::string end;
        std::string creator;
        std::string organizer;
        bool all_day = false;
        
        // Helper function to format the date/time for display
        std::string format_time() const {
            return all_day ? start.substr(0, 10) : start.substr(0, 16);
        }
    };
}