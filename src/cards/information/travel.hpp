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
#include "../../registrar.hpp"
#include "travel_plan.hpp"

namespace rouen::cards {

// Main Travel Plans card that displays all plans
class travel : public card {
public:
    travel() {
        // Set custom colors for the Travel card
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
        
        name("Travel Plans");
        requested_fps = 1;  // Update once per second
        width = 400.0f;
        
        // Initialize the Travel host controller with a system runner
        travel_host = hosts::TravelHost::getHost();
        
        // Initialize current date info for the date picker
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::localtime(&now_time_t);
        
        current_month = now_tm.tm_mon;
        current_year = now_tm.tm_year + 1900;
    }
    
    ~travel() override = default;

    // Helper for formatting dates
    std::string format_date(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%b %d, %Y");
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
    
    void render_add_plan() {
        static char title_buffer[128] = "";
        static char description_buffer[512] = "";
        static char start_date_buffer[16] = "";
        static char end_date_buffer[16] = "";
        static char budget_buffer[32] = "0.0";
        static bool show_form = false;
        static bool open_start_date_picker = false;
        static bool open_end_date_picker = false;
        
        if (show_form) {
            ImGui::TextColored(colors[2], "Create New Travel Plan");
            
            ImGui::Text("Title:"); 
            ImGui::SameLine(80);
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##title", title_buffer, sizeof(title_buffer));
            ImGui::PopItemWidth();
            
            ImGui::Text("Description:"); 
            ImGui::SameLine(80);
            ImGui::PushItemWidth(-1);
            ImGui::InputTextMultiline("##description", description_buffer, sizeof(description_buffer), 
                                    ImVec2(-1, 60));
            ImGui::PopItemWidth();
            
            ImGui::Text("Start Date:"); 
            ImGui::SameLine(80);
            ImGui::PushItemWidth(-1);
            
            // Date input with picker button
            ImGui::InputText("##start_date", start_date_buffer, sizeof(start_date_buffer), 0, nullptr, nullptr);
            
            // Show placeholder text for start date format
            if (start_date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "YYYY-MM-DD"
                );
            }
            
            // Check if the date field was clicked or if calendar button was clicked
            if (ImGui::IsItemClicked() || open_start_date_picker) {
                ImGui::OpenPopup("start_date_picker_popup");
                open_start_date_picker = false;
            }
            
            // Calendar button
            ImGui::SameLine();
            if (ImGui::Button("📅##start_date")) {
                open_start_date_picker = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Select date from calendar");
            }
            
            // Render the date picker popup with unique ID
            if (render_date_picker(start_date_buffer, sizeof(start_date_buffer), "start_date_picker_popup")) {
                // Date was selected
            }
            
            ImGui::PopItemWidth();
            
            ImGui::Text("End Date:"); 
            ImGui::SameLine(80);
            ImGui::PushItemWidth(-1);
            
            // Date input with picker button
            ImGui::InputText("##end_date", end_date_buffer, sizeof(end_date_buffer), 0, nullptr, nullptr);
            
            // Show placeholder text for end date format
            if (end_date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "YYYY-MM-DD"
                );
            }
            
            // Check if the date field was clicked or if calendar button was clicked
            if (ImGui::IsItemClicked() || open_end_date_picker) {
                ImGui::OpenPopup("end_date_picker_popup");
                open_end_date_picker = false;
            }
            
            // Calendar button
            ImGui::SameLine();
            if (ImGui::Button("📅##end_date")) {
                open_end_date_picker = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Select date from calendar");
            }
            
            // Render the date picker popup with unique ID
            if (render_date_picker(end_date_buffer, sizeof(end_date_buffer), "end_date_picker_popup")) {
                // Date was selected
            }
            
            ImGui::PopItemWidth();
            
            ImGui::Text("Budget:"); 
            ImGui::SameLine(80);
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##budget", budget_buffer, sizeof(budget_buffer));
            ImGui::PopItemWidth();
            
            ImGui::Spacing();
            
            // Submit and cancel buttons
            if (ImGui::Button("Create Plan", ImVec2(120, 0))) {
                if (strlen(title_buffer) > 0 && strlen(start_date_buffer) > 0 && strlen(end_date_buffer) > 0) {
                    // Parse dates
                    auto start_date = parse_date(start_date_buffer);
                    auto end_date = parse_date(end_date_buffer);
                    
                    // Parse budget
                    double budget = 0.0;
                    try {
                        budget = std::stod(budget_buffer);
                    } catch (...) {
                        budget = 0.0;
                    }
                    
                    // Create plan
                    if (travel_host->createPlan(title_buffer, description_buffer, start_date, end_date, budget) > 0) {
                        // Clear form
                        title_buffer[0] = '\0';
                        description_buffer[0] = '\0';
                        start_date_buffer[0] = '\0';
                        end_date_buffer[0] = '\0';
                        sprintf(budget_buffer, "0.0");
                        show_form = false;
                    }
                }
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                // Clear form and hide
                title_buffer[0] = '\0';
                description_buffer[0] = '\0';
                start_date_buffer[0] = '\0';
                end_date_buffer[0] = '\0';
                sprintf(budget_buffer, "0.0");
                show_form = false;
            }
            
            ImGui::Separator();
        } else {
            if (ImGui::Button("+ New Plan", ImVec2(120, 0))) {
                show_form = true;
            }
            ImGui::Separator();
        }
    }

