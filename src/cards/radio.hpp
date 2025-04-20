#pragma once

#include <memory>
#include <string>
#include <vector>
#include <imgui/imgui.h>

#include "card.hpp"
#include "../models/radio.hpp"

namespace rouen::cards {
    
    class radio : public card {
    public:
        radio() {
            // Set custom colors for the radio card
            colors[0] = {0.5f, 0.3f, 0.7f, 1.0f}; // Purple primary color
            colors[1] = {0.4f, 0.2f, 0.6f, 0.7f}; // Darker purple secondary color
            
            // Additional colors for status indicators
            get_color(2, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Green for playing
            get_color(3, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red for errors
            
            name("Radio");
            requested_fps = 5;  // Update 5 times per second
            
            // Create radio model
            radio_model = std::make_unique<rouen::models::radio>();
        }
        
        ~radio() override {
            // Ensure the radio is stopped when the card is destroyed
            if (radio_model) {
                radio_model->stopCurrentStation();
            }
        }
        
        bool render() override {
            return render_window([this]() {
                if (!radio_model) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Radio model not initialized");
                    return;
                }
                
                // Get current station and all station names
                const std::string& current_station = radio_model->getCurrentStation();
                const std::vector<std::string>& stations = radio_model->getStationNames();
                
                // Display currently playing station
                if (!current_station.empty()) {
                    ImGui::TextColored(colors[2], "Now Playing: %s", current_station.c_str());
                    
                    // Stop button
                    if (ImGui::Button("Stop")) {
                        radio_model->stopCurrentStation();
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No station playing");
                }
                
                ImGui::Separator();
                
                // Search filter
                static char search_buffer[256] = "";
                ImGui::PushItemWidth(-1);
                ImGui::InputText("##search", search_buffer, IM_ARRAYSIZE(search_buffer));
                
                // Show placeholder text when search is empty
                if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                    auto pos = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(pos.x + 5, pos.y + 2),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        "Search stations..."
                    );
                }
                ImGui::PopItemWidth();
                
                ImGui::Separator();
                
                // List of stations with scroll
                if (ImGui::BeginChild("StationsList", ImVec2(0, 0), true)) {
                    std::string search_text = search_buffer;
                    std::transform(search_text.begin(), search_text.end(), search_text.begin(),
                                  [](unsigned char c) { return std::tolower(c); });
                    
                    for (const auto& station_name : stations) {
                        // Apply search filter
                        std::string lower_name = station_name;
                        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                                      [](unsigned char c) { return std::tolower(c); });
                        
                        if (!search_text.empty() && lower_name.find(search_text) == std::string::npos) {
                            continue;  // Skip stations that don't match search
                        }
                        
                        // Highlight currently playing station
                        bool is_current = (station_name == current_station);
                        if (is_current) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[2]));
                        }
                        
                        // Station entry with play button
                        if (ImGui::Selectable(station_name.c_str(), is_current)) {
                            radio_model->playStation(station_name);
                        }
                        
                        if (is_current) {
                            ImGui::PopStyleColor();
                        }
                    }
                    
                    // If no stations match the search, show a message
                    if (ImGui::GetScrollMaxY() == 0 && search_text.length() > 0) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No stations match your search");
                    }
                }
                ImGui::EndChild();
            });
        }
        
    private:
        std::unique_ptr<rouen::models::radio> radio_model;
    };
    
}