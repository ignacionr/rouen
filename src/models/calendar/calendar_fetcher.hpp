#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <functional>
#include <optional>
#include <glaze/glaze.hpp>

#include "../../helpers/fetch.hpp"
#include "event.hpp"

namespace calendar {
    // Define glaze schema for the calendar event components
    struct EventDateTime {
        std::string dateTime;
        std::string date;
        std::optional<std::string> timeZone;
    };

    struct EventCreator {
        std::string email;
        std::optional<bool> self;
        std::optional<std::string> displayName;
    };

    struct EventOrganizer {
        std::string email;
        std::optional<bool> self;
        std::optional<std::string> displayName;
    };

    // Add a struct for reminders
    struct EventReminder {
        std::optional<bool> useDefault;
        std::optional<std::vector<std::map<std::string, std::variant<std::string, int>>>> overrides;
    };

    struct CalendarEvent {
        std::string id;
        std::string summary;
        std::optional<std::string> description;
        std::optional<std::string> location;
        std::string htmlLink;
        EventDateTime start;
        EventDateTime end;
        std::optional<EventCreator> creator;
        std::optional<EventOrganizer> organizer;
        std::optional<std::string> status;
        std::optional<std::string> created;
        std::optional<std::string> updated;
        std::optional<int> sequence;
        std::optional<std::string> transparency;
        std::optional<std::string> iCalUID;
        std::optional<std::string> etag;
        // Add new fields
        std::optional<std::string> eventType;
        std::optional<std::string> recurringEventId;
        std::optional<EventDateTime> originalStartTime;
        std::optional<std::string> visibility;
        std::optional<std::string> colorId;
        std::optional<EventReminder> reminders;
    };

    struct CalendarResponse {
        std::vector<CalendarEvent> items;
        std::optional<std::string> kind;
        std::optional<std::string> etag;
        std::optional<std::string> summary;
        std::optional<std::string> description;
        std::optional<std::string> updated;
        std::optional<std::string> timeZone;
        std::optional<std::string> accessRole;
        // Add defaultReminders field
        std::optional<std::vector<std::map<std::string, std::variant<std::string, int>>>> defaultReminders;
    };
}

// Define glaze schema for calendar event structures with error_on_unknown_keys option set to false
template <>
struct glz::meta<calendar::EventDateTime> {
    using T = calendar::EventDateTime;
    static constexpr auto value = object(
        "dateTime", &T::dateTime,
        "date", &T::date,
        "timeZone", &T::timeZone
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<calendar::EventCreator> {
    using T = calendar::EventCreator;
    static constexpr auto value = object(
        "email", &T::email,
        "self", &T::self,
        "displayName", &T::displayName
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<calendar::EventOrganizer> {
    using T = calendar::EventOrganizer;
    static constexpr auto value = object(
        "email", &T::email,
        "self", &T::self,
        "displayName", &T::displayName
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<calendar::EventReminder> {
    using T = calendar::EventReminder;
    static constexpr auto value = object(
        "useDefault", &T::useDefault,
        "overrides", &T::overrides
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<calendar::CalendarEvent> {
    using T = calendar::CalendarEvent;
    static constexpr auto value = object(
        "id", &T::id,
        "summary", &T::summary,
        "description", &T::description,
        "location", &T::location,
        "htmlLink", &T::htmlLink,
        "start", &T::start,
        "end", &T::end,
        "creator", &T::creator,
        "organizer", &T::organizer,
        "status", &T::status,
        "created", &T::created,
        "updated", &T::updated,
        "sequence", &T::sequence,
        "transparency", &T::transparency,
        "iCalUID", &T::iCalUID,
        "etag", &T::etag,
        "eventType", &T::eventType,
        "recurringEventId", &T::recurringEventId,
        "originalStartTime", &T::originalStartTime,
        "visibility", &T::visibility,
        "colorId", &T::colorId,
        "reminders", &T::reminders
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<calendar::CalendarResponse> {
    using T = calendar::CalendarResponse;
    static constexpr auto value = object(
        "items", &T::items,
        "kind", &T::kind,
        "etag", &T::etag,
        "summary", &T::summary,
        "description", &T::description,
        "updated", &T::updated,
        "timeZone", &T::timeZone,
        "accessRole", &T::accessRole,
        "defaultReminders", &T::defaultReminders
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

namespace calendar {
    class calendar_fetcher {
    public:
        calendar_fetcher(const std::string& calendar_url = {}) {
            // Get calendar delegate URL from environment if not provided
            calendar_delegate_url_ = calendar_url.empty() ? 
                std::getenv("CALENDAR_DELEGATE_URL") : calendar_url;
            
            if (calendar_delegate_url_.empty()) {
                throw std::runtime_error("Calendar delegate URL not provided and CALENDAR_DELEGATE_URL environment variable not set");
            }
        }

        // Fetch calendar events from the Google Calendar script
        std::vector<event> fetch_events() {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                http::fetch fetcher;
                std::string response = fetcher(calendar_delegate_url_);
                
                // Parse the JSON response
                auto data = parse_response(response);
                return data;
            } catch (const std::exception& e) {
                last_error_ = e.what();
                return {};
            }
        }

        // Check if there was an error in the last operation
        bool has_error() const { return !last_error_.empty(); }
        
        // Get the last error
        std::string last_error() const { return last_error_; }
        
    private:
        std::string calendar_delegate_url_;
        std::string last_error_;
        std::mutex mutex_;
        
        // Parse the calendar JSON response into a vector of events
        std::vector<event> parse_response(const std::string& json_response) {
            std::vector<event> events;
            
            try {
                // Parse the JSON using glaze with the defined schemas
                CalendarResponse calendar_data;
                glz::context ctx{};
                auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(calendar_data, json_response, ctx);

                if (error) {
                    std::cerr << "Error parsing JSON: " << glz::format_error(error) << std::endl;
                    std::cerr << "Source: " << json_response << std::endl;
                    
                    throw std::runtime_error("Failed to parse JSON response");
                }
                
                // Process each event in the response
                for (const auto& item : calendar_data.items) {
                    event evt;
                    
                    // Copy the basic properties
                    evt.id = item.id;
                    evt.summary = item.summary;
                    evt.description = item.description.value_or("");
                    evt.location = item.location.value_or("");
                    evt.htmlLink = item.htmlLink;
                    
                    // Handle start date/time
                    if (!item.start.dateTime.empty()) {
                        evt.start = item.start.dateTime;
                    } else if (!item.start.date.empty()) {
                        evt.start = item.start.date;
                        evt.all_day = true;
                    }
                    
                    // Handle end date/time
                    if (!item.end.dateTime.empty()) {
                        evt.end = item.end.dateTime;
                    } else if (!item.end.date.empty()) {
                        evt.end = item.end.date;
                    }
                    
                    // Creator and organizer
                    evt.creator = item.creator ? item.creator->email : "";
                    evt.organizer = item.organizer ? item.organizer->email : "";
                    
                    // Add to our collection
                    events.push_back(evt);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to parse calendar response: ") + e.what());
            }
            
            return events;
        }
    };
}