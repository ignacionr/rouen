#pragma once

#include <string>
#include "../glaze_include.hpp"
#include "../imgui_include.hpp"

namespace rouen::helpers::views {
    class json_view {
    public:
        void render(const glz::json_t& json, const std::string& label = "") {
            if (!label.empty()) {
                ImGui::Text("%s:", label.c_str());
                ImGui::Indent();
            }
            
            render_json_node(json);
            
            if (!label.empty()) {
                ImGui::Unindent();
            }
        }
        
    private:
        void render_json_node(const glz::json_t& node) {
            // For the new Glaze API, we need to use different methods
            try {
                std::string json_str;
                glz::write_json(node, json_str);
                
                // Simple JSON string display for now
                ImGui::Text("JSON: %s", json_str.c_str());
            } catch (const std::exception& e) {
                ImGui::Text("Error displaying JSON: %s", e.what());
            }
        }
        
        void render_json_value(const glz::json_t& value) {
            // For the new Glaze API, we'll just display the JSON as a string for now
            try {
                std::string json_str;
                glz::write_json(value, json_str);
                ImGui::Text("%s", json_str.c_str());
            } catch (const std::exception& e) {
                ImGui::Text("Error: %s", e.what());
            }
        }
    };
}