#pragma once

#include <imgui/imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <format>
#include <chrono>
#include <functional>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

#include "../interface/card.hpp"
#include "../../hosts/travel_host.hpp"
#include "../../models/travel/plan.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

// Card for displaying and editing a specific travel plan
class travel_plan : public card {
public:
    travel_plan(std::string_view plan_id_str) {
        // Set custom colors for the Travel Plan card
        colors[0] = {0.2f, 0.5f, 0.8f, 1.0f}; // Blue primary color
        colors[1] = {0.3f, 0.6f, 0.9f, 0.7f}; // Lighter blue secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(0.4f, 0.7f, 1.0f, 1.0f)); // Light blue for titles
        get_color(3, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); // Green for active/positive
        get_color(4, ImVec4(0.8f, 0.4f, 0.4f, 1.0f)); // Red for warning/negative
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        
        // Additional colors for date picker
        get_color(6, ImVec4(0.2f, 0.3f, 0.7f, 1.0f)); // Date picker highlight
        get_color(7, ImVec4(0.1f, 0.1f, 0.15f, 0.9f)); // Date picker background
        get_color(8, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // Date picker text
        get_color(9, ImVec4(0.3f, 0.4f, 0.8f, 1.0f)); // Date picker current day
        
        width = 600.0f;
        requested_fps = 1;  // Update once per second
        
        // Initialize the Travel host controller
        travel_host = hosts::TravelHost::getHost();
        
        // Parse the plan ID and load the plan
        try {
            plan_id = std::stoll(std::string(plan_id_str));
            plan_ptr = travel_host->getPlan(plan_id);
            
            if (plan_ptr) {
                name(std::format("Trip: {}", plan_ptr->title));
            } else {
                name("Travel Plan (Not Found)");
            }
        } catch (const std::exception& e) {
            name("Travel Plan (Invalid ID)");
        }
        
        // Initialize current date info for the date picker
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::localtime(&now_time_t);
        
        current_month = now_tm.tm_mon;
        current_year = now_tm.tm_year + 1900;
    }
    
    ~travel_plan() override = default;

    // Helper for formatting dates
    std::string format_date(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%b %d, %Y");
        return ss.str();
    }

    // Helper for formatting dates with time
    std::string format_date_time(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%b %d, %Y %H:%M");
        return ss.str();
    }

    // Helper for parsing dates from string
    std::chrono::system_clock::time_point parse_date(const std::string& date_str) {
        std::tm tm = {};
        std::stringstream ss(date_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    
    // Convert date info to string in YYYY-MM-DD format
    std::string date_to_string(int year, int month, int day) {
        char date_str[11];
        std::snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", year, month + 1, day);
        return std::string(date_str);
    }
    
    // Get number of days in a month
    int get_days_in_month(int year, int month) {
        static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 1) { // February
            if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
                return 29; // Leap year
            }
        }
        return days_in_month[month];
    }
    
    // Get the day of the week for the first day of the month (0 = Sunday, 6 = Saturday)
    int get_first_day_of_month(int year, int month) {
        std::tm time_info = {};
        time_info.tm_year = year - 1900;
        time_info.tm_mon = month;
        time_info.tm_mday = 1;
        std::mktime(&time_info);
        return time_info.tm_wday;
    }
    
    // Render a date picker popup
    bool render_date_picker(char* date_buffer, size_t buffer_size, const char* popup_id) {
        bool value_changed = false;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, colors[7]);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        
        if (ImGui::BeginPopup(popup_id)) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors[8]);
            
