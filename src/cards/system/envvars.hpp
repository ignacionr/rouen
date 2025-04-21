#pragma once

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <imgui/imgui.h>
#include "../interface/card.hpp"

// The environ variable is a C global, defined outside any namespace
extern char **environ;

namespace rouen::cards {

struct envvars_card : public card {
    envvars_card() {
        // Set custom colors for the card
        colors[0] = {0.2f, 0.4f, 0.6f, 1.0f};   // Blue primary color (first_color)
        colors[1] = {0.3f, 0.5f, 0.7f, 0.7f};   // Light blue secondary color (second_color)
        
        // Additional colors for specific elements with higher contrast
        get_color(2, {0.0f, 0.8f, 1.0f, 1.0f}); // Bright cyan for variable name
        get_color(3, {0.2f, 0.2f, 0.3f, 0.7f}); // Darker table header background for contrast
        get_color(4, {0.2f, 0.3f, 0.4f, 0.5f}); // Darker alternating row color
        get_color(5, {1.0f, 1.0f, 1.0f, 0.95f}); // Nearly white for value text
        
        name("Environment Variables");
        size.x = 600.0f; // Set initial width
        
        // Request refresh rate for updating variables
        requested_fps = 2;  // Update twice per second
        
        // Load environment variables
        refresh_env_vars();
    }
    
    // Explicit destructor
    ~envvars_card() override {
        env_vars.clear();
    }
    
    // Get all environment variables
    void refresh_env_vars() {
        env_vars.clear();
        
        // Get environment variables
        for (char **env = environ; *env != nullptr; ++env) {
            std::string entry = *env;
            auto pos = entry.find('=');
            if (pos != std::string::npos) {
                std::string key = entry.substr(0, pos);
                std::string value = entry.substr(pos + 1);
                env_vars[key] = value;
            }
        }
        
        // Update the sorted keys list for display
        sorted_keys.clear();
        for (const auto& [key, _] : env_vars) {
            sorted_keys.push_back(key);
        }
        
        // Sort keys alphabetically
        std::sort(sorted_keys.begin(), sorted_keys.end());
    }
    
    bool render() override {
        return render_window([this]() {
            ImGui::Text("Environment Variables (%zu):", env_vars.size());
            
            // Filter input
            static char filter_buffer[256] = "";
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##filter", filter_buffer, IM_ARRAYSIZE(filter_buffer))) {
                // Filter is updated
            }
            
            // Show placeholder text when filter is empty
            if (filter_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                auto pos = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(pos.x + 5, pos.y + 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Filter variables..."
                );
            }
            ImGui::PopItemWidth();
            
            ImGui::Separator();
            
            // Display variables in a scrollable area with a table
            if (ImGui::BeginChild("EnvVarsScroll", ImVec2(0, 0), true)) {
                // Filter text conversion for case insensitivity
                std::string filter_text = filter_buffer;
                std::transform(filter_text.begin(), filter_text.end(), filter_text.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                
                // Setup for table
                static ImGuiTableFlags table_flags = 
                    ImGuiTableFlags_Borders | 
                    ImGuiTableFlags_RowBg | 
                    ImGuiTableFlags_ScrollY | 
                    ImGuiTableFlags_SizingFixedFit;

                if (ImGui::BeginTable("EnvVarsTable", 2, table_flags)) {
                    // Set up table headers
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    
                    // Table headers with background color
                    ImGui::TableHeadersRow();
                    
                    // Filter and display variables in table rows
                    int row_idx = 0;
                    for (const auto& key : sorted_keys) {
                        // Apply filter if set
                        if (!filter_text.empty()) {
                            std::string lower_key = key;
                            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                                          [](unsigned char c) { return std::tolower(c); });
                            
                            std::string lower_value = env_vars[key];
                            std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(),
                                          [](unsigned char c) { return std::tolower(c); });
                            
                            // Skip if neither key nor value contains filter text
                            if (lower_key.find(filter_text) == std::string::npos && 
                                lower_value.find(filter_text) == std::string::npos) {
                                continue;
                            }
                        }
                        
                        // Add a row to the table
                        ImGui::TableNextRow();
                        
                        // Variable name (first column)
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(
                            ImVec4(colors[2].x, colors[2].y, colors[2].z, colors[2].w),
                            "%s", key.c_str()
                        );
                        
                        // Variable value (second column)
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(
                            ImVec4(colors[5].x, colors[5].y, colors[5].z, colors[5].w),
                            "%s", env_vars[key].c_str()
                        );
                        
                        row_idx++;
                    }
                    
                    ImGui::EndTable();
                    
                    // If no variables match the filter, show a message
                    if (row_idx == 0 && filter_text.length() > 0) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No variables match your filter");
                    }
                }
            }
            ImGui::EndChild();
        });
    }
    
private:
    std::map<std::string, std::string> env_vars;
    std::vector<std::string> sorted_keys;
};

} // namespace rouen::cards