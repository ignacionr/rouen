#pragma once

#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <format>

namespace rouen::cards {

class jira_projects_card : public card {
public:
    jira_projects_card() {
        // Set custom colors for the JIRA projects card theme
        colors[0] = ImVec4{0.0f, 0.48f, 0.8f, 1.0f};  // JIRA blue primary
        colors[1] = ImVec4{0.1f, 0.58f, 0.9f, 0.7f};  // JIRA blue secondary
        get_color(2, ImVec4(0.0f, 0.67f, 1.0f, 1.0f));  // Highlighted item color
        get_color(3, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));   // Gray text
        get_color(4, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));   // Success green
        get_color(5, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));   // Error red
        get_color(6, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));   // Warning yellow
        
        name("JIRA Projects");
        width = 500.0f;
        requested_fps = 5;
        
        // Initialize model
        jira_model_ = std::make_shared<models::jira_model>();
        
        // Load connection profiles
        connection_profiles_ = models::jira_model::load_profiles();
        
        // If we have environment variables, try to connect to the first profile automatically
        if (!connection_profiles_.empty()) {
            std::thread([this]() {
                bool success = jira_model_->connect(connection_profiles_[0]);
                if (success) {
                    status_message_ = std::format("Connected to {} automatically", connection_profiles_[0].name);
                    
                    // Fetch projects after connection
                    projects_future_ = jira_model_->get_projects();
                }
            }).detach();
        }
    }
    
    bool render() override {
        return render_window([this]() {
            // If not connected, show connection prompt
            if (!jira_model_->is_connected()) {
                render_connection_prompt();
                return;
            }
            
            check_async_operations();
            
            // Render project overview
            if (projects_.empty()) {
                ImGui::TextColored(colors[3], "Loading projects...");
                return;
            }
            
            // Project selection
            if (ImGui::BeginCombo("Select Project", selected_project_name_.c_str())) {
                for (const auto& project : projects_) {
                    if (ImGui::Selectable(std::format("{}  -  {}", project.key, project.name).c_str(), 
                                         project.key == selected_project_key_)) {
                        selected_project_key_ = project.key;
                        selected_project_name_ = project.name;
                        
                        // Get project details and statistics
                        fetch_project_details();
                    }
                    
                    if (ImGui::IsItemHovered() && !project.description.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::TextWrapped("%s", project.description.c_str());
                        ImGui::EndTooltip();
                    }
                }
                ImGui::EndCombo();
            }
            
            ImGui::Separator();
            
            if (selected_project_key_.empty()) {
                render_projects_overview();
            } else {
                render_project_details();
            }
        });
    }
    
    std::string get_uri() const override {
        return "jira-projects";
    }
    
