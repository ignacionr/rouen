#pragma once

#include <string>
#include <glaze/json.hpp>
#include <imgui.h>

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
            if (node.is_object()) {
                if (ImGui::TreeNode("Object")) {
                    for (const auto& [key, value] : node.get_object()) {
                        ImGui::Text("%s:", key.c_str());
                        ImGui::SameLine();
                        render_json_value(value);
                    }
                    ImGui::TreePop();
                }
            } else if (node.is_array()) {
                if (ImGui::TreeNode("Array")) {
                    int index = 0;
                    for (const auto& item : node.get_array()) {
                        ImGui::PushID(index++);
                        ImGui::Text("[%d]", index-1);
                        ImGui::SameLine();
                        render_json_value(item);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            } else {
                render_json_value(node);
            }
        }
        
        void render_json_value(const glz::json_t& value) {
            if (value.is_null()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "null");
            } else if (value.is_boolean()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), value.get_boolean() ? "true" : "false");
            } else if (value.is_number()) {
                double num = value.get_number();
                // Check if the number is an integer (no fractional part)
                if (std::abs(num - static_cast<double>(static_cast<long>(num))) < 1e-10) {
                    ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "%ld", static_cast<long>(num));
                } else {
                    // Convert to string using a type-safe approach
                    std::string str = std::to_string(num);
                    // Limit to max 6 characters plus decimal point to avoid excessive precision
                    if (str.size() > 7 && str.find('.') != std::string::npos) {
                        size_t decimal_pos = str.find('.');
                        str = str.substr(0, decimal_pos + 4); // keep 3 digits after decimal point
                    }
                    ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.8f, 1.0f), "%s", str.c_str());
                }
            } else if (value.is_string()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "\"%s\"", value.get_string().c_str());
            } else if (value.is_object()) {
                render_json_node(value);
            } else if (value.is_array()) {
                render_json_node(value);
            }
        }
    };
}