            // Month and year selection with navigation buttons
            ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[6]);
            
            if (ImGui::Button("<<")) {
                current_year--;
            }
            ImGui::SameLine();
            
            if (ImGui::Button("<")) {
                current_month--;
                if (current_month < 0) {
                    current_month = 11;
                    current_year--;
                }
            }
            ImGui::SameLine();
            
            // Month/Year display in the center
            char month_year[32];
            static const char* month_names[] = {
                "January", "February", "March", "April", "May", "June",
                "July", "August", "September", "October", "November", "December"
            };
            std::snprintf(month_year, sizeof(month_year), "%s %d", month_names[current_month], current_year);
            
            float month_year_width = ImGui::CalcTextSize(month_year).x;
            float available_width = ImGui::GetContentRegionAvail().x;
            float month_year_pos = (available_width - month_year_width) * 0.5f;
            
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + month_year_pos - 45.0f); // Adjust as needed
            ImGui::Text("%s", month_year);
            
            ImGui::SameLine(available_width - 60.0f); // Position for next/next year buttons
            
            if (ImGui::Button(">")) {
                current_month++;
                if (current_month > 11) {
                    current_month = 0;
                    current_year++;
                }
            }
            ImGui::SameLine();
            
            if (ImGui::Button(">>")) {
                current_year++;
            }
            
            ImGui::PopStyleColor(2); // Pop button colors
            
            // Day of week headers
            ImGui::Spacing();
            const char* day_names[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
            ImGui::Columns(7, "day_names", false);
            for (int i = 0; i < 7; i++) {
                ImGui::Text("%s", day_names[i]);
                ImGui::NextColumn();
            }
            
            // Calendar grid
            ImGui::Columns(7, "days", false);
            
            // Get the first day of the month and number of days
            int first_day = get_first_day_of_month(current_year, current_month);
            int days_in_month = get_days_in_month(current_year, current_month);
            
            // Current day info for highlighting
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm = *std::localtime(&now_time_t);
            bool is_current_month = (now_tm.tm_mon == current_month && now_tm.tm_year + 1900 == current_year);
            
            // Empty cells before the first day
            for (int i = 0; i < first_day; i++) {
                ImGui::NextColumn();
            }
            
            // Day cells
            for (int day = 1; day <= days_in_month; day++) {
                // Highlight current day
                bool is_today = (is_current_month && day == now_tm.tm_mday);
                if (is_today) {
                    ImGui::PushStyleColor(ImGuiCol_Text, colors[9]);
                }
                
                char day_str[3];
                std::snprintf(day_str, sizeof(day_str), "%d", day);
                
                // Make day selectable
                if (ImGui::Selectable(day_str, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    // Format the selected date to YYYY-MM-DD and store in the buffer
                    std::snprintf(date_buffer, buffer_size, "%04d-%02d-%02d", current_year, current_month + 1, day);
                    value_changed = true;
                    ImGui::CloseCurrentPopup();
                }
                
                if (is_today) {
                    ImGui::PopStyleColor();
                }
                
                ImGui::NextColumn();
                
                // Break to a new row after Saturday
                if ((first_day + day) % 7 == 0) {
                    ImGui::Separator();
                }
            }
            
            ImGui::Columns(1);
            ImGui::Separator();
            
            // Today button
            ImGui::Spacing();
            if (ImGui::Button("Today", ImVec2(120, 0))) {
                // Set to today's date
                std::snprintf(date_buffer, buffer_size, "%04d-%02d-%02d", 
                    now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);
                value_changed = true;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            // Close button
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::PopStyleColor(); // Pop text color
            ImGui::EndPopup();
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        
        return value_changed;
    }
    
    void render_plan_details() {
        // Plan not found
        if (!plan_ptr) {
            ImGui::TextColored(colors[4], "Travel plan not found");
            if (ImGui::Button("Back to Travel Plans")) {
                "create_card"_sfn("travel");
                return;
            }
            return;
        }
        
        // Main plan details
        ImGui::BeginGroup();
        
        // Title with timeframe badge
        ImGui::TextColored(colors[2], "%s", plan_ptr->title.c_str());
        ImGui::SameLine();
        ImGui::TextColored(colors[5], "(%s)", plan_ptr->timeframe().c_str());
        
        // Description
        ImGui::TextWrapped("%s", plan_ptr->description.c_str());
        
        // Dates
        ImGui::Spacing();
        ImGui::TextColored(colors[3], "Dates: %s - %s (%d days)",
            format_date(plan_ptr->start_date).c_str(),
            format_date(plan_ptr->end_date).c_str(),
            plan_ptr->total_days());
        
        // Budget
        ImGui::TextColored(colors[3], "Budget: $%.2f", plan_ptr->total_budget);
        
        // Status with colored indicator
        ImVec4 status_color;
        switch (plan_ptr->current_status) {
            case media::travel::plan::status::planning:
                status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
                break;
            case media::travel::plan::status::booked:
                status_color = ImVec4(0.4f, 0.4f, 0.8f, 1.0f); // Blue
                break;
            case media::travel::plan::status::active:
                status_color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); // Green
                break;
            case media::travel::plan::status::completed:
                status_color = ImVec4(0.8f, 0.8f, 0.4f, 1.0f); // Yellow
                break;
            case media::travel::plan::status::cancelled:
                status_color = ImVec4(0.8f, 0.4f, 0.4f, 1.0f); // Red
                break;
            default:
                status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
        }
        
        ImGui::TextColored(status_color, "Status: %s", 
            media::travel::plan::status_to_string(plan_ptr->current_status).c_str());
        
        ImGui::EndGroup();
        
        // Status change buttons
        ImGui::Spacing();
        ImGui::TextColored(colors[2], "Update Status:");
        
        if (ImGui::Button("Planning", ImVec2(90, 0))) {
            update_status(media::travel::plan::status::planning);
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Booked", ImVec2(90, 0))) {
            update_status(media::travel::plan::status::booked);
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Active", ImVec2(90, 0))) {
            update_status(media::travel::plan::status::active);
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Completed", ImVec2(90, 0))) {
            update_status(media::travel::plan::status::completed);
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancelled", ImVec2(90, 0))) {
            update_status(media::travel::plan::status::cancelled);
        }
    }
    
    void render_destinations() {
        // Destinations section
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(colors[2], "Destinations");
        
        if (plan_ptr->destinations.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No destinations added yet");
        } else {
            // Display each destination
            if (ImGui::BeginTable("destinations", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Name/Location");
                ImGui::TableSetupColumn("Dates");
                ImGui::TableSetupColumn("Accommodation");
                ImGui::TableSetupColumn("Budget");
                ImGui::TableHeadersRow();
                
                for (size_t i = 0; i < plan_ptr->destinations.size(); i++) {
                    const auto& dest = plan_ptr->destinations[i];
                    
                    ImGui::TableNextRow();
                    
                    // Name and location
                    ImGui::TableNextColumn();
                    ImGui::TextColored(dest.completed ? colors[3] : colors[0], 
                                     "%s", dest.name.c_str());
                    ImGui::TextColored(colors[5], "%s", dest.location.c_str());
                    if (!dest.notes.empty()) {
                        ImGui::TextDisabled("(?)");
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(dest.notes.c_str());
                            ImGui::EndTooltip();
                        }
                    }
                    
                    // Dates
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", format_date(dest.arrival).c_str());
                    ImGui::TextColored(colors[5], "to");
                    ImGui::Text("%s", format_date(dest.departure).c_str());
                    
                    // Accommodation
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", dest.accommodation.empty() ? "-" : dest.accommodation.c_str());
                    
                    // Budget
                    ImGui::TableNextColumn();
                    ImGui::Text("$%.2f", dest.budget);
                    
                    // Mark as completed checkbox
                    ImGui::SameLine();
                    bool completed = dest.completed;
                    if (ImGui::Checkbox("##completed", &completed)) {
                        toggle_destination_completed(i, completed);
                    }
                }
                
                ImGui::EndTable();
            }
        }
    }
    
    void render_add_destination() {
        static char name_buffer[128] = "";
        static char location_buffer[128] = "";
        static char notes_buffer[256] = "";
        static char arrival_date_buffer[16] = "";
        static char departure_date_buffer[16] = "";
        static char accommodation_buffer[128] = "";
        static char budget_buffer[32] = "0.0";
        static bool show_form = false;
        static bool open_arrival_date_picker = false;
        static bool open_departure_date_picker = false;
        
        if (show_form) {
            ImGui::TextColored(colors[2], "Add New Destination");
            
            ImGui::Text("Name:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##name", name_buffer, sizeof(name_buffer));
            ImGui::PopItemWidth();
            
            ImGui::Text("Location:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##location", location_buffer, sizeof(location_buffer));
            ImGui::PopItemWidth();
            
            ImGui::Text("Notes:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputTextMultiline("##notes", notes_buffer, sizeof(notes_buffer), 
                                    ImVec2(300, 60));
            ImGui::PopItemWidth();
            
            ImGui::Text("Arrival:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            
            // Date input with picker button
            ImGui::InputText("##arrival", arrival_date_buffer, sizeof(arrival_date_buffer));
            
            // Show placeholder text for arrival date format
            if (arrival_date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "YYYY-MM-DD"
                );
            }
            
            // Check if the date field was clicked or if calendar button was clicked
            if (ImGui::IsItemClicked() || open_arrival_date_picker) {
                ImGui::OpenPopup("arrival_date_picker_popup");
                open_arrival_date_picker = false;
            }
            
            // Calendar button
            ImGui::SameLine();
            if (ImGui::Button("📅##arrival_date")) {
                open_arrival_date_picker = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Select date from calendar");
            }
            
            // Render the date picker popup with unique ID
            if (render_date_picker(arrival_date_buffer, sizeof(arrival_date_buffer), "arrival_date_picker_popup")) {
                // Date was selected
            }
            
            ImGui::PopItemWidth();
            
            ImGui::Text("Departure:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            
            // Date input with picker button
            ImGui::InputText("##departure", departure_date_buffer, sizeof(departure_date_buffer));
            
            // Show placeholder text for departure date format
            if (departure_date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "YYYY-MM-DD"
                );
            }
            
            // Check if the date field was clicked or if calendar button was clicked
            if (ImGui::IsItemClicked() || open_departure_date_picker) {
                ImGui::OpenPopup("departure_date_picker_popup");
                open_departure_date_picker = false;
            }
            
            // Calendar button
            ImGui::SameLine();
            if (ImGui::Button("📅##departure_date")) {
                open_departure_date_picker = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Select date from calendar");
            }
            
            // Render the date picker popup with unique ID
            if (render_date_picker(departure_date_buffer, sizeof(departure_date_buffer), "departure_date_picker_popup")) {
                // Date was selected
            }
            
            ImGui::PopItemWidth();
            
            ImGui::Text("Accommodation:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##accommodation", accommodation_buffer, sizeof(accommodation_buffer));
            ImGui::PopItemWidth();
            
            ImGui::Text("Budget:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##budget", budget_buffer, sizeof(budget_buffer));
            ImGui::PopItemWidth();
            
            ImGui::Spacing();
            
            // Submit and cancel buttons
            if (ImGui::Button("Add Destination", ImVec2(150, 0))) {
                if (strlen(name_buffer) > 0 && strlen(location_buffer) > 0 && 
                    strlen(arrival_date_buffer) > 0 && strlen(departure_date_buffer) > 0) {
                    
                    // Parse dates
                    auto arrival = parse_date(arrival_date_buffer);
                    auto departure = parse_date(departure_date_buffer);
                    
                    // Parse budget
                    double budget = 0.0;
                    try {
                        budget = std::stod(budget_buffer);
                    } catch (...) {
                        budget = 0.0;
                    }
                    
                    // Add destination
                    if (travel_host->addDestination(
                        plan_id, 
                        name_buffer, 
                        location_buffer, 
                        arrival, 
                        departure, 
                        accommodation_buffer,
                        notes_buffer,
                        budget)) {
                        
                        // Clear form
                        name_buffer[0] = '\0';
                        location_buffer[0] = '\0';
                        notes_buffer[0] = '\0';
                        arrival_date_buffer[0] = '\0';
                        departure_date_buffer[0] = '\0';
                        accommodation_buffer[0] = '\0';
                        sprintf(budget_buffer, "0.0");
                        show_form = false;
                        
                        // Refresh plan data
                        plan_ptr = travel_host->getPlan(plan_id);
                    }
                }
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                // Clear form and hide
                name_buffer[0] = '\0';
                location_buffer[0] = '\0';
                notes_buffer[0] = '\0';
                arrival_date_buffer[0] = '\0';
                departure_date_buffer[0] = '\0';
                accommodation_buffer[0] = '\0';
                sprintf(budget_buffer, "0.0");
                show_form = false;
            }
        } else {
            if (ImGui::Button("+ Add Destination", ImVec2(150, 0))) {
                show_form = true;
            }
        }
    }

    bool render() override {
        return render_window([this]() {
            // Back to main travel card button
            if (ImGui::Button("< Back to Travel Plans")) {
                "create_card"_sfn("travel");
                return;
            }
            
            ImGui::Separator();
            
            // Plan details section
            render_plan_details();
            
            // Destinations section
            render_destinations();
            
            // Add destination form
            ImGui::Separator();
            render_add_destination();
        });
    }
    
private:
    // Helper to update the travel plan status
    void update_status(media::travel::plan::status new_status) {
        if (!plan_ptr) return;
        
        if (travel_host->updatePlan(
            plan_id,
            plan_ptr->title,
            plan_ptr->description,
            plan_ptr->start_date,
            plan_ptr->end_date,
            new_status,
            plan_ptr->total_budget
        )) {
            // Refresh plan data
            plan_ptr = travel_host->getPlan(plan_id);
        }
    }
    
    // Helper to toggle a destination as completed
    void toggle_destination_completed(size_t dest_index, bool completed) {
        if (!plan_ptr || dest_index >= plan_ptr->destinations.size()) return;
        
        // Make a copy of the plan to update
        auto plan_copy = *plan_ptr;
        plan_copy.destinations[dest_index].completed = completed;
        
        // Update in repository using the public method
        travel_host->updatePlanDirectly(plan_copy);
        
        // Refresh plan data
        plan_ptr = travel_host->getPlan(plan_id);
    }
    
    std::shared_ptr<hosts::TravelHost> travel_host;
    std::shared_ptr<media::travel::plan> plan_ptr;
    long long plan_id{-1};
    
    // Date picker state
    int current_month;
    int current_year;
};

} // namespace rouen::cards