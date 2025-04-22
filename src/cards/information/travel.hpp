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
        
        name("Travel Plans");
        requested_fps = 1;  // Update once per second
        width = 400.0f;
        
        // Initialize the Travel host controller with a system runner
        travel_host = hosts::TravelHost::getHost();
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
    
    void render_add_plan() {
        static char title_buffer[128] = "";
        static char description_buffer[512] = "";
        static char start_date_buffer[16] = "";
        static char end_date_buffer[16] = "";
        static char budget_buffer[32] = "0.0";
        static bool show_form = false;
        
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
            ImGui::InputText("##start_date", start_date_buffer, sizeof(start_date_buffer), 0, nullptr, nullptr);
            ImGui::PopItemWidth();
            
            // Show placeholder text for start date format
            if (start_date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "YYYY-MM-DD"
                );
            }
            
            ImGui::Text("End Date:"); 
            ImGui::SameLine(80);
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##end_date", end_date_buffer, sizeof(end_date_buffer), 0, nullptr, nullptr);
            ImGui::PopItemWidth();
            
            // Show placeholder text for end date format
            if (end_date_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "YYYY-MM-DD"
                );
            }
            
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
};

} // namespace rouen::cards