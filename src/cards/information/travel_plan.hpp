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
#include "../../models/travel/plan.hpp"
#include "../../helpers/date_picker.hpp"
#include "../../registrar.hpp"
#include <mutex>
#include <atomic>
#include <thread>
#include "../../hosts/weather_host.hpp"

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
        
        // Initialize DatePicker and set its colors to match the card theme
        date_picker.set_colors(
            colors[0],                            // Primary button color
            ImVec4(0.3f, 0.6f, 0.9f, 0.7f),      // Hover button color
            ImVec4(0.2f, 0.3f, 0.7f, 1.0f),      // Date highlight color
            ImVec4(0.1f, 0.1f, 0.15f, 0.9f),     // Background color
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f),      // Text color
            ImVec4(0.3f, 0.4f, 0.8f, 1.0f)       // Current day color
        );
        
        width = 600.0f;
        requested_fps = 1;  // Update once per second
        
        // Initialize the Travel host controller
        travel_host = hosts::TravelHost::getHost();
        
        // Parse the plan ID and load the plan
        try {
            plan_id = std::stoll(std::string(plan_id_str));
            plan_ptr = travel_host->getPlan(plan_id);
            
            if (plan_ptr) {
                name(std::format("{} - Trip", plan_ptr->title));
            } else {
                name("Travel Plan (Not Found)");
            }
        } catch (const std::exception& /* e */) {
            name("Travel Plan (Invalid ID)");
        }
    }
    
    ~travel_plan() override = default;

    bool render() override {
        if (travel_host) {
            auto latest = travel_host->getPlan(plan_id);
            if (latest) {
                plan_ptr = latest;
                name(std::format("{} - Trip", plan_ptr->title));
            }
        }

        return render_window([this]() {            
            // Plan details section
            render_plan_details();
            
            // Destinations section
            render_destinations();
            
            // Add destination form
            ImGui::Separator();
            render_add_destination();
        });
    }

    std::string get_uri() const override
    {
        return std::format("travel-plan:{}", plan_id);
    }
    
