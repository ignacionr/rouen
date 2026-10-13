#pragma once

#include "../../helpers/imgui_include.hpp"
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
#include "../../helpers/debug.hpp" // Add debug header
#include "travel_plan.hpp"
#include "../../../external/IconsMaterialDesign.h"
#include "../../helpers/ui_context.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../fonts.hpp"

namespace rouen::cards {

struct checklist_item {
    std::string id;
    std::string task;
    bool completed{false};
};

struct reservation_entry {
    std::string type;
    std::string details;
};

// Main Travel Plans card that displays all plans
class travel : public card {
public:
    travel() {
        DB_INFO("Travel card: Constructor starting");
        
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
        requested_fps = 5;  // Higher FPS for smoother loading states
        width = 480.0f;
        
        DB_INFO("Travel card: Getting TravelHost");
        
        // Get travel host in a try-catch block to capture any initialization errors
        try {
            // Initialize the Travel host controller with a system runner
            travel_host = hosts::TravelHost::getHost();
            is_loading = true;
            DB_INFO("Travel card: Successfully obtained TravelHost");
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Travel card: Exception getting TravelHost: {}", e.what());
        } catch (...) {
            DB_ERROR("Travel card: Unknown exception getting TravelHost");
        }
        
        // Initialize current date info for the date picker
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::localtime(&now_time_t);
        
        current_month = now_tm.tm_mon;
        current_year = now_tm.tm_year + 1900;
        
        last_refresh_time = std::chrono::steady_clock::now();
        
        DB_INFO("Travel card: Constructor completed");
    }
    
    ~travel() override {
        DB_INFO("Travel card: Destructor called");
    }

    // Helper for formatting dates
    static std::string format_date(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%b %d, %Y");
        return ss.str();
    }

