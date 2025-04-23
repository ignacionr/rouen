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
#include "../../helpers/date_picker.hpp"
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
        } catch (const std::exception& e) {
            name("Travel Plan (Invalid ID)");
        }
    }
    
    ~travel_plan() override = default;

    bool render() override {
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
            helpers::DatePicker::format_date(plan_ptr->start_date).c_str(),
            helpers::DatePicker::format_date(plan_ptr->end_date).c_str(),
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
                    ImGui::Text("%s", helpers::DatePicker::format_date(dest.arrival).c_str());
                    ImGui::TextColored(colors[5], "to");
                    ImGui::Text("%s", helpers::DatePicker::format_date(dest.departure).c_str());
                    
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
            
            // Use DatePicker's render_date_field for arrival date
            date_picker.render_date_field("Arrival:", arrival_date_buffer, 
                                        sizeof(arrival_date_buffer), 
                                        "arrival_date_picker_popup", 
                                        &open_arrival_date_picker);
            
            // Use DatePicker's render_date_field for departure date
            date_picker.render_date_field("Departure:", departure_date_buffer, 
                                        sizeof(departure_date_buffer), 
                                        "departure_date_picker_popup", 
                                        &open_departure_date_picker);
            
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
                    auto arrival = helpers::DatePicker::parse_date(arrival_date_buffer);
                    auto departure = helpers::DatePicker::parse_date(departure_date_buffer);
                    
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
};

} // namespace rouen::cards