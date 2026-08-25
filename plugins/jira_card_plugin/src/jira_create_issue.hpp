#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <format>
#include <array>
#include <algorithm>

#include "helpers/imgui_include.hpp"
#include "jira_model.hpp"
#include "helpers/debug.hpp"
#include "jira_ui_components.hpp"

namespace rouen::cards {

class jira_create_issue_handler {
public:
    using color_array = std::array<ImVec4, 16>;

    jira_create_issue_handler(std::shared_ptr<models::jira_model> jira_host)
        : jira_host_(jira_host) {
    }
    
    void render_create_issue_tab(const color_array& colors, const std::vector<models::jira_project>& projects) {
        ImGui::TextColored(colors[0], "Create New Issue");
        
        render_project_selection(projects);
        
        if (!selected_project_key_.empty()) {
            render_issue_form(colors);
        } else {
            ImGui::TextColored(colors[5], "Please select a project first");
        }
    }
    
    void load_issue_types(const std::string& project_key, const std::vector<models::jira_project>& projects) {
        issue_types_.clear();
        
        auto it = std::find_if(projects.begin(), projects.end(), 
                            [&](const auto& p) { return p.key == project_key; });
        
        if (it != projects.end()) {
            issue_types_ = it->issue_types;
            
            if (!issue_types_.empty()) {
                auto task_it = std::find_if(issue_types_.begin(), issue_types_.end(),
                                         [](const auto& t) { return t.name == "Task"; });
                                         
                if (task_it != issue_types_.end()) {
                    selected_issue_type_ = task_it->name;
                } else {
                    selected_issue_type_ = issue_types_[0].name;
                }
            }
        }
    }
    
private:
    void render_project_selection(const std::vector<models::jira_project>& projects) {
        ImGui::Text("Project:");
        ImGui::SameLine();
        
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##project_select", selected_project_key_.c_str())) {
            for (const auto& project : projects) {
                if (ImGui::Selectable(project.key.c_str(), selected_project_key_ == project.key)) {
                    selected_project_key_ = project.key;
                    load_issue_types(selected_project_key_, projects);
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh Projects")) {
        }
    }
    
    void render_issue_form(const color_array& colors) {
        ImGui::Text("Issue Type:");
        ImGui::SameLine();
        
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##issue_type_select", selected_issue_type_.c_str())) {
            for (const auto& type : issue_types_) {
                if (ImGui::Selectable(type.name.c_str(), selected_issue_type_ == type.name)) {
                    selected_issue_type_ = type.name;
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Text("Summary:");
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##summary", issue_summary_, sizeof(issue_summary_));
        ImGui::PopItemWidth();
        
        ImGui::Text("Description:");
        ImGui::PushItemWidth(-1);
        ImGui::InputTextMultiline("##description", issue_description_, sizeof(issue_description_), 
                                ImVec2(-1, 150));
        ImGui::PopItemWidth();
        
        render_form_buttons();
        render_form_messages(colors);
    }
    
    void render_form_buttons() {
        if (ImGui::Button("Create Issue", ImVec2(150, 0))) {
            create_issue();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Reset Form", ImVec2(150, 0))) {
            reset_create_form();
        }
    }
    
    void render_form_messages(const color_array& colors) {
        if (!create_issue_error_.empty()) {
            ImGui::TextColored(colors[2], "%s", create_issue_error_.c_str());
        }
        
        if (!create_issue_success_.empty()) {
            ImGui::TextColored(colors[3], "%s", create_issue_success_.c_str());
        }
    }
    
    void create_issue() {
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
        
        models::jira_issue_create issue_data;
        issue_data.project_key = selected_project_key_;
        issue_data.issue_type = selected_issue_type_;
        issue_data.summary = issue_summary_;
        issue_data.description = issue_description_;
        
        auto future = jira_host_->create_issue(issue_data);
        
        jira_ui::execute_async<models::jira_issue>(
            std::move(future),
            [this](const models::jira_issue& result) {
                if (!result.key.empty()) {
                    create_issue_success_ = std::format("Issue created: {}", result.key);
                    JIRA_INFO_FMT("Issue created successfully: {}", result.key);
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
    
    void reset_create_form() {
        issue_summary_[0] = '\0';
        issue_description_[0] = '\0';
        create_issue_error_.clear();
        create_issue_success_.clear();
    }

    std::shared_ptr<models::jira_model> jira_host_;
    
    std::string selected_project_key_;
    std::vector<models::jira_issue_type> issue_types_;
    std::string selected_issue_type_;
    char issue_summary_[256] = "";
    char issue_description_[4096] = "";
    std::string create_issue_error_;
    std::string create_issue_success_;
};

} // namespace rouen::cards