    // Helper for parsing dates from string
    static std::chrono::system_clock::time_point parse_date(const std::string& date_str) {
        std::tm tm = {};
        std::stringstream ss(date_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    
    // Convert date info to string in YYYY-MM-DD format
    static std::string date_to_string(int year, int month, int day) {
        // Use std::format instead of std::snprintf for modern C++ style and type safety
        return std::format("{:04d}-{:02d}-{:02d}", year, month + 1, day);
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
            static const char* month_names[] = {
                "January", "February", "March", "April", "May", "June",
                "July", "August", "September", "October", "November", "December"
            };
            
            // Use std::format for type-safe formatting
            std::string month_year_str = std::format("{} {}", month_names[current_month], current_year);
            
            float month_year_width = ImGui::CalcTextSize(month_year_str.c_str()).x;
            float available_width = ImGui::GetContentRegionAvail().x;
            float month_year_pos = (available_width - month_year_width) * 0.5f;
            
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + month_year_pos - 45.0f); // Adjust as needed
            ImGui::Text("%s", month_year_str.c_str());
            
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
                // Use std::format for day numbers
                std::string day_fmt = std::format("{}", day);
                std::strncpy(day_str, day_fmt.c_str(), sizeof(day_str) - 1);
                day_str[sizeof(day_str) - 1] = '\0';  // Ensure null termination
                
                // Make day selectable
                if (ImGui::Selectable(day_str, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    // Format the selected date to YYYY-MM-DD and store in the buffer
                    std::string formatted_date = std::format("{:04d}-{:02d}-{:02d}", current_year, current_month + 1, day);
                    std::strncpy(date_buffer, formatted_date.c_str(), buffer_size - 1);
                    date_buffer[buffer_size - 1] = '\0'; // Ensure null termination
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
                // Set to today's date using std::format
                std::string today_date = std::format("{:04d}-{:02d}-{:02d}", 
                    now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);
                std::strncpy(date_buffer, today_date.c_str(), buffer_size - 1);
                date_buffer[buffer_size - 1] = '\0'; // Ensure null termination
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
            if (ImGui::Button(ICON_MD_CALENDAR_TODAY "##start_date")) {
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
            if (ImGui::Button(ICON_MD_CALENDAR_TODAY "##end_date")) {
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
                        
                        // Use std::format for budget formatting
                        std::string budget_fmt = std::format("{:.1f}", 0.0);
                        std::strncpy(budget_buffer, budget_fmt.c_str(), sizeof(budget_buffer) - 1);
                        budget_buffer[sizeof(budget_buffer) - 1] = '\0';
                        
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
                
                // Use std::format for budget formatting
                std::string budget_fmt = std::format("{:.1f}", 0.0);
                std::strncpy(budget_buffer, budget_fmt.c_str(), sizeof(budget_buffer) - 1);
                budget_buffer[sizeof(budget_buffer) - 1] = '\0';
                
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
    
    void render_loading_spinner() {
        // Add a loading spinner animation
        const float RADIUS = 15.0f;
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 center = ImVec2(pos.x + RADIUS, pos.y + RADIUS);
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Get current time for animation
        auto time = ImGui::GetTime() * 5.0;
        
        // Draw spinner segments
        const int NUM_SEGMENTS = 8;
        for (int i = 0; i < NUM_SEGMENTS; i++) {
            auto t = time + i * 0.25;
            auto a = (t * M_PI * 2.0) / NUM_SEGMENTS;
            auto b = ((t + 0.25) * M_PI * 2.0) / NUM_SEGMENTS;
            
            // Calculate alpha based on time
            float alpha = 0.1f + 0.9f * (1.0f - static_cast<float>(i) / static_cast<float>(NUM_SEGMENTS));
            ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(colors[0].x, colors[0].y, colors[0].z, alpha));
            
            draw_list->PathArcTo(center, RADIUS, static_cast<float>(a), static_cast<float>(b), 12);
            draw_list->PathStroke(color, false, 2.0f);
        }
        
        // Add some space for the spinner
        ImGui::Dummy(ImVec2(RADIUS * 2 + 10, RADIUS * 2 + 10));
        ImGui::SameLine();
        ImGui::Text("Loading travel plans...");
    }

    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
        DB_INFO("Travel card: Render starting");
        
        // Check if we need to refresh our loading state
        auto now = std::chrono::steady_clock::now();
        if (is_loading && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh_time).count() > 100) {
            // Check if TravelHost has completed initialization
            if (!travel_host) {
                DB_ERROR("Travel card: TravelHost is null when checking loading state");
            } else {
                // Accept empty plan vectors as valid initialized state, don't wait for plans to exist
                is_loading = false;
                DB_INFO("Travel card: Loading completed (accepting empty plans as valid)");
            }
            last_refresh_time = now;
        }
        
        bool result = render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now_t = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_t - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now_t;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

            try {
                // Add plan form section
                render_add_plan();
                
                // Plans section title
                ImGui::TextColored(colors[2], "Your Travel Plans:");
                
                if (!travel_host) {
                    DB_ERROR("Travel card: TravelHost is null in render");
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                                     "Error: Travel host not initialized");
                    return;
                }
                
                if (is_loading) {
                    // Show loading spinner while waiting for initialization
                    render_loading_spinner();
                    return;
                }
                
                // Create scrollable area for travel plans
                if (ImGui::BeginChild("PlansScrollArea", ImVec2(0, 0), true)) {
                    DB_INFO("Travel card: Getting plans from host");
                    auto plans = travel_host->plans();
                    DB_INFO_FMT("Travel card: Got {} plans", plans.size());
                    
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
                            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                            ImGui::TextColored(colors[2], "%s Trips", timeframe.c_str());
                            ImGui::PopFont();
                            ImGui::Separator();
                            ImGui::Spacing();
                            
                            // Display all plans in this timeframe
                            for (const auto& plan : plans) {
                                if (plan->timeframe() != timeframe) {
                                    continue;
                                }
                                
                                ImGui::PushID(static_cast<int>(plan->id));
                                
                                // Set up status color
                                ImVec4 status_color;
                                switch (plan->current_status) {
                                    case media::travel::plan::status::planning:
                                        status_color = ImVec4(0.9f, 0.6f, 0.2f, 1.0f); // Warm Orange
                                        break;
                                    case media::travel::plan::status::booked:
                                        status_color = ImVec4(0.2f, 0.5f, 0.9f, 1.0f); // Bright Blue
                                        break;
                                    case media::travel::plan::status::active:
                                        status_color = ImVec4(0.2f, 0.8f, 0.4f, 1.0f); // Green
                                        break;
                                    case media::travel::plan::status::completed:
                                        status_color = ImVec4(0.6f, 0.4f, 0.8f, 1.0f); // Purple
                                        break;
                                    case media::travel::plan::status::cancelled:
                                        status_color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); // Red
                                        break;
                                }
                                
                                // Draw a nice styled child panel for the plan card
                                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 0.7f));
                                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
                                
                                float card_height = 85.0f;
                                ImGui::BeginChild(std::format("plan_card_{}", plan->id).c_str(), ImVec2(0, card_height), true, ImGuiWindowFlags_NoScrollbar);
                                
                                // Left status accent line using draw list
                                ImVec2 w_pos = ImGui::GetWindowPos();
                                ImVec2 line_start = ImVec2(w_pos.x + 2.0f, w_pos.y + 4.0f);
                                ImVec2 line_end = ImVec2(w_pos.x + 2.0f, w_pos.y + card_height - 4.0f);
                                ImGui::GetWindowDrawList()->AddLine(line_start, line_end, ImGui::GetColorU32(status_color), 4.0f);
                                
                                // Plan Title (bold font/large)
                                ImGui::Indent(4.0f);
                                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                                std::string title = plan->title;
                                if (title.length() > 28) {
                                    title = title.substr(0, 25) + "...";
                                }
                                if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_None, ImVec2(240.0f, 0.0f))) {
                                    std::string plan_uri = std::format("travel-plan:{}", plan->id);
                                    "create_card"_sfn(plan_uri);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Open trip details");
                                }
                                ImGui::PopFont();
                                
                                // Status badge
                                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                                ImGui::TextColored(status_color, "%s %s", ICON_MD_FIBER_MANUAL_RECORD, media::travel::plan::status_to_string(plan->current_status).c_str());
                                
                                // Delete button in top-right
                                ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.1f, 0.1f, 0.3f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 0.5f));
                                if (ImGui::SmallButton(std::format(ICON_MD_DELETE "##del_{}", plan->id).c_str())) {
                                    plans_to_delete.push_back(plan->id);
                                }
                                ImGui::PopStyleColor(3);
                                
