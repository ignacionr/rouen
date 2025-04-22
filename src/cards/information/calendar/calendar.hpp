#pragma once

#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui/imgui.h>

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
            std::string calendar_url = url;
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
        
    private:
        std::shared_ptr<::calendar::calendar_fetcher> fetcher_;
        std::vector<::calendar::event> events_;
        std::mutex events_mutex_;
        std::chrono::steady_clock::time_point last_refresh_ = std::chrono::steady_clock::now();
        std::chrono::seconds refresh_interval_{300}; // Refresh every 5 minutes
        std::jthread refresh_thread_;                // Thread for refreshing events in the background
        bool show_event_details_ = false;
        ::calendar::event selected_event_;
        
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
            get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        }
        
        void render_status_bar()
        {
            auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_refresh_).count();
            auto minutes = elapsed_seconds / 60;
            auto seconds = elapsed_seconds % 60;
            
            ImGui::Text("Last refresh: %ld:%02ld ago", minutes, seconds);
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
            
            if (show_event_details_) {
                render_event_details();
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
                        selected_event_ = event;
                        show_event_details_ = true;
                    }
                    
                    // Location column
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", event.location.c_str());
                }
                
                ImGui::EndTable();
            }
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
    };
} // namespace rouen::cards