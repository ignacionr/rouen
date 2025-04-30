#pragma once

#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include <string>
#include <format>

namespace rouen::cards {

// Temporary dummy card implementation for JIRA-related cards
// This will be used as a fallback until the actual implementation is fixed
class dummy_jira_card : public card {
public:
    dummy_jira_card() {
        // Set brand colors
        colors[0] = ImVec4{0.0f, 0.48f, 0.8f, 1.0f};  // JIRA blue primary
        colors[1] = ImVec4{0.1f, 0.58f, 0.9f, 0.7f};  // JIRA blue secondary
        
        // Add additional colors
        get_color(2, ImVec4(0.0f, 0.67f, 1.0f, 1.0f));  // Highlighted item color
        get_color(3, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));   // Gray text
        get_color(4, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));   // Success green
        get_color(5, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));   // Error red
        
        // Default message
        set_message();
    }
    
    bool render() override {
        return render_window([this]() {
            ImGui::TextColored(colors[0], "JIRA Integration");
            ImGui::Separator();
            
            ImGui::TextWrapped("%s", message_.c_str());
            
            if (ImGui::Button("Open Main JIRA Card", ImVec2(150, 0))) {
                open_main_jira_card();
            }
        });
    }
    
    virtual void set_message() {
        message_ = "This JIRA feature is currently under development.";
    }
    
    std::string get_uri() const override {
        return "jira-dummy";
    }
    
protected:
    std::string message_;
    
    void open_main_jira_card() {
        "create_card"_sfn("jira");
        grab_focus = false;  // Give focus to the new card
    }
};

} // namespace rouen::cards