                                // Date Range under title
                                ImGui::Spacing();
                                ImGui::TextColored(colors[5], "%s %s - %s", ICON_MD_DATE_RANGE, format_date(plan->start_date).c_str(), format_date(plan->end_date).c_str());
                                
                                // Budget Progress Bar (Allocated budget vs Total Budget)
                                double allocated_budget = 0.0;
                                for (const auto& dest : plan->destinations) {
                                    allocated_budget += dest.budget;
                                }
                                
                                float budget_fraction = 0.0f;
                                if (plan->total_budget > 0.0) {
                                    budget_fraction = static_cast<float>(allocated_budget / plan->total_budget);
                                }
                                
                                ImGui::SameLine(ImGui::GetWindowWidth() - 225.0f);
                                ImGui::TextColored(colors[5], "Allocated: $%.0f/$%.0f", allocated_budget, plan->total_budget);
                                
                                ImGui::SameLine(ImGui::GetWindowWidth() - 245.0f);
                                ImGui::SetNextItemWidth(120.0f);
                                ImVec4 progress_bar_color = (allocated_budget > plan->total_budget) ? colors[4] : colors[3];
                                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, progress_bar_color);
                                ImGui::ProgressBar(budget_fraction, ImVec2(100.0f, 6.0f), "");
                                ImGui::PopStyleColor();
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Budget Allocated: %.1f%%", static_cast<double>(budget_fraction * 100.0f));
                                }
                                
                                ImGui::Unindent(4.0f);
                                ImGui::EndChild();
                                ImGui::PopStyleVar(2);
                                ImGui::PopStyleColor();
                                
                                ImGui::Spacing();
                                ImGui::PopID();
                            }
                        }
                        
                        // Process deletion requests
                        for (const auto& id : plans_to_delete) {
                            DB_INFO_FMT("Travel card: Deleting plan {}", id);
                            travel_host->deletePlan(id);
                        }
                        plans_to_delete.clear();
                    }
                }
                ImGui::EndChild();
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Travel card: Exception during render: {}", e.what());
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                                 "Error during rendering: %s", e.what());
            } catch (...) {
                DB_ERROR("Travel card: Unknown exception during render");
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                                 "Unknown error during rendering");
            }
        });
        
        DB_INFO("Travel card: Render completed");
        return result;
    }

    std::string get_uri() const override {
        return "travel";
    }

    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header Container (emphasis)
        {
            glz::json_t container;
            container["type"] = "Container";
            container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";

            std::vector<glz::json_t> cols;

            // Icon column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t icon;
                icon["type"] = "TextBlock";
                icon["text"] = "\u2708\ufe0f";
                icon["size"] = "Large";
                items.push_back(std::move(icon));
                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            // Title and date column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t title;
                title["type"] = "TextBlock";
                title["text"] = trip_destination;
                title["weight"] = "Bolder";
                title["size"] = "Large";
                items.push_back(std::move(title));

                glz::json_t dates;
                dates["type"] = "TextBlock";
                dates["text"] = departure_date.starts_with("Trip dates:") ? departure_date : ("Trip dates: " + departure_date);
                dates["isSubtle"] = true;
                dates["size"] = "Small";
                items.push_back(std::move(dates));

                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            col_set["columns"] = std::move(cols);
            container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(container));
        }

        // 2. Boarding Pass Container (accent)
        {
            glz::json_t container;
            container["type"] = "Container";
            container["style"] = "accent";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";

            std::vector<glz::json_t> cols;

            // Origin column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t code;
                code["type"] = "TextBlock";
                code["text"] = origin_code;
                code["weight"] = "Bolder";
                code["size"] = "ExtraLarge";
                items.push_back(std::move(code));

                glz::json_t city;
                city["type"] = "TextBlock";
                city["text"] = origin_city;
                city["isSubtle"] = true;
                city["size"] = "Small";
                items.push_back(std::move(city));

                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            // Arrow / Flight column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                glz::json_t arrow;
                arrow["type"] = "TextBlock";
                arrow["text"] = "\u2794";
                arrow["size"] = "Large";
                items.push_back(std::move(arrow));

                glz::json_t flight;
                flight["type"] = "TextBlock";
                flight["text"] = flight_number;
                flight["isSubtle"] = true;
                flight["size"] = "Small";
                items.push_back(std::move(flight));

                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            // Destination column
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                col["horizontalAlignment"] = "Right";
                std::vector<glz::json_t> items;

                glz::json_t code;
                code["type"] = "TextBlock";
                code["text"] = destination_code;
                code["weight"] = "Bolder";
                code["size"] = "ExtraLarge";
                code["horizontalAlignment"] = "Right";
                items.push_back(std::move(code));

                glz::json_t city;
                city["type"] = "TextBlock";
                city["text"] = destination_city;
                city["isSubtle"] = true;
                city["size"] = "Small";
                city["horizontalAlignment"] = "Right";
                items.push_back(std::move(city));

                col["items"] = std::move(items);
                cols.push_back(std::move(col));
            }

            col_set["columns"] = std::move(cols);
            container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(container));
        }

        // 3. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";

            std::vector<glz::json_t> facts;
            facts.push_back(glz::json_t{{"title", "Terminal / Gate:"}, {"value", terminal_gate}});
            facts.push_back(glz::json_t{{"title", "Boarding Time:"}, {"value", boarding_time}});
            facts.push_back(glz::json_t{{"title", "Seat:"}, {"value", seat}});
            facts.push_back(glz::json_t{{"title", "Hotel:"}, {"value", hotel_name}});

            for (const auto& res : reservations) {
                facts.push_back(glz::json_t{{"title", res.type + ":"}, {"value", res.details}});
            }

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 4. Inputs Container
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            input_container["separator"] = true;

            std::vector<glz::json_t> items;

            // Type choice
            {
                glz::json_t choice_set;
                choice_set["type"] = "Input.ChoiceSet";
                choice_set["id"] = "type";
                choice_set["label"] = "Reservation Type";
                choice_set["value"] = "Flight";

                std::vector<glz::json_t> choices;
                choices.push_back(glz::json_t{{"title", "Flight"}, {"value", "Flight"}});
                choices.push_back(glz::json_t{{"title", "Hotel"}, {"value", "Hotel"}});
                choices.push_back(glz::json_t{{"title", "Car Rental"}, {"value", "Car Rental"}});
                choice_set["choices"] = std::move(choices);
                items.push_back(std::move(choice_set));
            }

            // Details input
            {
                glz::json_t text_input;
                text_input["type"] = "Input.Text";
                text_input["id"] = "details";
                text_input["placeholder"] = "Reservation details (e.g. Flight JL 001 / Conf #12345)";
                items.push_back(std::move(text_input));
            }

            // Checklist item ID input
            {
                glz::json_t text_input;
                text_input["type"] = "Input.Text";
                text_input["id"] = "item_id";
                text_input["placeholder"] = "Checklist item ID to toggle (e.g. adapt, esim)";
                items.push_back(std::move(text_input));
            }

            input_container["items"] = std::move(items);
            body.push_back(std::move(input_container));
        }

        card["body"] = std::move(body);

        // 5. Actions
        {
            std::vector<glz::json_t> actions;

            {
                glz::json_t act;
                act["type"] = "Action.Execute";
                act["title"] = "Add Reservation";
                act["verb"] = "add_reservation";
                actions.push_back(std::move(act));
            }
            {
                glz::json_t act;
                act["type"] = "Action.Execute";
                act["title"] = "Toggle Item";
                act["verb"] = "toggle_checklist";
                actions.push_back(std::move(act));
            }
            {
                glz::json_t act;
                act["type"] = "Action.OpenUrl";
                act["title"] = "Flight Status";
                act["url"] = "https://flightaware.com";
                actions.push_back(std::move(act));
            }

            card["actions"] = std::move(actions);
        }

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    void handle_action(std::string_view action_json) override {
        if (action_json.empty()) return;

        try {
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "add_reservation") {
                last_action_ = "add_reservation";
                std::string res_type = "Flight";
                std::string res_details;

                if (data.is_object()) {
                    if (data.contains("type") && data["type"].holds<std::string>()) {
                        res_type = data["type"].get<std::string>();
                    } else if (data.contains("reservation_type") && data["reservation_type"].holds<std::string>()) {
                        res_type = data["reservation_type"].get<std::string>();
                    }

                    if (data.contains("details") && data["details"].holds<std::string>()) {
                        res_details = data["details"].get<std::string>();
                    } else if (data.contains("reservation_details") && data["reservation_details"].holds<std::string>()) {
                        res_details = data["reservation_details"].get<std::string>();
                    }
                }

                if (!res_details.empty()) {
                    reservations.push_back({res_type, res_details});
                    std::string lower_type = res_type;
                    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lower_type.find("hotel") != std::string::npos) {
                        hotel_name = res_details;
                    } else if (lower_type.find("flight") != std::string::npos) {
                        flight_number = res_details;
                    }
                }
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_checklist") {
                last_action_ = "toggle_checklist";
                std::string item_id;

                if (data.is_object()) {
                    if (data.contains("item_id") && data["item_id"].holds<std::string>()) {
                        item_id = data["item_id"].get<std::string>();
                    } else if (data.contains("checklist_item_id") && data["checklist_item_id"].holds<std::string>()) {
                        item_id = data["checklist_item_id"].get<std::string>();
                    } else if (data.contains("id") && data["id"].holds<std::string>()) {
                        item_id = data["id"].get<std::string>();
                    }
                }

                if (!item_id.empty()) {
                    bool found = false;
                    for (auto& item : checklist) {
                        if (item.id == item_id) {
                            item.completed = !item.completed;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        checklist.push_back({item_id, item_id, true});
                    }
                }
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_action_ = "toggle_adaptive_view";
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            DB_ERROR_FMT("Travel card: Action error: {}", e.what());
        }
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }

    [[nodiscard]] const std::string& get_trip_destination() const noexcept { return trip_destination; }
    void set_trip_destination(std::string_view dest) { trip_destination = dest; }

    [[nodiscard]] const std::string& get_departure_date() const noexcept { return departure_date; }
    void set_departure_date(std::string_view date) { departure_date = date; }

    [[nodiscard]] const std::string& get_flight_number() const noexcept { return flight_number; }
    void set_flight_number(std::string_view fn) { flight_number = fn; }

    [[nodiscard]] const std::string& get_hotel_name() const noexcept { return hotel_name; }
    void set_hotel_name(std::string_view hn) { hotel_name = hn; }

    [[nodiscard]] const std::vector<checklist_item>& get_checklist() const noexcept { return checklist; }
    [[nodiscard]] bool is_checklist_item_completed(std::string_view item_id) const {
        for (const auto& item : checklist) {
            if (item.id == item_id) return item.completed;
        }
        return false;
    }

    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::vector<reservation_entry>& get_reservations() const noexcept { return reservations; }