    bool render() override {
        return render_window([this]() {
            // Add plan form section
            render_add_plan();
            
            // Plans section title
            ImGui::TextColored(colors[2], "Your Travel Plans:");
            
            // Create scrollable area for travel plans
            if (ImGui::BeginChild("PlansScrollArea", ImVec2(0, 0), true)) {
                auto plans = travel_host->plans();
                if (plans.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No travel plans created yet");
                } else {
                    // Group plans by their timeframe
                    std::vector<std::string> timeframes = {"Current", "Upcoming", "Past"};
                    
                    for (const auto& timeframe : timeframes) {
                        bool has_plans_in_timeframe = false;
                        
                        // First check if there are any plans with this timeframe
                        for (const auto& plan : plans) {
                            if (plan->timeframe() == timeframe) {
                                has_plans_in_timeframe = true;
                                break;
                            }
                        }
                        
                        if (!has_plans_in_timeframe) {
                            continue;
                        }
                        
                        // Display timeframe header
                        ImGui::TextColored(colors[3], "%s", timeframe.c_str());
                        
                        // Display all plans in this timeframe
                        for (const auto& plan : plans) {
                            if (plan->timeframe() != timeframe) {
                                continue;
                            }
                            
                            ImGui::PushID(static_cast<int>(plan->id));
                            
                            ImGui::BeginGroup();
                            
                            // Plan title
                            std::string title = plan->title;
                            
                            // Limit title length and add ellipsis if too long
                            if (title.length() > 40) {
                                title = title.substr(0, 37) + "...";
                            }
                            
                            if (ImGui::Selectable(title.c_str(), false)) {
                                // Open travel plan details in a new card
                                std::string plan_uri = std::format("travel-plan:{}", plan->id);
                                "create_card"_sfn(plan_uri);
                            }
                            
                            // Date range
                            ImGui::SameLine();
                            ImGui::TextColored(
                                ImVec4(colors[5].x, colors[5].y, colors[5].z, colors[5].w),
                                "%s - %s", 
                                format_date(plan->start_date).c_str(),
                                format_date(plan->end_date).c_str()
                            );
                            
                            // Status
                            ImVec4 status_color;
                            switch (plan->current_status) {
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
                            
                            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                            ImGui::TextColored(
                                status_color,
                                "%s", media::travel::plan::status_to_string(plan->current_status).c_str()
                            );
                            
                            // Delete button
                            ImGui::SameLine(ImGui::GetWindowWidth() - 30);
                            if (ImGui::SmallButton("×")) {
                                plans_to_delete.push_back(plan->id);
                            }
                            
                            ImGui::EndGroup();
                            
                            ImGui::PopID();
                        }
                        
                        ImGui::Separator();
                    }
                    
                    // Process deletion requests
                    for (const auto& id : plans_to_delete) {
                        travel_host->deletePlan(id);
                    }
                    plans_to_delete.clear();
                }
            }
            ImGui::EndChild();
        });
    }
    
private:
    std::shared_ptr<hosts::TravelHost> travel_host;
    std::vector<long long> plans_to_delete;
    
    // Date picker state
    int current_month;
    int current_year;
};

} // namespace rouen::cards