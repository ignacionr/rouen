#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <chrono>
#include <format>
#include <algorithm>

#include "imgui.h"
#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include "../../helpers/views/json_view.hpp"
#include "../../helpers/api_keys.hpp"
#include "../../helpers/debug.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {

class jira_card : public card {
public:
    jira_card() {
        // Set custom colors
        colors[0] = {0.0f, 0.4f, 0.8f, 1.0f};  // Jira blue - primary color
        colors[1] = {0.1f, 0.5f, 0.9f, 0.7f};  // Light blue - secondary color
        
        // Additional colors - using the parent class's get_color method
        colors[2] = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); // Error color - red
        colors[3] = ImVec4(0.2f, 0.7f, 0.2f, 1.0f); // Success color - green
        colors[4] = ImVec4(0.9f, 0.7f, 0.0f, 1.0f); // Warning color - amber
        colors[5] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Neutral color - gray
        colors[6] = ImVec4(1.0f, 1.0f, 1.0f, 0.8f); // Text color - white
        colors[7] = ImVec4(0.7f, 0.1f, 0.1f, 1.0f); // Critical status - red
        colors[8] = ImVec4(0.7f, 0.7f, 0.1f, 1.0f); // In progress status - yellow
        colors[9] = ImVec4(0.1f, 0.6f, 0.1f, 1.0f); // Done status - green
        
        // Set window properties
        name("Jira");
        width = 800.0f;  // Wider width for data tables
        
        // Initialize the JIRA model
        jira_host_ = std::make_shared<models::jira_model>();
        
        // Load available profiles
        refresh_profiles();
        
        // Check for direct connection via environment variables
        auto env_profiles = jira_host_->detect_environment_profiles();
        if (!env_profiles.empty()) {
            // Try to connect using the first environment profile
            try_connect(env_profiles[0]);
        }
    }
    
    bool render() override {
        return render_window([this]() {
            // If not connected, show login screen
            if (!jira_host_->is_connected()) {
                render_connection_screen();
            } else {
                // Connected - show main interface
                if (ImGui::BeginTabBar("JiraTabs")) {
                    if (ImGui::BeginTabItem("Projects")) {
                        render_projects_tab();
                        ImGui::EndTabItem();
                    }
                    
                    if (ImGui::BeginTabItem("My Issues")) {
                        render_my_issues_tab();
                        ImGui::EndTabItem();
                    }
                    
                    if (ImGui::BeginTabItem("Search")) {
                        render_search_tab();
                        ImGui::EndTabItem();
                    }
                    
                    if (ImGui::BeginTabItem("Create Issue")) {
                        render_create_issue_tab();
                        ImGui::EndTabItem();
                    }
                    
                    ImGui::EndTabBar();
                }
                
                // Logout button (bottom right)
                ImGui::Separator();
                ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 100);
                if (ImGui::Button("Logout", ImVec2(100, 0))) {
                    jira_host_->disconnect();
                }
            }
        });
    }
    
    std::string get_uri() const override {
        return "jira";
    }
    
