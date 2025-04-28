#pragma once

#include "imgui.h"
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace rouen::helpers {

class DatePicker {
public:
    DatePicker() {
        // Initialize current date info for the date picker
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::localtime(&now_time_t);
        
        current_month = now_tm.tm_mon;
        current_year = now_tm.tm_year + 1900;
        
        // Set default colors for the date picker
        colors[0] = {0.2f, 0.5f, 0.8f, 1.0f}; // Primary button color
        colors[1] = {0.3f, 0.6f, 0.9f, 0.7f}; // Hover button color
        colors[2] = {0.2f, 0.3f, 0.7f, 1.0f}; // Date highlight color
        colors[3] = {0.1f, 0.1f, 0.15f, 0.9f}; // Background color
        colors[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // Text color
        colors[5] = {0.3f, 0.4f, 0.8f, 1.0f}; // Current day color
    }

    // Set the date picker colors
    void set_colors(const ImVec4& primary, const ImVec4& hover, const ImVec4& highlight,
                    const ImVec4& background, const ImVec4& text, const ImVec4& current_day) {
        colors[0] = primary;
        colors[1] = hover;
        colors[2] = highlight;
        colors[3] = background;
        colors[4] = text;
        colors[5] = current_day;
    }

    // Convert date info to string in YYYY-MM-DD format
    static std::string date_to_string(int year, int month, int day) {
        char date_str[11];
        std::snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", year, month + 1, day);
        return std::string(date_str);
    }
    
    // Get number of days in a month
    static int get_days_in_month(int year, int month) {
        static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 1) { // February
            if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
                return 29; // Leap year
            }
        }
        return days_in_month[month];
    }
    
    // Get the day of the week for the first day of the month (0 = Sunday, 6 = Saturday)
    static int get_first_day_of_month(int year, int month) {
        std::tm time_info = {};
        time_info.tm_year = year - 1900;
        time_info.tm_mon = month;
        time_info.tm_mday = 1;
        std::mktime(&time_info);
        return time_info.tm_wday;
    }
    
    // Static helper for parsing dates from string
    static std::chrono::system_clock::time_point parse_date(const std::string& date_str) {
        std::tm tm = {};
        std::stringstream ss(date_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    // Static helper for formatting dates
    static std::string format_date(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%b %d, %Y");
        return ss.str();
    }

    // Static helper for formatting dates with time
    static std::string format_date_time(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%b %d, %Y %H:%M");
        return ss.str();
    }
    
    // Render a date picker popup
    bool render(char* date_buffer, size_t buffer_size, const char* popup_id) {
        bool value_changed = false;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, colors[3]);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        
        if (ImGui::BeginPopup(popup_id)) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors[4]);
            
            // Month and year selection with navigation buttons
            ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[1]);
            
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
                    ImGui::PushStyleColor(ImGuiCol_Text, colors[5]);
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

    // Convenience function to render a date field with picker
    bool render_date_field(const char* label, char* date_buffer, size_t buffer_size, 
                           const char* popup_id, bool* open_picker = nullptr) {
        bool value_changed = false;
        
        // Label (if provided)
        if (label && label[0] != '\0') {
            ImGui::Text("%s", label);
            ImGui::SameLine(100);
        }
        
        ImGui::PushItemWidth(300);
        
        // Date input field
        ImGui::InputText(std::string("##" + std::string(popup_id)).c_str(), 
                          date_buffer, buffer_size);
        
        // Show placeholder text for date format
        if (date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
            auto pos = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + 5, pos.y + 2),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                "YYYY-MM-DD"
            );
        }
        
        // Check if the date field was clicked or if we should open picker
        bool should_open = ImGui::IsItemClicked();
        if (open_picker && *open_picker) {
            should_open = true;
            *open_picker = false;
        }
        
        if (should_open) {
            ImGui::OpenPopup(popup_id);
        }
        
        // Calendar button
        ImGui::SameLine();
        if (ImGui::Button(std::string("📅##" + std::string(popup_id)).c_str())) {
            if (open_picker) *open_picker = true;
            else ImGui::OpenPopup(popup_id);
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select date from calendar");
        }
        
        // Render the date picker popup
        if (render(date_buffer, buffer_size, popup_id)) {
            value_changed = true;
        }
        
        ImGui::PopItemWidth();
        
        return value_changed;
    }

private:
    int current_month;
    int current_year;
    ImVec4 colors[6]; // Array of colors for the date picker
};

} // namespace rouen::helpers