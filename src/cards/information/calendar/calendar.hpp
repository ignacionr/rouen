#pragma once

#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>

#include "imgui.h"
// Include our compatibility layer for C++20/23 features
#include "../../../helpers/compat/compat.hpp"
// Include Material Design icons
#include "../../../../external/IconsMaterialDesign.h"

#include "../../../models/calendar/calendar_fetcher.hpp"
#include "../../../models/calendar/event.hpp"
#include "../../../registrar.hpp"
#include "../../interface/card.hpp"

namespace rouen::cards
{
    class calendar : public card
    {
    public:
        calendar(const std::string &url = {})
        {
            // Setup colors
            setup_colors();
            
            // Set card properties
            name("Calendar");
            width = 600.0f;
            
            // Get URL from parameter or environment
            calendar_url = url;
            if (calendar_url.empty()) {
                const char* env_url = std::getenv("CALENDAR_DELEGATE_URL");
                if (env_url) {
                    calendar_url = env_url;
                }
            }
            
            // Initialize the calendar fetcher
            fetcher_ = std::make_shared<::calendar::calendar_fetcher>(calendar_url);
            
            // Initial load of events
            refresh_events();
            
            // Start the refresh thread
            start_refresh_thread();
        }
        
        ~calendar() override {
            // The refresh_thread_ is a jthread, which will automatically join when destroyed
        }

        bool render() override
        {
            return render_window([this]()
            {
                // Header area
                ImGui::PushStyleColor(ImGuiCol_Text, colors[2]); // Use title color
                ImGui::Text("Calendar Events");
                ImGui::PopStyleColor();
                
                // Status bar
                render_status_bar();
                
                ImGui::Separator();
                
                // Calendar events area
                render_events();
                
                // Handle automatic refresh
                if (auto now = std::chrono::steady_clock::now(); 
                    std::chrono::duration_cast<std::chrono::seconds>(now - last_refresh_).count() >= refresh_interval_.count()) {
                    refresh_events();
                }
            });
        }

        std::string get_uri() const override
        {
            return calendar_url.empty() ? "calendar" : std::format("calendar:{}", calendar_url);
        }
        
    private:
        std::shared_ptr<::calendar::calendar_fetcher> fetcher_;
        std::vector<::calendar::event> events_;
        std::mutex events_mutex_;
        std::chrono::steady_clock::time_point last_refresh_ = std::chrono::steady_clock::now();
        std::chrono::seconds refresh_interval_{300}; // Refresh every 5 minutes
        std::jthread refresh_thread_;                // Thread for refreshing events in the background
        bool show_event_details_ = false;
        ::calendar::event selected_event_;
        bool use_day_view_ {true};                  // Toggle between list view and day view
        std::string current_date_;              // Current date for day view
        std::string calendar_url;                    // Calendar URL
        