private:
    // Connection screen rendering
    void render_connection_screen() {
        ImGui::TextColored(colors[0], ICON_MD_CLOUD_QUEUE " Connect to Jira");
        ImGui::Separator();
        
        // Error message if login failed
        if (!error_message_.empty()) {
            ImGui::TextColored(colors[2], "%s", error_message_.c_str());
            ImGui::Separator();
        }
        
        // Profiles selection
        if (!available_profiles_.empty()) {
            ImGui::Text("Select a saved profile:");
            
            for (const auto& profile : available_profiles_) {
                std::string label = profile.name;
                if (profile.is_environment) {
                    label += " (environment)";
                }
                
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        try_connect(profile);
                    } else {
                        current_profile_ = profile;
                        // Fix character array assignments by using strncpy
                        std::strncpy(url_buffer_, profile.server_url.c_str(), sizeof(url_buffer_) - 1);
                        std::strncpy(username_buffer_, profile.username.c_str(), sizeof(username_buffer_) - 1);
                        std::strncpy(token_buffer_, profile.api_token.c_str(), sizeof(token_buffer_) - 1);
                        std::strncpy(profile_name_buffer_, profile.name.c_str(), sizeof(profile_name_buffer_) - 1);
                    }
                }
            }
            
            ImGui::Separator();
        }
        
        // Manual connection form
        ImGui::Text("Connect manually:");
        
        // Profile name (only for saving)
        ImGui::InputText("Profile Name", profile_name_buffer_, sizeof(profile_name_buffer_));
        
        // Server URL
        ImGui::InputText("Server URL", url_buffer_, sizeof(url_buffer_));
        
        // Username
        ImGui::InputText("Username", username_buffer_, sizeof(username_buffer_));
        
        // API Token (password field)
        ImGui::InputText("API Token", token_buffer_, sizeof(token_buffer_), ImGuiInputTextFlags_Password);
        
        // Connect button
        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            // Create a profile from the form data
            models::jira_connection_profile profile;
            profile.name = profile_name_buffer_;
            profile.server_url = url_buffer_;
            profile.username = username_buffer_;
            profile.api_token = token_buffer_;
            
            // Try to connect
            try_connect(profile);
        }
        
        ImGui::SameLine();
        
        // Save profile button
        if (ImGui::Button("Save Profile", ImVec2(120, 0))) {
            // Create a profile from the form data
            models::jira_connection_profile profile;
            profile.name = profile_name_buffer_;
            profile.server_url = url_buffer_;
            profile.username = username_buffer_;
            profile.api_token = token_buffer_;
            
            // Save the profile
            jira_host_->save_profile(profile);
            
            // Refresh the profiles list
            refresh_profiles();
        }
        
        ImGui::SameLine();
        
        // Refresh profiles button
        if (ImGui::Button("Refresh", ImVec2(120, 0))) {
            refresh_profiles();
        }
    }
    
    // Projects tab rendering
    void render_projects_tab() {
        // Refresh button
        if (ImGui::Button("Refresh Projects")) {
            refresh_projects();
        }
        
        // Show loading indicator or error
        if (is_loading_projects_) {
            ImGui::TextColored(colors[1], "Loading projects...");
        } else if (!error_message_.empty()) {
            ImGui::TextColored(colors[2], "%s", error_message_.c_str());
        } else if (projects_.empty()) {
            ImGui::TextColored(colors[5], "No projects found. Click Refresh to try again.");
        }
        
        // Project filter
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##project_filter", "Filter projects...", project_filter_, sizeof(project_filter_));
        ImGui::PopItemWidth();
        
        // Projects list
        if (ImGui::BeginTable("ProjectsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();
            
            std::string filter = project_filter_;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            
            for (const auto& project : projects_) {
                // Apply filter
                std::string name_lower = project.name;
                std::string key_lower = project.key;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
                
                if (!filter.empty() && 
                    name_lower.find(filter) == std::string::npos && 
                    key_lower.find(filter) == std::string::npos) {
                    continue;
                }
                
                // Project row
                ImGui::TableNextRow();
                
                // Key column
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[0], "%s", project.key.c_str());
                
                // Name column
                ImGui::TableNextColumn();
                ImGui::Text("%s", project.name.c_str());
                if (!project.description.empty()) {
                    ImGui::TextColored(colors[5], "%s", project.description.c_str());
                }
                
                // Actions column
                ImGui::TableNextColumn();
                std::string view_btn_id = "View##" + project.key;
                if (ImGui::Button(view_btn_id.c_str())) {
                    selected_project_ = project;
                    load_project_issues(project.key);
                }
            }
            
            ImGui::EndTable();
        }
        
        // Show selected project details
        if (selected_project_.key.empty()) {
            return;
        }
        
        ImGui::Separator();
        ImGui::TextColored(colors[0], "Issues for %s: %s", 
                          selected_project_.key.c_str(), 
                          selected_project_.name.c_str());
        
        // Issue filter
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##issue_filter", "Filter issues...", issue_filter_, sizeof(issue_filter_));
        ImGui::PopItemWidth();
        
        // Loading indicator
        if (is_loading_issues_) {
            ImGui::TextColored(colors[1], "Loading issues...");
        }
        
        // Issues table
        if (ImGui::BeginTable("IssuesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            
            std::string filter = issue_filter_;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            
            for (const auto& issue : project_issues_) {
                // Apply filter
                std::string summary_lower = issue.summary;
                std::string key_lower = issue.key;
                std::transform(summary_lower.begin(), summary_lower.end(), summary_lower.begin(), ::tolower);
                std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
                
                if (!filter.empty() && 
                    summary_lower.find(filter) == std::string::npos && 
                    key_lower.find(filter) == std::string::npos) {
                    continue;
                }
                
                // Issue row
                ImGui::TableNextRow();
                
                // Key column
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[0], "%s", issue.key.c_str());
                
                // Summary column
                ImGui::TableNextColumn();
                ImGui::Text("%s", issue.summary.c_str());
                
                // Status column
                ImGui::TableNextColumn();
                ImVec4 status_color = colors[5]; // Default gray
                
                // Map status category to color
                if (issue.status.category == "To Do") {
                    status_color = colors[5]; // Gray
                } else if (issue.status.category == "In Progress") {
                    status_color = colors[8]; // Yellow
                } else if (issue.status.category == "Done") {
                    status_color = colors[9]; // Green
                }
                
                ImGui::TextColored(status_color, "%s", issue.status.name.c_str());
                
                // Actions column
                ImGui::TableNextColumn();
                std::string view_btn_id = "View##" + issue.key;
                if (ImGui::Button(view_btn_id.c_str())) {
                    selected_issue_ = issue;
                    show_issue_details_ = true;
                }
            }
            
            ImGui::EndTable();
        }
        
        // Issue details popup
        render_issue_details_popup();
    }
    
    // My Issues tab rendering
    void render_my_issues_tab() {
        // Refresh button
        if (ImGui::Button("Refresh My Issues")) {
            refresh_my_issues();
        }
        
        ImGui::SameLine();
        
        // Status filter dropdown
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("Status Filter", status_filter_.c_str())) {
            if (ImGui::Selectable("All", status_filter_ == "All")) {
                status_filter_ = "All";
                refresh_my_issues();
            }
            if (ImGui::Selectable("To Do", status_filter_ == "To Do")) {
                status_filter_ = "To Do";
                refresh_my_issues();
            }
            if (ImGui::Selectable("In Progress", status_filter_ == "In Progress")) {
                status_filter_ = "In Progress";
                refresh_my_issues();
            }
            if (ImGui::Selectable("Done", status_filter_ == "Done")) {
                status_filter_ = "Done";
                refresh_my_issues();
            }
            ImGui::EndCombo();
        }
        
        // Show loading indicator or error
        if (is_loading_my_issues_) {
            ImGui::TextColored(colors[1], "Loading issues...");
        } else if (!error_message_.empty()) {
            ImGui::TextColored(colors[2], "%s", error_message_.c_str());
        } else if (my_issues_.empty()) {
            ImGui::TextColored(colors[5], "No issues assigned to you found.");
        }
        
        // Issue filter
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##my_issue_filter", "Filter issues...", my_issue_filter_, sizeof(my_issue_filter_));
        ImGui::PopItemWidth();
        
        // Issues table
        if (ImGui::BeginTable("MyIssuesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            
            std::string filter = my_issue_filter_;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            
            for (const auto& issue : my_issues_) {
                // Apply filter
                std::string summary_lower = issue.summary;
                std::string key_lower = issue.key;
                std::transform(summary_lower.begin(), summary_lower.end(), summary_lower.begin(), ::tolower);
                std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
                
                if (!filter.empty() && 
                    summary_lower.find(filter) == std::string::npos && 
                    key_lower.find(filter) == std::string::npos) {
                    continue;
                }
                
                // Issue row
                ImGui::TableNextRow();
                
                // Key column
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[0], "%s", issue.key.c_str());
                
                // Project column
                ImGui::TableNextColumn();
                // Extract project key from issue key (e.g., "PROJ-123" -> "PROJ")
                std::string project_key = issue.key.substr(0, issue.key.find('-'));
                ImGui::Text("%s", project_key.c_str());
                
                // Summary column
                ImGui::TableNextColumn();
                ImGui::Text("%s", issue.summary.c_str());
                
                // Status column
                ImGui::TableNextColumn();
                ImVec4 status_color = colors[5]; // Default gray
                
                // Map status category to color
                if (issue.status.category == "To Do") {
                    status_color = colors[5]; // Gray
                } else if (issue.status.category == "In Progress") {
                    status_color = colors[8]; // Yellow
                } else if (issue.status.category == "Done") {
                    status_color = colors[9]; // Green
                }
                
                ImGui::TextColored(status_color, "%s", issue.status.name.c_str());
                
                // Actions column
                ImGui::TableNextColumn();
                std::string view_btn_id = "View##" + issue.key;
                if (ImGui::Button(view_btn_id.c_str())) {
                    selected_issue_ = issue;
                    show_issue_details_ = true;
                }
            }
            
            ImGui::EndTable();
        }
        
        // Issue details popup
        render_issue_details_popup();
    }
    
    // Search tab rendering
    void render_search_tab() {
        ImGui::TextColored(colors[0], "Search Issues with JQL");
        ImGui::TextWrapped("JQL (Jira Query Language) lets you search for issues with complex criteria");
        
        // JQL Query input
        ImGui::PushItemWidth(-120); // Leave space for the button
        ImGui::InputTextWithHint("##jql_query", "Enter JQL query... (e.g., project = KEY AND status = \"In Progress\")", 
                               jql_query_, sizeof(jql_query_));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        
        // Search button
        if (ImGui::Button("Search", ImVec2(100, 0))) {
            perform_jql_search();
        }
        
        // Example JQL queries
        if (ImGui::TreeNode("Example Queries")) {
            render_example_jql_queries();
            ImGui::TreePop();
        }
        
        // Show loading indicator or error
        if (is_loading_search_results_) {
            ImGui::TextColored(colors[1], "Searching...");
        } else if (!search_error_.empty()) {
            ImGui::TextColored(colors[2], "%s", search_error_.c_str());
        }
        
        // Search results
        if (!search_results_.issues.empty()) {
            ImGui::Separator();
            ImGui::TextColored(colors[0], "Search Results: %d issue(s) found", search_results_.total);
            
            if (search_results_.total > static_cast<int>(search_results_.issues.size())) {
                ImGui::SameLine();
                ImGui::TextColored(colors[4], "(showing %zu of %d)", 
                                 search_results_.issues.size(), search_results_.total);
                
                // Load more button
                if (ImGui::Button("Load More")) {
                    load_more_search_results();
                }
            }
            
            // Results table
            if (ImGui::BeginTable("SearchResultsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();
                
                for (const auto& issue : search_results_.issues) {
                    // Issue row
                    ImGui::TableNextRow();
                    
                    // Key column
                    ImGui::TableNextColumn();
                    ImGui::TextColored(colors[0], "%s", issue.key.c_str());
                    
                    // Project column
                    ImGui::TableNextColumn();
                    // Extract project key from issue key (e.g., "PROJ-123" -> "PROJ")
                    std::string project_key = issue.key.substr(0, issue.key.find('-'));
                    ImGui::Text("%s", project_key.c_str());
                    
                    // Summary column
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", issue.summary.c_str());
                    
                    // Status column
                    ImGui::TableNextColumn();
                    ImVec4 status_color = colors[5]; // Default gray
                    
                    // Map status category to color
                    if (issue.status.category == "To Do") {
                        status_color = colors[5]; // Gray
                    } else if (issue.status.category == "In Progress") {
                        status_color = colors[8]; // Yellow
                    } else if (issue.status.category == "Done") {
                        status_color = colors[9]; // Green
                    }
                    
                    ImGui::TextColored(status_color, "%s", issue.status.name.c_str());
                    
                    // Actions column
                    ImGui::TableNextColumn();
                    std::string view_btn_id = "View##" + issue.key;
                    if (ImGui::Button(view_btn_id.c_str())) {
                        selected_issue_ = issue;
                        show_issue_details_ = true;
                    }
                }
                
                ImGui::EndTable();
            }
        }
        
        // Issue details popup
        render_issue_details_popup();
    }
    
    // Create issue tab rendering
    void render_create_issue_tab() {
        ImGui::TextColored(colors[0], "Create New Issue");
        
        // Project selection
        ImGui::Text("Project:");
        ImGui::SameLine();
        
        // Project dropdown
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##project_select", selected_project_key_.c_str())) {
            for (const auto& project : projects_) {
                if (ImGui::Selectable(project.key.c_str(), selected_project_key_ == project.key)) {
                    selected_project_key_ = project.key;
                    // Load issue types for this project
                    load_issue_types(selected_project_key_);
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh Projects")) {
            refresh_projects();
        }
        
        // Issue type selection (only show if project is selected)
        if (!selected_project_key_.empty()) {
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
            
            // Create button
            if (ImGui::Button("Create Issue", ImVec2(150, 0))) {
                create_issue();
            }
            
            ImGui::SameLine();
            
            // Reset button
            if (ImGui::Button("Reset Form", ImVec2(150, 0))) {
                reset_create_form();
            }
            
            // Success or error message
            if (!create_issue_error_.empty()) {
                ImGui::TextColored(colors[2], "%s", create_issue_error_.c_str());
            }
            
            if (!create_issue_success_.empty()) {
                ImGui::TextColored(colors[3], "%s", create_issue_success_.c_str());
            }
        } else {
            ImGui::TextColored(colors[5], "Please select a project first");
        }
    }
    
    // Issue details popup
    void render_issue_details_popup() {
        if (!show_issue_details_ || selected_issue_.key.empty()) {
            return;
        }
        
        // Popup modal for issue details
        ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
        ImGui::OpenPopup(std::format("Issue: {}", selected_issue_.key).c_str());
        
        if (ImGui::BeginPopupModal(std::format("Issue: {}", selected_issue_.key).c_str(), &show_issue_details_)) {
            // Key and basic info section
            ImGui::TextColored(colors[0], "%s: %s", selected_issue_.key.c_str(), selected_issue_.summary.c_str());
            
            // Status with color
            ImVec4 status_color = colors[5]; // Default gray
            
            // Map status category to color
            if (selected_issue_.status.category == "To Do") {
                status_color = colors[5]; // Gray
            } else if (selected_issue_.status.category == "In Progress") {
                status_color = colors[8]; // Yellow
            } else if (selected_issue_.status.category == "Done") {
                status_color = colors[9]; // Green
            }
            
            ImGui::TextColored(status_color, "Status: %s", selected_issue_.status.name.c_str());
            
            // Issue type
            ImGui::Text("Type: %s", selected_issue_.issue_type.name.c_str());
            
            // Assignee
            if (!selected_issue_.assignee.display_name.empty()) {
                ImGui::Text("Assignee: %s", selected_issue_.assignee.display_name.c_str());
            } else {
                ImGui::TextColored(colors[5], "Assignee: Unassigned");
            }
            
            // Created/Updated
            ImGui::Text("Created: %s", format_jira_date(selected_issue_.created).c_str());
            ImGui::Text("Updated: %s", format_jira_date(selected_issue_.updated).c_str());
            
            ImGui::Separator();
            
            // Description (in a scrollable region)
            ImGui::TextColored(colors[0], "Description:");
            ImGui::BeginChild("Description", ImVec2(0, 200), true);
            if (!selected_issue_.description.empty()) {
                ImGui::TextWrapped("%s", selected_issue_.description.c_str());
            } else {
                ImGui::TextColored(colors[5], "No description provided");
            }
            ImGui::EndChild();
            
            // Transitions
            ImGui::Separator();
            ImGui::TextColored(colors[0], "Transitions:");
            
            // Only load transitions if we haven't loaded them yet
            if (issue_transitions_.empty() && !is_loading_transitions_) {
                load_issue_transitions(selected_issue_.key);
            }
            
            if (is_loading_transitions_) {
                ImGui::TextColored(colors[1], "Loading available transitions...");
            } else if (issue_transitions_.empty()) {
                ImGui::TextColored(colors[5], "No transitions available");
            } else {
                for (const auto& transition : issue_transitions_) {
                    if (ImGui::Button(transition.name.c_str(), ImVec2(150, 0))) {
                        transition_issue(selected_issue_.key, transition.id);
                    }
                    ImGui::SameLine();
                    
                    // Show target status
                    ImGui::TextColored(colors[5], "-> %s", transition.to_status.name.c_str());
                    
                    // Wrap buttons after 2 transitions
                    if (&transition != &issue_transitions_.back() && 
                        (&transition - &issue_transitions_.front() + 1) % 2 == 0) {
                        ImGui::NewLine();
                    } else {
                        ImGui::SameLine();
                    }
                }
            }
            
            ImGui::NewLine();
            ImGui::Separator();
            
            // Close button
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                show_issue_details_ = false;
                issue_transitions_.clear();
            }
            
            ImGui::EndPopup();
        }
    }
    
    // Example JQL queries rendering
    void render_example_jql_queries() {
        const std::vector<std::pair<std::string, std::string>> examples = {
            {"My open issues", "assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC"},
            {"Recently updated", "updated >= -1w ORDER BY updated DESC"},
            {"Created by me", "reporter = currentUser() ORDER BY created DESC"},
            {"Done last week", "status = Done AND statusChanged >= -1w ORDER BY updated DESC"},
            {"High priority", "priority = High AND resolution = Unresolved ORDER BY updated DESC"},
            {"Bugs", "issuetype = Bug AND resolution = Unresolved ORDER BY priority DESC"}
        };
        
        for (const auto& [name, query] : examples) {
            if (ImGui::Button(name.c_str(), ImVec2(150, 0))) {
                std::strncpy(jql_query_, query.c_str(), sizeof(jql_query_) - 1);
            }
            ImGui::SameLine();
            ImGui::TextColored(colors[5], "%s", query.c_str());
            ImGui::Separator();
        }
    }
    
    // Connect to JIRA with specified profile
    void try_connect(const models::jira_connection_profile& profile) {
        // Clear previous errors
        error_message_.clear();
        
        try {
            if (jira_host_->connect(profile)) {
                // Connection successful
                JIRA_INFO_FMT("Connected to JIRA server: {}", profile.server_url);
                
                // Load initial data
                refresh_projects();
                refresh_my_issues();
            } else {
                error_message_ = "Failed to connect to JIRA server";
                JIRA_ERROR_FMT("Failed to connect to JIRA server: {}", profile.server_url);
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Connection error: {}", e.what());
            JIRA_ERROR_FMT("JIRA connection error: {}", e.what());
        }
    }
    
    // Load profiles from model
    void refresh_profiles() {
        available_profiles_ = jira_host_->load_profiles();
        
        // Also add environment profiles
        auto env_profiles = jira_host_->detect_environment_profiles();
        available_profiles_.insert(available_profiles_.end(), env_profiles.begin(), env_profiles.end());
    }
    
    // Load all projects
    void refresh_projects() {
        is_loading_projects_ = true;
        error_message_.clear();
        
        auto future = jira_host_->get_projects();
        
        // Handle the future asynchronously
        std::thread([this, future = std::move(future)]() mutable {
            try {
                projects_ = future.get();
                is_loading_projects_ = false;
            } catch (const std::exception& e) {
                error_message_ = std::format("Error loading projects: {}", e.what());
                is_loading_projects_ = false;
                JIRA_ERROR_FMT("Error loading projects: {}", e.what());
            }
        }).detach();
    }
    
    // Load issues for a specific project
    void load_project_issues(const std::string& project_key) {
        is_loading_issues_ = true;
        
        // Use search_issues to get issues for the project
        std::string jql = std::format("project = {} ORDER BY updated DESC", project_key);
        auto future = jira_host_->search_issues(jql);
        
        // Handle the future asynchronously
        std::thread([this, future = std::move(future)]() mutable {
            try {
                auto result = future.get();
                // Store the issues from the search result
                project_issues_ = result.issues;
                is_loading_issues_ = false;
            } catch (const std::exception& e) {
                error_message_ = std::format("Failed to load issues: {}", e.what());
                is_loading_issues_ = false;
            }
        }).detach();
    }
    
    // Load issues assigned to current user
    void refresh_my_issues() {
        is_loading_my_issues_ = true;
        error_message_.clear();
        
        // Construct JQL query based on status filter
        std::string jql = "assignee = currentUser()";
        
        if (status_filter_ != "All") {
            jql += std::format(" AND statusCategory = \"{}\"", status_filter_);
        }
        
        jql += " ORDER BY updated DESC";
        
        auto future = jira_host_->search_issues(jql);
        
        // Handle the future asynchronously
        std::thread([this, future = std::move(future)]() mutable {
            try {
                auto search_result = future.get();
                my_issues_ = search_result.issues;
                is_loading_my_issues_ = false;
            } catch (const std::exception& e) {
                error_message_ = std::format("Error loading your issues: {}", e.what());
                is_loading_my_issues_ = false;
                JIRA_ERROR_FMT("Error loading user issues: {}", e.what());
            }
        }).detach();
    }
    
    // Load issue types for a project
    void load_issue_types(const std::string& project_key) {
        issue_types_.clear();
        
        // Find the project
        auto it = std::find_if(projects_.begin(), projects_.end(), 
                            [&](const auto& p) { return p.key == project_key; });
        
        if (it != projects_.end()) {
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
    
    // Perform JQL search
    void perform_jql_search() {
        if (std::string(jql_query_).empty()) {
            search_error_ = "Please enter a JQL query";
            return;
        }
        
        search_error_.clear();
        is_loading_search_results_ = true;
        current_search_start_at_ = 0;
        
        auto future = jira_host_->search_issues(jql_query_, current_search_start_at_);
        
        // Handle the future asynchronously
        std::thread([this, future = std::move(future)]() mutable {
            try {
                search_results_ = future.get();
                is_loading_search_results_ = false;
            } catch (const std::exception& e) {
                search_error_ = std::format("Search error: {}", e.what());
                is_loading_search_results_ = false;
                JIRA_ERROR_FMT("JQL search error: {}", e.what());
            }
        }).detach();
    }
    
    // Load more search results (pagination)
    void load_more_search_results() {
        if (is_loading_search_results_) {
            return;
        }
        
        is_loading_search_results_ = true;
        current_search_start_at_ += search_results_.max_results;
        
        auto future = jira_host_->search_issues(jql_query_, current_search_start_at_);
        
        // Handle the future asynchronously
        std::thread([this, future = std::move(future)]() mutable {
            try {
                auto next_page = future.get();
                
                // Append new issues to existing results
                search_results_.issues.insert(
                    search_results_.issues.end(), 
                    next_page.issues.begin(), 
                    next_page.issues.end()
                );
                
                is_loading_search_results_ = false;
            } catch (const std::exception& e) {
                search_error_ = std::format("Error loading more results: {}", e.what());
                is_loading_search_results_ = false;
                JIRA_ERROR_FMT("Error loading more search results: {}", e.what());
            }
        }).detach();
    }
    
    // Load transitions for an issue
    void load_issue_transitions(const std::string& issue_key) {
        is_loading_transitions_ = true;
        
        auto future = jira_host_->get_transitions(issue_key);
        
        // Handle the future asynchronously
        std::thread([this, future = std::move(future)]() mutable {
            try {
                issue_transitions_ = future.get();
                is_loading_transitions_ = false;
            } catch (const std::exception& e) {
                error_message_ = std::format("Error loading transitions: {}", e.what());
                is_loading_transitions_ = false;
                JIRA_ERROR_FMT("Error loading issue transitions: {}", e.what());
            }
        }).detach();
    }
    
    // Transition an issue to a new status
    void transition_issue(const std::string& issue_key, const std::string& transition_id) {
        if (jira_host_->transition_issue(issue_key, transition_id)) {
            // Success - refresh the issue details
            auto future = jira_host_->get_issue(issue_key);
            
            std::thread([this, issue_key, future = std::move(future)]() mutable {
                try {
                    auto updated_issue = future.get();
                    selected_issue_ = updated_issue;
                    
                    // Also refresh transitions
                    issue_transitions_.clear();
                    load_issue_transitions(issue_key);
                    
                    // Refresh affected lists
                    refresh_my_issues();
                    if (!selected_project_.key.empty()) {
                        load_project_issues(selected_project_.key);
                    }
                } catch (const std::exception& e) {
                    error_message_ = std::format("Error refreshing issue: {}", e.what());
                    JIRA_ERROR_FMT("Error refreshing issue after transition: {}", e.what());
                }
            }).detach();
        } else {
            error_message_ = "Failed to transition issue";
            JIRA_ERROR("Failed to transition issue");
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
        std::thread([this, future = std::move(future)]() mutable {
            try {
                auto created_issue = future.get();
                
                if (!created_issue.key.empty()) {
                    create_issue_success_ = std::format("Issue created: {}", created_issue.key);
                    JIRA_INFO_FMT("Issue created successfully: {}", created_issue.key);
                    
                    // Reset form
                    reset_create_form();
                    
                    // Refresh affected lists
                    refresh_my_issues();
                    if (!selected_project_.key.empty() && selected_project_.key == selected_project_key_) {
                        load_project_issues(selected_project_.key);
                    }
                } else {
                    create_issue_error_ = "Failed to create issue";
                    JIRA_ERROR("Failed to create issue");
                }
            } catch (const std::exception& e) {
                create_issue_error_ = std::format("Error creating issue: {}", e.what());
                JIRA_ERROR_FMT("Error creating issue: {}", e.what());
            }
        }).detach();
    }
    
    // Reset create issue form
    void reset_create_form() {
        issue_summary_[0] = '\0';
        issue_description_[0] = '\0';
        create_issue_error_.clear();
        create_issue_success_.clear();
    }
    
    // Format JIRA date string
    std::string format_jira_date(const std::string& jira_date) {
        if (jira_date.empty()) {
            return "";
        }
        
        // Example JIRA date: "2023-05-22T14:35:30.000+0000"
        // Just extract the date part for simplicity
        if (jira_date.size() >= 10) {
            return jira_date.substr(0, 10);
        }
        
        return jira_date;
    }
    
    // JIRA model
    std::shared_ptr<models::jira_model> jira_host_;
    
    // Connection state and profiles
    std::vector<models::jira_connection_profile> available_profiles_;
    models::jira_connection_profile current_profile_;
    std::string error_message_;
    
    // Connection form
    char url_buffer_[256] = "";
    char username_buffer_[128] = "";
    char token_buffer_[256] = "";
    char profile_name_buffer_[128] = "Default";
    
    // Projects data
    std::vector<models::jira_project> projects_;
    bool is_loading_projects_ = false;
    char project_filter_[256] = "";
    models::jira_project selected_project_;
    
    // Project issues data
    std::vector<models::jira_issue> project_issues_;
    bool is_loading_issues_ = false;
    char issue_filter_[256] = "";
    
    // My issues data
    std::vector<models::jira_issue> my_issues_;
    bool is_loading_my_issues_ = false;
    char my_issue_filter_[256] = "";
    std::string status_filter_ = "All";
    
    // Search data
    char jql_query_[1024] = "";
    models::jira_search_result search_results_;
    bool is_loading_search_results_ = false;
    std::string search_error_;
    int current_search_start_at_ = 0;
    
    // Issue details
    models::jira_issue selected_issue_;
    bool show_issue_details_ = false;
    std::vector<models::jira_transition> issue_transitions_;
    bool is_loading_transitions_ = false;
    
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