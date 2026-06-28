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
#include "../../helpers/glaze_include.hpp"

#include "../../helpers/fetch.hpp"
#include "event.hpp"

#if defined(__APPLE__)
#include "../../helpers/process_helper.hpp"
#include "../../helpers/platform_utils.hpp"
#include <filesystem>
#endif

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
        std::optional<std::vector<std::string>> calendars;
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
        "defaultReminders", &T::defaultReminders,
        "calendars", &T::calendars
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

namespace calendar {
    class calendar_fetcher {
    public:
        calendar_fetcher(const std::string& calendar_url = {}) {
#if defined(__APPLE__)
            // On macOS, we query the local calendar via scripting, so calendar_url/CALENDAR_DELEGATE_URL is not required.
            (void)calendar_url;
#else
            // Get calendar delegate URL from parameter or environment
            if (!calendar_url.empty()) {
                calendar_delegate_url_ = calendar_url;
            } else {
                const char* env_url = std::getenv("CALENDAR_DELEGATE_URL");
                calendar_delegate_url_ = env_url ? env_url : "";
            }
            
            // Set error if the URL is empty (don't throw, let fetch_events handle it)
            if (calendar_delegate_url_.empty()) {
                last_error_ = "Calendar URL not provided. Please configure CALENDAR_DELEGATE_URL.";
            }
#endif
        }

        // Fetch calendar events
        std::vector<event> fetch_events() {
#if defined(__APPLE__)
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                last_error_.clear();
                
                // Prefer the Swift/EventKit fetcher (fetch_calendar.swift) which properly
                // expands recurring event occurrences.  Fall back to the legacy AppleScript
                // if the Swift script cannot be found.
                auto swift_path = rouen::platform::get_resource_path("fetch_calendar.swift");
                if (!std::filesystem::exists(swift_path)) {
                    auto dev_swift_path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "scripts" / "fetch_calendar.swift";
                    if (std::filesystem::exists(dev_swift_path)) {
                        swift_path = dev_swift_path;
                    }
                }

                std::string command;
                if (std::filesystem::exists(swift_path)) {
                    command = "swift \"" + swift_path.string() + "\"";
                } else {
                    // Legacy AppleScript fallback (does not expand recurring events)
                    auto script_path = rouen::platform::get_resource_path("fetch_calendar.scpt");
                    if (!std::filesystem::exists(script_path)) {
                        auto dev_script_path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "scripts" / "fetch_calendar.scpt";
                        if (std::filesystem::exists(dev_script_path)) {
                            script_path = dev_script_path;
                        } else {
                            throw std::runtime_error("fetch_calendar.swift / fetch_calendar.scpt not found in resources");
                        }
                    }
                    command = "osascript \"" + script_path.string() + "\"";
                }

                std::string response = ProcessHelper::executeCommand(command);
                if (response.empty()) {
                    throw std::runtime_error("calendar fetch command returned empty response or failed");
                }
                
                // Parse the JSON response
                auto data = parse_response(response);
                return data;
            } catch (const std::exception& e) {
                last_error_ = e.what();
                return {};
            }
#else
            if (calendar_delegate_url_.empty()) {
                last_error_ = "Calendar URL not provided. Please configure CALENDAR_DELEGATE_URL.";
                return {};
            }
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                last_error_.clear();
                http::fetch fetcher;
                std::string response = fetcher(calendar_delegate_url_);
                
                // Parse the JSON response
                auto data = parse_response(response);
                return data;
            } catch (const std::exception& e) {
                last_error_ = e.what();
                return {};
            }
#endif
        }

        // Check if there was an error in the last operation
        bool has_error() const { return !last_error_.empty(); }
        
        // Get the last error
        std::string last_error() const { return last_error_; }

        // Get the list of writable calendars
        std::vector<std::string> get_calendars() const {
            return calendars_;
        }

        // Create a new calendar event
        bool create_event(const std::string& calendar_name, const std::string& summary, const std::string& description, const std::string& location,
                          int start_year, int start_month, int start_day, int start_hour, int start_min,
                          int end_year, int end_month, int end_day, int end_hour, int end_min,
                          bool is_all_day) {
#if defined(__APPLE__)
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                last_error_.clear();
                
                auto script_path = rouen::platform::get_resource_path("create_event.scpt");
                if (!std::filesystem::exists(script_path)) {
                    auto dev_script_path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "scripts" / "create_event.scpt";
                    if (std::filesystem::exists(dev_script_path)) {
                        script_path = dev_script_path;
                    } else {
                        throw std::runtime_error("create_event.scpt not found in resources");
                    }
                }
                
                auto escape_arg = [](const std::string& arg) {
                    std::string res = "'";
                    for (char c : arg) {
                        if (c == '\'') {
                            res += "'\\''";
                        } else {
                            res += c;
                        }
                    }
                    res += "'";
                    return res;
                };
                
                std::string command = "osascript \"" + script_path.string() + "\" "
                    + escape_arg(calendar_name) + " "
                    + escape_arg(summary) + " "
                    + escape_arg(description) + " "
                    + escape_arg(location) + " "
                    + std::to_string(start_year) + " "
                    + std::to_string(start_month) + " "
                    + std::to_string(start_day) + " "
                    + std::to_string(start_hour) + " "
                    + std::to_string(start_min) + " "
                    + std::to_string(end_year) + " "
                    + std::to_string(end_month) + " "
                    + std::to_string(end_day) + " "
                    + std::to_string(end_hour) + " "
                    + std::to_string(end_min) + " "
                    + (is_all_day ? "true" : "false");
                
                std::string response = ProcessHelper::executeCommand(command);
                while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
                    response.pop_back();
                }
                
                if (response == "SUCCESS") {
                    return true;
                } else {
                    throw std::runtime_error(response.empty() ? "Event creation failed with empty response" : response);
                }
            } catch (const std::exception& e) {
                last_error_ = e.what();
                return false;
            }
#else
            (void)calendar_name; (void)summary; (void)description; (void)location;
            (void)start_year; (void)start_month; (void)start_day; (void)start_hour; (void)start_min;
            (void)end_year; (void)end_month; (void)end_day; (void)end_hour; (void)end_min;
            (void)is_all_day;
            last_error_ = "Event creation not supported on this platform";
            return false;
#endif
        }
        
    private:
        std::string calendar_delegate_url_;
        std::string last_error_;
        std::mutex mutex_;
        std::vector<std::string> calendars_;
        
        // Parse the calendar JSON response into a vector of events
        std::vector<event> parse_response(const std::string& json_response) {
            std::vector<event> events;
            
            try {
                // Parse the JSON using glaze with the defined schemas
                CalendarResponse calendar_data;
                glz::context ctx{};
                auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(calendar_data, json_response, ctx);

                if (error) {
                    std::cerr << "Error parsing JSON: " << glz::format_error(error, json_response) << '\n';
                    std::cerr << "Source: " << json_response << '\n';
                    
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
                
                if (calendar_data.calendars) {
                    calendars_ = *calendar_data.calendars;
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to parse calendar response: ") + e.what());
            }
            
            return events;
        }
    };
}