private:
    std::shared_ptr<models::jira_model> jira_model_;
    
    // Connection state
    std::vector<models::jira_connection_profile> connection_profiles_;
    std::string status_message_;
    
    // Projects state
    std::future<std::vector<models::jira_project>> projects_future_;
    std::vector<models::jira_project> projects_;
    std::string selected_project_key_;
    std::string selected_project_name_;
    
    // Project details
    std::future<models::jira_search_result> project_issues_future_;
    std::optional<models::jira_search_result> project_issues_;
    
    // Statistics
    struct project_stats {
        int total_issues = 0;
        int open_issues = 0;
        int in_progress_issues = 0;
        int done_issues = 0;
        std::unordered_map<std::string, int> issues_by_type;
        std::unordered_map<std::string, int> issues_by_assignee;
    };
    
    std::unordered_map<std::string, project_stats> project_statistics_;
    
    void check_async_operations() {
        // Check if projects have been loaded
        if (projects_future_.valid() && 
            projects_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            projects_ = projects_future_.get();
            
            // If we have projects but none selected, fetch overview stats
            if (!projects_.empty() && selected_project_key_.empty()) {
                for (const auto& project : projects_) {
                    if (project_statistics_.find(project.key) == project_statistics_.end()) {
                        fetch_project_statistics(project.key);
                    }
                }
            }
        }
        
        // Check if project issues have been loaded
        if (project_issues_future_.valid() && 
            project_issues_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            project_issues_ = project_issues_future_.get();
            
            // Calculate statistics
            if (project_issues_.has_value() && !selected_project_key_.empty()) {
                calculate_project_statistics(selected_project_key_, project_issues_.value());
            }
        }
    }
    
    void render_connection_prompt() {
        ImGui::TextColored(colors[3], "Not connected to JIRA.");
        ImGui::Separator();
        
        ImGui::TextWrapped("Please connect to JIRA first using the main JIRA card.");
        
        if (ImGui::Button("Open JIRA Card", ImVec2(150, 0))) {
            // Use the registrar to open the main JIRA card
            "create_card"_sfn("jira");
            grab_focus = false;  // Give focus to the new card
        }
    }
    
    void fetch_project_details() {
        // Reset existing data
        project_issues_.reset();
        
        // Fetch all issues for this project
        std::string jql = std::format("project = {} ORDER BY updated DESC", selected_project_key_);
        project_issues_future_ = jira_model_->search_issues(jql, 0, 500); // Get more issues for stats
    }
    
    void fetch_project_statistics(const std::string& project_key) {
        // Fetch statistics for a project in the background
        std::thread([this, project_key]() {
            std::string jql = std::format("project = {} ORDER BY updated DESC", project_key);
            auto future = jira_model_->search_issues(jql, 0, 100); // Limit to 100 for overview
            auto result = future.get();
            
            calculate_project_statistics(project_key, result);
        }).detach();
    }
    
    void calculate_project_statistics(const std::string& project_key, const models::jira_search_result& result) {
        // Calculate statistics for the project
        project_stats stats;
        stats.total_issues = result.total;
        
        for (const auto& issue : result.issues) {
            // Count by status category
            if (issue.status.category == "To Do") {
                stats.open_issues++;
            } else if (issue.status.category == "In Progress") {
                stats.in_progress_issues++;
            } else if (issue.status.category == "Done") {
                stats.done_issues++;
            }
            
            // Count by issue type
            stats.issues_by_type[issue.issue_type.name]++;
            
            // Count by assignee
            std::string assignee_name = issue.assignee.display_name.empty() 
                ? "Unassigned" 
                : issue.assignee.display_name;
            stats.issues_by_assignee[assignee_name]++;
        }
        
        // Store the statistics
        project_statistics_[project_key] = stats;
    }
    
    void render_projects_overview() {
        ImGui::TextColored(colors[0], "JIRA Projects Overview");
        ImGui::Text("Connected to: %s", jira_model_->get_server_url().c_str());
        ImGui::Text("Total Projects: %zu", projects_.size());
        
        ImGui::Separator();
        
        // Create table of projects with key metrics
        if (ImGui::BeginTable("ProjectsTable", 4, 
                             ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            
            // Header row
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Issues", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            
            // Sort projects by name
            std::vector<std::reference_wrapper<const models::jira_project>> sorted_projects(
                projects_.begin(), projects_.end());
            std::sort(sorted_projects.begin(), sorted_projects.end(), 
                     [](const auto& a, const auto& b) { return a.get().name < b.get().name; });
            
            for (const auto& project_ref : sorted_projects) {
                const auto& project = project_ref.get();
                ImGui::TableNextRow();
                
                // Project name/key column
                ImGui::TableNextColumn();
                if (ImGui::Selectable(std::format("{}##proj_{}", project.key, project.id).c_str(), 
                                    false, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_project_key_ = project.key;
                    selected_project_name_ = project.name;
                    fetch_project_details();
                }
                ImGui::SameLine();
                ImGui::TextWrapped("%s", project.name.c_str());
                
                // Issues count column
                ImGui::TableNextColumn();
                if (project_statistics_.find(project.key) != project_statistics_.end()) {
                    ImGui::Text("%d", project_statistics_[project.key].total_issues);
                } else {
                    ImGui::TextColored(colors[3], "...");
                }
                
                // Progress bar column
                ImGui::TableNextColumn();
                if (project_statistics_.find(project.key) != project_statistics_.end()) {
                    const auto& stats = project_statistics_[project.key];
                    
                    if (stats.total_issues > 0) {
                        float done_ratio = static_cast<float>(stats.done_issues) / static_cast<float>(stats.total_issues);
                        float in_progress_ratio = static_cast<float>(stats.in_progress_issues) / static_cast<float>(stats.total_issues);
                        
                        // Progress bars for each status
                        ImVec2 bar_pos = ImGui::GetCursorScreenPos();
                        ImVec2 bar_size(ImGui::GetContentRegionAvail().x, 20.0f);
                        
                        // Background
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            bar_pos, 
                            ImVec2(bar_pos.x + bar_size.x, bar_pos.y + bar_size.y),
                            ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.5f)));
                        
                        // Done segment (green)
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            bar_pos, 
                            ImVec2(bar_pos.x + bar_size.x * done_ratio, bar_pos.y + bar_size.y),
                            ImGui::GetColorU32(ImVec4(0.0f, 0.7f, 0.2f, 1.0f)));
                        
                        // In progress segment (blue)
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            ImVec2(bar_pos.x + bar_size.x * done_ratio, bar_pos.y), 
                            ImVec2(bar_pos.x + bar_size.x * (done_ratio + in_progress_ratio), bar_pos.y + bar_size.y),
                            ImGui::GetColorU32(ImVec4(0.0f, 0.5f, 0.9f, 1.0f)));
                        
                        // To do segment (gray) - already covered by background
                        
                        // Add the percentage text centered on the bar
                        char overlay_text[32];
                        snprintf(overlay_text, sizeof(overlay_text), "%.0f%% Complete", static_cast<double>(done_ratio * 100.0f));
                        ImVec2 text_size = ImGui::CalcTextSize(overlay_text);
                        ImVec2 text_pos(
                            bar_pos.x + (bar_size.x - text_size.x) * 0.5f,
                            bar_pos.y + (bar_size.y - text_size.y) * 0.5f
                        );
                        
                        // Draw text with a contrasting shadow
                        ImGui::GetWindowDrawList()->AddText(
                            ImVec2(text_pos.x + 1, text_pos.y + 1),
                            ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)),
                            overlay_text
                        );
                        ImGui::GetWindowDrawList()->AddText(
                            text_pos,
                            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)),
                            overlay_text
                        );
                        
                        ImGui::Dummy(bar_size);
                    } else {
                        ImGui::TextColored(colors[3], "No issues");
                    }
                } else {
                    ImGui::TextColored(colors[3], "Loading...");
                }
                
                // Actions column
                ImGui::TableNextColumn();
                ImGui::PushID(std::format("actions_{}", project.key).c_str());
                if (ImGui::Button("View")) {
                    selected_project_key_ = project.key;
                    selected_project_name_ = project.name;
                    fetch_project_details();
                }
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Refresh All", ImVec2(120, 0))) {
            projects_future_ = jira_model_->get_projects();
            project_statistics_.clear();
        }
    }
    
    void render_project_details() {
        // Back button
        if (ImGui::Button("< Back to Projects", ImVec2(150, 0))) {
            selected_project_key_.clear();
            selected_project_name_.clear();
            project_issues_.reset();
            return;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh Project", ImVec2(120, 0))) {
            fetch_project_details();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Open in Browser", ImVec2(120, 0))) {
            std::string url = std::format("{}/browse/{}", jira_model_->get_server_url(), selected_project_key_);
            // Open URL in browser (platform-specific)
            #ifdef _WIN32
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            #elif defined(__APPLE__)
            system(std::format("open \"{}\"", url).c_str());
            #else
            system(std::format("xdg-open \"{}\"", url).c_str());
            #endif
        }
        
        ImGui::Separator();
        
        ImGui::TextColored(colors[0], "%s", selected_project_name_.c_str());
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "(%s)", selected_project_key_.c_str());
        
        if (!project_issues_.has_value()) {
            ImGui::TextColored(colors[3], "Loading project details...");
            return;
        }
        
        // If we have statistics for this project, show them
        if (project_statistics_.find(selected_project_key_) != project_statistics_.end()) {
            const auto& stats = project_statistics_[selected_project_key_];
            
            // Main stats in a 2x2 grid
            ImGui::Columns(3, "StatColumns", false);
            
            // Open issues
            ImGui::BeginChild("OpenIssues", ImVec2(0, 80), true);
            ImGui::TextColored(colors[3], "Open");
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Larger font if available
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%d", stats.open_issues);
            ImGui::PopFont();
            ImGui::EndChild();
            
            ImGui::NextColumn();
            
            // In progress issues
            ImGui::BeginChild("InProgressIssues", ImVec2(0, 80), true);
            ImGui::TextColored(colors[3], "In Progress");
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Larger font if available
            ImGui::TextColored(ImVec4(0.0f, 0.5f, 0.9f, 1.0f), "%d", stats.in_progress_issues);
            ImGui::PopFont();
            ImGui::EndChild();
            
            ImGui::NextColumn();
            
            // Done issues
            ImGui::BeginChild("DoneIssues", ImVec2(0, 80), true);
            ImGui::TextColored(colors[3], "Done");
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Larger font if available
            ImGui::TextColored(ImVec4(0.0f, 0.7f, 0.2f, 1.0f), "%d", stats.done_issues);
            ImGui::PopFont();
            ImGui::EndChild();
            
            ImGui::Columns(1);
            
            ImGui::Separator();
            
            // Tabs for different visualizations
            if (ImGui::BeginTabBar("ProjectDetailsTabs")) {
                if (ImGui::BeginTabItem("By Issue Type")) {
                    render_issues_by_type(stats);
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("By Assignee")) {
                    render_issues_by_assignee(stats);
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Issue List")) {
                    render_issues_list();
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
        } else {
            ImGui::TextColored(colors[3], "Loading project statistics...");
        }
    }
    
    void render_issues_by_type(const project_stats& stats) {
        // Sort issue types by count
        std::vector<std::pair<std::string, int>> sorted_types(
            stats.issues_by_type.begin(), stats.issues_by_type.end());
        std::sort(sorted_types.begin(), sorted_types.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // Calculate total for percentage
        int total = 0;
        for (const auto& [_, count] : sorted_types) {
            total += count;
        }
        
        // Bar chart
        if (ImGui::BeginChild("IssueTypesChart", ImVec2(0, 200), true)) {
            const float bar_height = 24.0f;
            const float chart_width = ImGui::GetContentRegionAvail().x;
            const float text_width = 100.0f;
            const float bar_width = chart_width - text_width - 50.0f;
            
            for (const auto& [type_name, count] : sorted_types) {
                // Type name
                ImGui::TextColored(colors[3], "%s", type_name.c_str());
                ImGui::SameLine(text_width);
                
                // Bar
                float percentage = total > 0 ? static_cast<float>(count) / static_cast<float>(total) : 0.0f;
                ImVec2 bar_pos = ImGui::GetCursorScreenPos();
                
                // Choose color based on issue type
                ImVec4 bar_color = colors[0]; // Default blue
                if (type_name == "Bug") {
                    bar_color = colors[5]; // Red for bugs
                } else if (type_name == "Task") {
                    bar_color = colors[3]; // Gray for tasks
                } else if (type_name == "Story") {
                    bar_color = colors[2]; // Blue for stories
                } else if (type_name == "Epic") {
                    bar_color = colors[6]; // Yellow for epics
                }
                
                // Background bar
                ImGui::GetWindowDrawList()->AddRectFilled(
                    bar_pos, 
                    ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_height),
                    ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.5f)));
                
                // Filled portion of bar
                ImGui::GetWindowDrawList()->AddRectFilled(
                    bar_pos, 
                    ImVec2(bar_pos.x + bar_width * percentage, bar_pos.y + bar_height),
                    ImGui::GetColorU32(bar_color));
                
                // Count and percentage text
                ImGui::Dummy(ImVec2(bar_width, bar_height));
                ImGui::SameLine();
                ImGui::Text("%d (%.0f%%)", count, static_cast<double>(percentage * 100.0f));
                
                ImGui::Spacing();
            }
        }
        ImGui::EndChild();
        
        // Quick actions
        ImGui::TextColored(colors[0], "Quick Filters");
        
        // Create buttons for each issue type
        int button_count = 0;
        for (const auto& [type_name, count] : sorted_types) {
            if (button_count > 0 && button_count % 3 != 0) {
                ImGui::SameLine();
            }
            
            if (ImGui::Button(std::format("{} ({})", type_name, count).c_str())) {
                // Open JIRA search card with a filter for this issue type
                std::string jql = std::format("project = {} AND issuetype = \"{}\" ORDER BY updated DESC", 
                                            selected_project_key_, type_name);
                
                // Use the registrar to open the JIRA search card with this JQL
                "jira_search_query"_sfn(jql);
                "create_card"_sfn("jira-search");
                grab_focus = false;  // Give focus to the new card
            }
            
            button_count++;
            if (button_count % 3 == 0) {
                ImGui::Spacing();
            }
        }
    }
    
    void render_issues_by_assignee(const project_stats& stats) {
        // Sort assignees by count
        std::vector<std::pair<std::string, int>> sorted_assignees(
            stats.issues_by_assignee.begin(), stats.issues_by_assignee.end());
        std::sort(sorted_assignees.begin(), sorted_assignees.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // Calculate total for percentage
        int total = 0;
        for (const auto& [_, count] : sorted_assignees) {
            total += count;
        }
        
        // Bar chart
        if (ImGui::BeginChild("AssigneesChart", ImVec2(0, 200), true)) {
            const float bar_height = 24.0f;
            const float chart_width = ImGui::GetContentRegionAvail().x;
            const float text_width = 150.0f;
            const float bar_width = chart_width - text_width - 50.0f;
            
            for (const auto& [assignee_name, count] : sorted_assignees) {
                // Assignee name
                ImGui::TextColored(colors[3], "%s", assignee_name.c_str());
                ImGui::SameLine(text_width);
                
                // Bar
                float percentage = total > 0 ? static_cast<float>(count) / static_cast<float>(total) : 0.0f;
                ImVec2 bar_pos = ImGui::GetCursorScreenPos();
                
                // Background bar
                ImGui::GetWindowDrawList()->AddRectFilled(
                    bar_pos, 
                    ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_height),
                    ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.5f)));
                
                // Filled portion of bar - use a gradient based on number of issues
                float hue = 0.33f; // Green hue
                if (sorted_assignees.size() > 1) {
                    // Find max count for scaling
                    int max_count = sorted_assignees[0].second;
                    // Normalize the current count to a value between 0.0 and 0.33 (red to green)
                    hue = 0.33f * static_cast<float>(count) / static_cast<float>(max_count);
                }
                
                ImVec4 bar_color;
                ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 0.8f, 
                                           bar_color.x, bar_color.y, bar_color.z);
                bar_color.w = 1.0f;
                
                ImGui::GetWindowDrawList()->AddRectFilled(
                    bar_pos, 
                    ImVec2(bar_pos.x + bar_width * percentage, bar_pos.y + bar_height),
                    ImGui::GetColorU32(bar_color));
                
                // Count and percentage text
                ImGui::Dummy(ImVec2(bar_width, bar_height));
                ImGui::SameLine();
                ImGui::Text("%d (%.0f%%)", count, static_cast<double>(percentage * 100.0f));
                
                ImGui::Spacing();
            }
        }
        ImGui::EndChild();
        
        // Quick actions
        ImGui::TextColored(colors[0], "Quick Filters");
        
        // Create buttons for top assignees
        int button_count = 0;
        for (size_t i = 0; i < std::min(sorted_assignees.size(), static_cast<size_t>(5)); ++i) {
            const auto& [assignee_name, count] = sorted_assignees[i];
            
            if (button_count > 0 && button_count % 2 != 0) {
                ImGui::SameLine();
            }
            
            if (ImGui::Button(std::format("{} ({})", assignee_name, count).c_str())) {
                // Open JIRA search card with a filter for this assignee
                std::string jql;
                if (assignee_name == "Unassigned") {
                    jql = std::format("project = {} AND assignee is EMPTY ORDER BY updated DESC", 
                                    selected_project_key_);
                } else {
                    jql = std::format("project = {} AND assignee = \"{}\" ORDER BY updated DESC", 
                                     selected_project_key_, assignee_name);
                }
                
                // Use the registrar to open the JIRA search card with this JQL
                "jira_search_query"_sfn(jql);
                "create_card"_sfn("jira-search");
                grab_focus = false;  // Give focus to the new card
            }
            
            button_count++;
            if (button_count % 2 == 0) {
                ImGui::Spacing();
            }
        }
    }
    
    void render_issues_list() {
        if (!project_issues_.has_value() || project_issues_->issues.empty()) {
            ImGui::TextColored(colors[3], "No issues found for this project.");
            return;
        }
        
        // Filter options
        static int status_filter = 0; // 0 = All, 1 = Open, 2 = In Progress, 3 = Done
        ImGui::TextColored(colors[0], "Filter by Status:");
        ImGui::SameLine();
        ImGui::RadioButton("All", &status_filter, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Open", &status_filter, 1);
        ImGui::SameLine();
        ImGui::RadioButton("In Progress", &status_filter, 2);
        ImGui::SameLine();
        ImGui::RadioButton("Done", &status_filter, 3);
        
        ImGui::Separator();
        
        // Issues table
        if (ImGui::BeginTable("IssuesTable", 4, 
                             ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                             ImVec2(0, 300))) {
            
            // Header row
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Assignee", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            
            for (const auto& issue : project_issues_->issues) {
                // Apply status filter
                if (status_filter == 1 && issue.status.category != "To Do") continue;
                if (status_filter == 2 && issue.status.category != "In Progress") continue;
                if (status_filter == 3 && issue.status.category != "Done") continue;
                
                ImGui::TableNextRow();
                
                // Key column
                ImGui::TableNextColumn();
                if (ImGui::Selectable(issue.key.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    // Open in main JIRA card
                    "jira_open_issue"_sfn(issue.key);
                    "create_card"_sfn("jira");
                    grab_focus = false;  // Give focus to the new card
                }
                
                // Summary column
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", issue.summary.c_str());
                
                // Status column with color
                ImGui::TableNextColumn();
                ImVec4 status_color = colors[3]; // Default gray
                
                if (issue.status.category == "To Do") {
                    status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
                } else if (issue.status.category == "In Progress") {
                    status_color = ImVec4(0.0f, 0.5f, 0.9f, 1.0f); // Blue
                } else if (issue.status.category == "Done") {
                    status_color = ImVec4(0.0f, 0.7f, 0.2f, 1.0f); // Green
                }
                
                ImGui::TextColored(status_color, "%s", issue.status.name.c_str());
                
                // Assignee column
                ImGui::TableNextColumn();
                if (issue.assignee.display_name.empty()) {
                    ImGui::TextColored(colors[3], "Unassigned");
                } else {
                    ImGui::Text("%s", issue.assignee.display_name.c_str());
                }
            }
            
            ImGui::EndTable();
        }
        
        ImGui::Separator();
        
        // Quick actions
        if (ImGui::Button("Create Issue", ImVec2(120, 0))) {
            // Open the create issue tab in the main JIRA card
            "jira_create_issue"_sfn(selected_project_key_);
            "create_card"_sfn("jira");
            grab_focus = false;  // Give focus to the new card
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Advanced Search", ImVec2(120, 0))) {
            // Open JIRA search card for this project
            std::string jql = std::format("project = {} ORDER BY updated DESC", selected_project_key_);
            
            // Use the registrar to open the JIRA search card with this JQL
            "jira_search_query"_sfn(jql);
            "create_card"_sfn("jira-search");
            grab_focus = false;  // Give focus to the new card
        }
    }
};

} // namespace rouen::cards