        // Helper methods for DRY code
        void setup_colors()
        {
            // Set custom colors for the Calendar card
            colors[0] = {0.2f, 0.6f, 0.6f, 1.0f}; // Teal primary color
            colors[1] = {0.3f, 0.7f, 0.7f, 0.7f}; // Lighter teal secondary color

            // Additional colors for specific elements
            get_color(2, ImVec4(0.3f, 0.8f, 0.8f, 1.0f)); // Light teal for titles
            get_color(3, ImVec4(0.4f, 0.8f, 0.4f, 1.0f)); // Green for active/positive elements
            get_color(4, ImVec4(0.8f, 0.8f, 0.3f, 1.0f)); // Yellow for warnings/highlights
            get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 0.7f)); // Light gray for secondary text
        }
        
        void render_status_bar()
        {
            auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_refresh_).count();
            auto minutes = elapsed_seconds / 60;
            auto seconds = elapsed_seconds % 60;
            
            ImGui::Text("Last refresh: %lld:%02lld ago", minutes, seconds);
            ImGui::SameLine();
            
            if (ImGui::Button("Refresh Now")) {
                refresh_events();
            }
            
            // Error message if fetch failed
            if (fetcher_->has_error()) {
                // ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); // Red for error
                ImGui::Text("Error: %s", fetcher_->last_error().c_str());
                ImGui::PopStyleColor();
            }
        }
        
        void render_events()
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            
            if (events_.empty()) {
                ImGui::Text("No upcoming events found.");
                return;
            }
            
            // View mode toggle
            if (ImGui::Button(use_day_view_ ? "Switch to List View" : "Switch to Day View")) {
                use_day_view_ = !use_day_view_;
                
                // If switching to day view and no date is selected, use today's date
                if (use_day_view_ && current_date_.empty()) {
                    auto now = std::chrono::system_clock::now();
                    auto now_time_t = std::chrono::system_clock::to_time_t(now);
                    std::tm now_tm = *std::localtime(&now_time_t);
                    
                    char date_buf[11];
                    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &now_tm);
                    current_date_ = date_buf;
                }
            }
            
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("List view shows all upcoming events.\nDay view shows events for a specific day.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            
            if (show_event_details_) {
                render_event_details();
            } else if (use_day_view_) {
                render_day_view();
            } else {
                render_events_list();
            }
        }
        
        void render_events_list()
        {
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;
            
            if (ImGui::BeginTable("events_table", 3, flags, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                
                // Loop through calendar events
                for (const auto& event : events_) {
                    ImGui::TableNextRow();
                    
                    // Time column
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", event.format_time().c_str());
                    
                    // Summary column
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::Selectable(event.summary.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                        if (ImGui::GetIO().KeyCtrl) {
                            // Create alarm card when Ctrl+click on event
                            create_alarm_from_event(event);
                        } else {
                            // Normal selection behavior
                            selected_event_ = event;
                            show_event_details_ = true;
                        }
                    }
                    
                    // Location column
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", event.location.c_str());
                }
                
                ImGui::EndTable();
            }
        }
        
        void render_day_view()
        {
            // Date picker for day view
            char date_buf[11];
            std::strncpy(date_buf, current_date_.c_str(), sizeof(date_buf));
            date_buf[10] = '\0'; // Ensure null termination
            
            // Display current date with navigation buttons
            ImGui::PushStyleColor(ImGuiCol_Button, colors[1]);
            
            if (ImGui::Button(ICON_MD_CHEVRON_LEFT)) { // Using Material Design icon instead of "<<"
                // Parse current date
                std::tm date_tm = {};
                std::istringstream ss(current_date_);
                ss >> std::get_time(&date_tm, "%Y-%m-%d");
                
                // Subtract one day
                auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&date_tm));
                time_point -= std::chrono::hours(24);
                auto new_time_t = std::chrono::system_clock::to_time_t(time_point);
                std::tm new_tm = *std::localtime(&new_time_t);
                
                // Format new date
                std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &new_tm);
                current_date_ = date_buf;
            }
            
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
            ImGui::Text("%s", current_date_.c_str());
            ImGui::PopStyleColor();
            
            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_CHEVRON_RIGHT)) { // Using Material Design icon instead of ">>"
                // Parse current date
                std::tm date_tm = {};
                std::istringstream ss(current_date_);
                ss >> std::get_time(&date_tm, "%Y-%m-%d");
                
                // Add one day
                auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&date_tm));
                time_point += std::chrono::hours(24);
                auto new_time_t = std::chrono::system_clock::to_time_t(time_point);
                std::tm new_tm = *std::localtime(&new_time_t);
                
                // Format new date
                std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &new_tm);
                current_date_ = date_buf;
            }
            
            bool is_today = (current_date_ == std::format("{:%Y-%m-%d}", std::chrono::system_clock::now()));
            if (!is_today) {
                ImGui::SameLine();
                if (ImGui::Button("Today")) {
                    auto now = std::chrono::system_clock::now();
                    auto now_time_t = std::chrono::system_clock::to_time_t(now);
                    std::tm now_tm = *std::localtime(&now_time_t);
                    
                    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &now_tm);
                    current_date_ = date_buf;
                }
            }

            ImGui::PopStyleColor();
            ImGui::Separator();
            
            // Group events by time (hourly slots)
            std::map<int, std::vector<const ::calendar::event*>> hourly_events;
            bool has_all_day_events = false;
            
            // First pass: organize events by hour
            for (const auto& event : events_) {
                if (event.start.substr(0, 10) == current_date_) {
                    if (event.all_day) {
                        has_all_day_events = true;
                        continue;
                    }
                    
                    // Extract hour from time (format: "YYYY-MM-DDTHH:MM:SS")
                    int hour = 0;
                    if (event.start.length() >= 13) {
                        hour = std::stoi(event.start.substr(11, 2));
                    }
                    
                    hourly_events[hour].push_back(&event);
                }
            }
            
            // Render all-day events first if any
            if (has_all_day_events) {
                ImGui::PushStyleColor(ImGuiCol_Text, colors[4]); // Use highlight color
                ImGui::Text("All-day Events:");
                ImGui::PopStyleColor();
                
                for (const auto& event : events_) {
                    if (event.start.substr(0, 10) == current_date_ && event.all_day) {
                        render_day_view_event(event);
                    }
                }
                
                ImGui::Separator();
            }
            
            // Render time slots (from 0 to 23 hours)
            int current_hour = is_today ? std::chrono::duration_cast<std::chrono::hours>(
                std::chrono::system_clock::now().time_since_epoch()).count() % 24 : -1;
            for (int hour = 0; hour < 24; hour++) {
                std::string time_label = std::format("{:02d}:00", hour);
                
                auto const label_color {hour == current_hour ? colors[4] : colors[2]};
                // Check if we have events for this hour
                if (hourly_events.find(hour) != hourly_events.end() && !hourly_events[hour].empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, label_color); // Title color
                    ImGui::Text("%s", time_label.c_str());
                    ImGui::PopStyleColor();
                    
                    // Render events for this hour
                    for (const auto* event_ptr : hourly_events[hour]) {
                        render_day_view_event(*event_ptr);
                    }
                } else {
                    // Display empty time slot with lighter color
                    ImGui::PushStyleColor(ImGuiCol_Text, label_color);
                    ImGui::Text("%s", time_label.c_str());
                    ImGui::PopStyleColor();
                }
                
                // Add a subtle separator between hours
                ImGui::Separator();
            }
        }

        void render_day_view_event(const ::calendar::event& event)
        {
            // Calculate the event time display
            std::string time_display;
            if (event.all_day) {
                time_display = "All day";
            } else if (event.start.length() >= 16) {
                // Extract hour:minute from the start time (format: "YYYY-MM-DDTHH:MM:SS")
                time_display = event.start.substr(11, 5);
            }
            
            // Event card style
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.3f, 0.3f, 0.3f));
            
            // Calculate minimum height for the event card based on content
            // Use a reasonable minimum height that works well on macOS
            const float min_event_height = ImGui::GetTextLineHeightWithSpacing() * 2.5f;
            
            ImGui::BeginChild(("event_" + event.id).c_str(), 
                             ImVec2(ImGui::GetContentRegionAvail().x, min_event_height), 
                             true);
            
            // Event summary with click to view details
            ImGui::PushStyleColor(ImGuiCol_Text, colors[3]); // Use the active/positive color
            if (ImGui::Selectable(event.summary.c_str(), false)) {
                if (ImGui::GetIO().KeyCtrl) {
                    // Create alarm card when Ctrl+click on event
                    create_alarm_from_event(event);
                } else {
                    // Normal selection behavior
                    selected_event_ = event;
                    show_event_details_ = true;
                }
            }
            ImGui::PopStyleColor();
            
            // Event time
            if (!event.all_day) {
                ImGui::Text("Time: %s", time_display.c_str());
            }
            
            // Event location if available
            if (!event.location.empty()) {
                ImGui::Text("Location: %s", event.location.c_str());
            }
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
        }
        
        void render_event_details()
        {
            // Back button
            if (ImGui::Button("Back to Events List")) {
                show_event_details_ = false;
            }
            
            ImGui::Separator();
            
            // Event details
            ImGui::PushStyleColor(ImGuiCol_Text, colors[2]); // Title color
            ImGui::Text("%s", selected_event_.summary.c_str());
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
            
            // Time
            if (selected_event_.all_day) {
                ImGui::Text("Date: %s (All day)", selected_event_.start.substr(0, 10).c_str());
            } else {
                ImGui::Text("Start: %s", selected_event_.start.c_str());
                ImGui::Text("End:   %s", selected_event_.end.c_str());
            }
            
            // Add Set Alarm button
            if (!selected_event_.all_day) {
                ImGui::SameLine();
                if (ImGui::Button("Set Alarm")) {
                    create_alarm_from_event(selected_event_);
                }
                
                // Add tooltip explanation
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Create an alarm card for this event");
                    ImGui::EndTooltip();
                }
            }
            
            // Location
            if (!selected_event_.location.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("Location: %s", selected_event_.location.c_str());
            }
            
            // Description
            if (!selected_event_.description.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, colors[5]); // Secondary text color
                ImGui::TextWrapped("Description: %s", selected_event_.description.c_str());
                ImGui::PopStyleColor();
            }
            
            // Organizer
            if (!selected_event_.organizer.empty()) {
                ImGui::Spacing();
                ImGui::Text("Organizer: %s", selected_event_.organizer.c_str());
            }
            
            // Link to Google Calendar
            if (!selected_event_.htmlLink.empty()) {
                ImGui::Spacing();
                if (ImGui::Button("Open in Google Calendar")) {
                    // Open link using xdg-open on Linux
                    std::string cmd = "xdg-open \"" + selected_event_.htmlLink + "\" &";
                    system(cmd.c_str());
                }
            }
        }
        
        void refresh_events()
        {
            try {
                auto new_events = fetcher_->fetch_events();
                
                {
                    std::lock_guard<std::mutex> lock(events_mutex_);
                    events_ = std::move(new_events);
                }
                
                last_refresh_ = std::chrono::steady_clock::now();
            } catch (const std::exception& e) {
                // Error will be shown in the UI via fetcher_->has_error()
            }
        }
        
        void start_refresh_thread()
        {
            refresh_thread_ = std::jthread([this](std::stop_token stoken) {
                while (!stoken.stop_requested()) {
                    // Sleep for a minute before checking if refresh is needed
                    for (int i = 0; i < 60 && !stoken.stop_requested(); ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    
                    if (stoken.stop_requested()) break;
                    
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_refresh_).count();
                    
                    // If the refresh interval has passed, refresh the events
                    if (elapsed >= refresh_interval_.count()) {
                        refresh_events();
                    }
                }
            });
        }

        // Add helper method to create alarm from event
        void create_alarm_from_event(const ::calendar::event& event) {
            if (event.all_day) {
                // Don't create alarms for all-day events
                return;
            }
            
            // Extract the time from the event start time
            // Format is typically "YYYY-MM-DDTHH:MM:SS"
            if (event.start.length() >= 16) {
                std::string time_str = event.start.substr(11, 5); // Extract HH:MM
                
                // Create alarm card with the event time
                "create_card"_sfn(std::format("alarm:{}", time_str));
            }
        }
    };
} // namespace rouen::cards