#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <optional>

namespace media::travel {
    // Represents a single travel destination within a trip plan
    struct destination {
        std::string name;
        std::string location;
        std::string notes;
        std::chrono::system_clock::time_point arrival;
        std::chrono::system_clock::time_point departure;
        std::string accommodation;
        double budget{0.0};
        bool completed{false};
    };

    // Represents a complete travel plan/trip
    struct plan {
        long long id{-1};
        std::string title;
        std::string description;
        std::vector<destination> destinations;
        std::chrono::system_clock::time_point created;
        std::chrono::system_clock::time_point start_date;
        std::chrono::system_clock::time_point end_date;
        double total_budget{0.0};
        
        // Possible statuses for a travel plan
        enum class status {
            planning,
            booked,
            active,
            completed,
            cancelled
        };
        
        status current_status{status::planning};
        
        // Convert enum to string for display
        static std::string status_to_string(status s) {
            switch (s) {
                case status::planning: return "Planning";
                case status::booked: return "Booked";
                case status::active: return "Active";
                case status::completed: return "Completed";
                case status::cancelled: return "Cancelled";
                default: return "Unknown";
            }
        }
        
        // Convert string to enum for storage/retrieval
        static std::optional<status> string_to_status(const std::string& s) {
            if (s == "Planning") return status::planning;
            if (s == "Booked") return status::booked;
            if (s == "Active") return status::active;
            if (s == "Completed") return status::completed;
            if (s == "Cancelled") return status::cancelled;
            return std::nullopt;
        }
        
        // Calculate total days of the trip
        int total_days() const {
            auto diff = end_date - start_date;
            return std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24 + 1;
        }
        
        // Calculate if the trip is upcoming, current, or past
        std::string timeframe() const {
            auto now = std::chrono::system_clock::now();
            if (now < start_date) return "Upcoming";
            if (now > end_date) return "Past";
            return "Current";
        }
    };
}