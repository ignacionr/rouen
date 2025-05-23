#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <format>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include "../../helpers/debug.hpp"
#include "jira_ui_components.hpp"

namespace rouen::cards {

class jira_create_issue_handler {
public:
    jira_create_issue_handler(std::shared_ptr<models::jira_model> jira_host)
        : jira_host_(jira_host) {
    }
    
    // Create issue tab rendering
    void render_create_issue_tab(const card::color_array& colors, const std::vector<models::jira_project>& projects) {
        ImGui::TextColored(colors[0], "Create New Issue");
        
        // Project selection
        render_project_selection(projects);
        
        // Issue form
        if (!selected_project_key_.empty()) {
            render_issue_form(colors);
        } else {
            ImGui::TextColored(colors[5], "Please select a project first");
        }
    }
    
    // Load issue types for a project
    void load_issue_types(const std::string& project_key, const std::vector<models::jira_project>& projects) {
        issue_types_.clear();
        
        // Find the project
        auto it = std::find_if(projects.begin(), projects.end(), 
                            [&](const auto& p) { return p.key == project_key; });
        
        if (it != projects.end()) {
            issue_types_ = it->issue_types;
            
            // Set default issue type if available
            if (!issue_types_.empty()) {
                // Prefer "Task" type if available
                auto task_it = std::find_if(issue_types_.begin(), issue_types_.end(),
                                         [](const auto& t) { return t.name == "Task"; });
                                         
                if (task_it != issue_types_.end()) {
                    selected_issue_type_ = task_it->name;
                } else {
                    // Otherwise use first type
                    selected_issue_type_ = issue_types_[0].name;
                }
            }
        }
    }
    
private:
    void render_project_selection(const std::vector<models::jira_project>& projects) {
        ImGui::Text("Project:");
        ImGui::SameLine();
        
        // Project dropdown
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##project_select", selected_project_key_.c_str())) {
            for (const auto& project : projects) {
                if (ImGui::Selectable(project.key.c_str(), selected_project_key_ == project.key)) {
                    selected_project_key_ = project.key;
                    // Load issue types for this project
                    load_issue_types(selected_project_key_, projects);
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh Projects")) {
            // This will be handled by the parent
        }
    }
    
    void render_issue_form(const card::color_array& colors) {
        // Issue type selection
        ImGui::Text("Issue Type:");
        ImGui::SameLine();
        
        // Issue type dropdown
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##issue_type_select", selected_issue_type_.c_str())) {
            for (const auto& type : issue_types_) {
                if (ImGui::Selectable(type.name.c_str(), selected_issue_type_ == type.name)) {
                    selected_issue_type_ = type.name;
                }
            }
            ImGui::EndCombo();
        }
        
        // Summary
        ImGui::Text("Summary:");
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##summary", issue_summary_, sizeof(issue_summary_));
        ImGui::PopItemWidth();
        
        // Description
        ImGui::Text("Description:");
        ImGui::PushItemWidth(-1);
        ImGui::InputTextMultiline("##description", issue_description_, sizeof(issue_description_), 
                                ImVec2(-1, 150));
        ImGui::PopItemWidth();
        
        // Action buttons
        render_form_buttons();
        
        // Messages
        render_form_messages(colors);
    }
    
    void render_form_buttons() {
        if (ImGui::Button("Create Issue", ImVec2(150, 0))) {
            create_issue();
        }
        
        ImGui::SameLine();
        
        // Reset button
        if (ImGui::Button("Reset Form", ImVec2(150, 0))) {
            reset_create_form();
        }
    }
    
    void render_form_messages(const card::color_array& colors) {
        if (!create_issue_error_.empty()) {
            ImGui::TextColored(colors[2], "%s", create_issue_error_.c_str());
        }
        
        if (!create_issue_success_.empty()) {
            ImGui::TextColored(colors[3], "%s", create_issue_success_.c_str());
        }
    }
    
    // Create a new issue
    void create_issue() {
        // Validate form
        if (selected_project_key_.empty()) {
            create_issue_error_ = "Please select a project";
            return;
        }
        
        if (selected_issue_type_.empty()) {
            create_issue_error_ = "Please select an issue type";
            return;
        }
        
        if (std::string(issue_summary_).empty()) {
            create_issue_error_ = "Please enter a summary";
            return;
        }
        
        create_issue_error_.clear();
        create_issue_success_.clear();
        
        // Create issue data structure
        models::jira_issue_create issue_data;
        issue_data.project_key = selected_project_key_;
        issue_data.issue_type = selected_issue_type_;
        issue_data.summary = issue_summary_;
        issue_data.description = issue_description_;
        
        auto future = jira_host_->create_issue(issue_data);
        
        // Handle the future asynchronously
        jira_ui::execute_async<models::jira_issue>(
            std::move(future),
            [this](const models::jira_issue& result) {
                if (!result.key.empty()) {
                    create_issue_success_ = std::format("Issue created: {}", result.key);
                    JIRA_INFO_FMT("Issue created successfully: {}", result.key);
                    
                    // Reset form
                    reset_create_form();
                } else {
                    create_issue_error_ = "Failed to create issue";
                }
            },
            [this](const std::string& error) {
                create_issue_error_ = std::format("Error creating issue: {}", error);
                JIRA_ERROR_FMT("Error creating issue: {}", error);
            }
        );
    }
    
    // Reset create issue form
    void reset_create_form() {
        issue_summary_[0] = '\0';
        issue_description_[0] = '\0';
        create_issue_error_.clear();
        create_issue_success_.clear();
    }

    // JIRA model reference
    std::shared_ptr<models::jira_model> jira_host_;
    
    // Create issue
    std::string selected_project_key_;
    std::vector<models::jira_issue_type> issue_types_;
    std::string selected_issue_type_;
    char issue_summary_[256] = "";
    char issue_description_[4096] = "";
    std::string create_issue_error_;
    std::string create_issue_success_;
};

} // namespace rouen::cards