private:
    std::shared_ptr<hosts::TravelHost> travel_host;
    std::vector<long long> plans_to_delete;
    bool is_loading = false;
    std::chrono::steady_clock::time_point last_refresh_time;

    // Date picker state
    int current_month;
    int current_year;

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // Synchronized card state
    std::string trip_destination{"Tokyo C++ Summit 2026"};
    std::string departure_date{"Nov 12 - Nov 20, 2026 (36 days away)"};
    std::string flight_number{"NH 184"};
    std::string hotel_name{"The Capitol Hotel Tokyu (Confirmed #849201)"};
    std::string origin_code{"EZE"};
    std::string origin_city{"Buenos Aires"};
    std::string destination_code{"HND"};
    std::string destination_city{"Tokyo Haneda"};
    std::string terminal_gate{"Terminal 2, Gate B14"};
    std::string boarding_time{"21:45 local time"};
    std::string seat{"12A (Window)"};
    std::vector<checklist_item> checklist{
        {"pass", "Renew/Check Passport", true},
        {"ins", "Travel Insurance Policy", true},
        {"adapt", "Power Plugs & Adapters", false},
        {"esim", "International eSIM Activation", false}
    };
    std::vector<reservation_entry> reservations;
    std::string last_action_{};
};

} // namespace rouen::cards