private:
    void render_plan_details() {
        // Plan not found
        if (!plan_ptr) {
            ImGui::TextColored(colors[4], "Travel plan not found");
            return;
        }
        
        // Status color mapping
        ImVec4 status_color;
        switch (plan_ptr->current_status) {
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
            default:
                status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        }

        // Header Panel: Child frame for trip name and description
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 0.8f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        
        float header_height = 100.0f;
        ImGui::BeginChild("TripHeaderPanel", ImVec2(0, header_height), true, ImGuiWindowFlags_NoScrollbar);
        
        // Left status accent line
        ImVec2 w_pos = ImGui::GetWindowPos();
        ImVec2 line_start = ImVec2(w_pos.x + 2.0f, w_pos.y + 4.0f);
        ImVec2 line_end = ImVec2(w_pos.x + 2.0f, w_pos.y + header_height - 4.0f);
        ImGui::GetWindowDrawList()->AddLine(line_start, line_end, ImGui::GetColorU32(status_color), 4.0f);
        
        ImGui::Indent(4.0f);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Large/bold
        ImGui::TextColored(colors[2], "%s", plan_ptr->title.c_str());
        ImGui::PopFont();
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
        ImGui::TextColored(status_color, "%s %s", ICON_MD_FIBER_MANUAL_RECORD, media::travel::plan::status_to_string(plan_ptr->current_status).c_str());
        
        ImGui::Spacing();
        ImGui::TextWrapped("%s", plan_ptr->description.c_str());
        
        ImGui::Unindent(4.0f);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Stats Box: Side-by-side child columns
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.15f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::BeginChild("TripStatsBox", ImVec2(0, 115.0f), true, ImGuiWindowFlags_NoScrollbar);
        
        ImGui::Columns(2, "StatsColumns", false);
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f);
        ImGui::SetColumnWidth(1, ImGui::GetWindowWidth() * 0.5f);
        
        // Column 0: Dates & Countdown & Completion
        auto now = std::chrono::system_clock::now();
        std::string countdown_text = "";
        ImVec4 countdown_color = colors[5];
        if (now < plan_ptr->start_date) {
            auto diff = plan_ptr->start_date - now;
            long long days = std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24;
            if (days == 0) {
                countdown_text = std::format("{} Starts TOMORROW!", ICON_MD_FLIGHT_TAKEOFF);
            } else {
                countdown_text = std::format("{} Starts in {} days", ICON_MD_FLIGHT_TAKEOFF, days + 1);
            }
            countdown_color = ImVec4(0.9f, 0.6f, 0.2f, 1.0f);
        } else if (now > plan_ptr->end_date) {
            countdown_text = std::format("{} Completed", ICON_MD_DONE_ALL);
            countdown_color = ImVec4(0.6f, 0.4f, 0.8f, 1.0f);
        } else {
            auto elapsed = now - plan_ptr->start_date;
            long long day_num = std::chrono::duration_cast<std::chrono::hours>(elapsed).count() / 24 + 1;
            countdown_text = std::format("{} Day {} of {}", ICON_MD_FLIGHT, day_num, plan_ptr->total_days());
            countdown_color = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
        }
        
        ImGui::TextColored(colors[2], "TRIP INFORMATION");
        ImGui::TextColored(colors[5], "%s %s - %s", ICON_MD_DATE_RANGE, 
                           helpers::DatePicker::format_date(plan_ptr->start_date).c_str(),
                           helpers::DatePicker::format_date(plan_ptr->end_date).c_str());
        ImGui::TextColored(colors[5], "%s Duration: %ld days", ICON_MD_HOURGLASS_EMPTY, plan_ptr->total_days());
        ImGui::TextColored(countdown_color, "%s", countdown_text.c_str());
        
        // Column 1: Financials & Completion Progress
        ImGui::NextColumn();
        
        double allocated_budget = 0.0;
        size_t completed_destinations = 0;
        for (const auto& dest : plan_ptr->destinations) {
            allocated_budget += dest.budget;
            if (dest.completed) {
                completed_destinations++;
            }
        }
        
        double remaining_budget = plan_ptr->total_budget - allocated_budget;
        float budget_fraction = 0.0f;
        if (plan_ptr->total_budget > 0.0) {
            budget_fraction = static_cast<float>(allocated_budget / plan_ptr->total_budget);
        }
        
        float completion_fraction = 0.0f;
        if (!plan_ptr->destinations.empty()) {
            completion_fraction = static_cast<float>(completed_destinations) / static_cast<float>(plan_ptr->destinations.size());
        }
        
        ImGui::TextColored(colors[2], "BUDGET & PROGRESS");
        
        // Budget stats
        ImGui::TextColored(colors[5], "Allocated: $%.0f / $%.0f (Rem: $%.0f)", allocated_budget, plan_ptr->total_budget, remaining_budget);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 20.0f);
        ImVec4 budget_bar_color = (allocated_budget > plan_ptr->total_budget) ? colors[4] : colors[3];
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, budget_bar_color);
        ImGui::ProgressBar(budget_fraction, ImVec2(0, 5.0f), "");
        ImGui::PopStyleColor();
        
        // Completion stats
        ImGui::TextColored(colors[5], "Destinations: %zu / %zu completed", completed_destinations, plan_ptr->destinations.size());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
        ImGui::ProgressBar(completion_fraction, ImVec2(0, 5.0f), "");
        ImGui::PopStyleColor();
        
        ImGui::Columns(1);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Status change buttons
        ImGui::Spacing();
        ImGui::TextColored(colors[2], "Update Status:");
        
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("Planning", ImVec2(100, 0))) {
            update_status(media::travel::plan::status::planning);
        }
        ImGui::SameLine();
        if (ImGui::Button("Booked", ImVec2(100, 0))) {
            update_status(media::travel::plan::status::booked);
        }
        ImGui::SameLine();
        if (ImGui::Button("Active", ImVec2(100, 0))) {
            update_status(media::travel::plan::status::active);
        }
        ImGui::SameLine();
        if (ImGui::Button("Completed", ImVec2(100, 0))) {
            update_status(media::travel::plan::status::completed);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelled", ImVec2(100, 0))) {
            update_status(media::travel::plan::status::cancelled);
        }
        ImGui::PopStyleVar();
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
            if (ImGui::BeginTable("destinations", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Name / Location", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Dates", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Accommodation", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("Budget & Status", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();
                
                for (size_t i = 0; i < plan_ptr->destinations.size(); i++) {
                    const auto& dest = plan_ptr->destinations[i];
                    
                    ImGui::TableNextRow();
                    
                    // Name and location
                    ImGui::TableNextColumn();
                    ImGui::TextColored(dest.completed ? colors[3] : colors[8], 
                                     "%s", dest.name.c_str());
                    
                    ImGui::TextColored(colors[5], "%s %s", ICON_MD_PLACE, dest.location.c_str());
                    
                    if (!dest.notes.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(colors[2], "%s", ICON_MD_INFO);
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(dest.notes.c_str());
                            ImGui::EndTooltip();
                        }
                    }
                    
                    // Dates
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", helpers::DatePicker::format_date(dest.arrival).c_str());
                    ImGui::TextColored(colors[5], "to");
                    ImGui::Text("%s", helpers::DatePicker::format_date(dest.departure).c_str());
                    
                    // Accommodation
                    ImGui::TableNextColumn();
                    if (dest.accommodation.empty()) {
                        ImGui::TextDisabled("-");
                    } else {
                        ImGui::Text("%s %s", ICON_MD_HOTEL, dest.accommodation.c_str());
                    }
                    
                    // Budget & completed status
                    ImGui::TableNextColumn();
                    ImGui::Text("%s $%.2f", ICON_MD_ATTACH_MONEY, dest.budget);
                    
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 45.0f);
                    bool completed = dest.completed;
                    
                    // Beautiful custom checkbox look by drawing check icons next to it
                    ImGui::PushStyleColor(ImGuiCol_Text, completed ? colors[3] : colors[5]);
                    std::string label = std::format("{}##completed_{}", completed ? ICON_MD_CHECK_CIRCLE : ICON_MD_CHECK_CIRCLE_OUTLINE, i);
                    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(24.0f, 0.0f))) {
                        toggle_destination_completed(i, !completed);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(completed ? "Mark as Incomplete" : "Mark as Completed");
                    }
                }
                
                ImGui::EndTable();
            }
        }
    }
    
    void render_add_destination() {
        if (show_dest_form_) {
            ImGui::TextColored(colors[2], "Add New Destination");
            
            ImGui::Text("Name:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##name", dest_name_, sizeof(dest_name_));
            ImGui::PopItemWidth();
            
            ImGui::Text("Location:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            if (ImGui::InputText("##location", dest_location_, sizeof(dest_location_))) {
                std::lock_guard<std::mutex> lock(validation_state_.mutex);
                if (validation_state_.input_location != dest_location_) {
                    validation_state_.is_valid = false;
                    validation_state_.resolved_location = "";
                    validation_state_.error_message = "";
                }
            }
            ImGui::PopItemWidth();
            
            ImGui::SameLine();
            if (validation_state_.is_checking.load()) {
                ImGui::TextDisabled("%s Verifying...", ICON_MD_SYNC);
            } else {
                bool is_valid = false;
                std::string resolved;
                std::string err;
                {
                    std::lock_guard<std::mutex> lock(validation_state_.mutex);
                    is_valid = validation_state_.is_valid;
                    resolved = validation_state_.resolved_location;
                    err = validation_state_.error_message;
                }
                
                if (is_valid) {
                    ImGui::TextColored(colors[3], "%s", ICON_MD_CHECK_CIRCLE);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Resolved: %s", resolved.c_str());
                    }
                    if (resolved != dest_location_) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(std::format("Use '{}'", resolved).c_str())) {
                            std::strncpy(dest_location_, resolved.c_str(), sizeof(dest_location_) - 1);
                            dest_location_[sizeof(dest_location_) - 1] = '\0';
                        }
                    }
                } else {
                    if (ImGui::Button(std::format("{} Verify", ICON_MD_SEARCH).c_str())) {
                        start_location_validation(dest_location_);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Verify location with Weather database");
                    }
                    if (!err.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(colors[4], "%s", ICON_MD_WARNING);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Verification failed: %s", err.c_str());
                        }
                    }
                }
            }
            
            ImGui::Text("Notes:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputTextMultiline("##notes", dest_notes_, sizeof(dest_notes_), 
                                    ImVec2(300, 60));
            ImGui::PopItemWidth();
            
            // Use DatePicker's render_date_field for arrival date
            date_picker.render_date_field("Arrival:", dest_arrival_, 
                                        sizeof(dest_arrival_), 
                                        "arrival_date_picker_popup", 
                                        &open_arrival_picker_);
            
            // Use DatePicker's render_date_field for departure date
            date_picker.render_date_field("Departure:", dest_departure_, 
                                        sizeof(dest_departure_), 
                                        "departure_date_picker_popup", 
                                        &open_departure_picker_);
            
            ImGui::Text("Accommodation:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##accommodation", dest_accommodation_, sizeof(dest_accommodation_));
            ImGui::PopItemWidth();
            
            ImGui::Text("Budget:"); 
            ImGui::SameLine(100);
            ImGui::PushItemWidth(300);
            ImGui::InputText("##budget", dest_budget_, sizeof(dest_budget_));
            ImGui::PopItemWidth();
            
            ImGui::Spacing();
            
            // Submit and cancel buttons
            if (ImGui::Button("Add Destination", ImVec2(150, 0))) {
                if (strlen(dest_name_) > 0 && strlen(dest_location_) > 0 && 
                    strlen(dest_arrival_) > 0 && strlen(dest_departure_) > 0) {
                    
                    // Parse dates
                    auto arrival = helpers::DatePicker::parse_date(dest_arrival_);
                    auto departure = helpers::DatePicker::parse_date(dest_departure_);
                    
                    // Parse budget
                    double budget = 0.0;
                    try {
                        budget = std::stod(dest_budget_);
                    } catch (...) {
                        budget = 0.0;
                    }
                    
                    // Add destination
                    if (travel_host->addDestination(
                        plan_id, 
                        dest_name_, 
                        dest_location_, 
                        arrival, 
                        departure, 
                        dest_accommodation_,
                        dest_notes_,
                        budget)) {
                        
                        // Clear form
                        dest_name_[0] = '\0';
                        dest_location_[0] = '\0';
                        dest_notes_[0] = '\0';
                        dest_arrival_[0] = '\0';
                        dest_departure_[0] = '\0';
                        dest_accommodation_[0] = '\0';
                        std::snprintf(dest_budget_, sizeof(dest_budget_), "0.0");
                        show_dest_form_ = false;
                        
                        // Refresh plan data
                        plan_ptr = travel_host->getPlan(plan_id);
                    }
                }
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                // Clear form and hide
                dest_name_[0] = '\0';
                dest_location_[0] = '\0';
                dest_notes_[0] = '\0';
                dest_arrival_[0] = '\0';
                dest_departure_[0] = '\0';
                dest_accommodation_[0] = '\0';
                std::snprintf(dest_budget_, sizeof(dest_budget_), "0.0");
                show_dest_form_ = false;
            }
        } else {
            if (ImGui::Button("+ Add Destination", ImVec2(150, 0))) {
                show_dest_form_ = true;
            }
        }
    }
    
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
    helpers::DatePicker date_picker;

    // Destination form fields
    char dest_name_[128] = "";
    char dest_location_[128] = "";
    char dest_notes_[256] = "";
    char dest_arrival_[16] = "";
    char dest_departure_[16] = "";
    char dest_accommodation_[128] = "";
    char dest_budget_[32] = "0.0";
    bool show_dest_form_{false};
    bool open_arrival_picker_{false};
    bool open_departure_picker_{false};

    // Location validation state
    struct LocationValidationState {
        std::string input_location;
        std::string resolved_location;
        bool is_valid{false};
        std::atomic<bool> is_checking{false};
        std::string error_message;
        std::mutex mutex;
    } validation_state_;

    void start_location_validation(const std::string& query) {
        if (query.empty() || validation_state_.is_checking.load()) {
            return;
        }

        validation_state_.is_checking.store(true);
        {
            std::lock_guard<std::mutex> lock(validation_state_.mutex);
            validation_state_.error_message = "";
            validation_state_.is_valid = false;
            validation_state_.input_location = query;
            validation_state_.resolved_location = "";
        }

        std::thread([this, query]() {
            bool valid = false;
            std::string resolved;
            std::string err;
            try {
                auto temp_host = std::make_shared<hosts::WeatherHost>();
                temp_host->setLocation(query);
                temp_host->refreshWeather();
                
                auto cur_weather = temp_host->getCurrentWeather();
                if (cur_weather) {
                    resolved = cur_weather->name;
                    if (!cur_weather->sys.country.empty()) {
                        resolved += ", " + cur_weather->sys.country;
                    }
                    valid = true;
                } else {
                    err = "Location not found in weather database.";
                }
            } catch (const std::exception& e) {
                err = e.what();
            }

            {
                std::lock_guard<std::mutex> lock(validation_state_.mutex);
                validation_state_.resolved_location = resolved;
                validation_state_.is_valid = valid;
                validation_state_.error_message = err;
            }
            validation_state_.is_checking.store(false);
        }).detach();
    }
};

} // namespace rouen::cards
