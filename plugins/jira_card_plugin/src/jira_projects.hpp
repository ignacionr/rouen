#pragma once

#include <rouen_plugin_api.hpp>
#include "jira_model.hpp"
#include "helpers/imgui_include.hpp"
#include "helpers/platform_utils.hpp"
#include "registrar.hpp"
#include "jira_ui_components.hpp"
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <format>
#include <array>
#include <thread>

namespace rouen::cards {

class jira_projects_card : public rouen::plugin::plugin_card {
public:
    using color_array = std::array<ImVec4, 16>;

    jira_projects_card() {
        colors[0] = ImVec4{0.0f, 0.48f, 0.8f, 1.0f};
        colors[1] = ImVec4{0.1f, 0.58f, 0.9f, 0.7f};
        colors[2] = ImVec4(0.0f, 0.67f, 1.0f, 1.0f);
        colors[3] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        colors[4] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        colors[5] = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
        colors[6] = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);
        
        jira_model_ = std::make_shared<models::jira_model>();
        connection_profiles_ = models::jira_model::load_profiles();
        
        if (!connection_profiles_.empty()) {
            std::thread([this, profile = connection_profiles_[0]]() {
                try {
                    jira_model_->connect(profile);
                    auto future_ptr = std::make_shared<std::future<std::vector<models::jira_project>>>(jira_model_->get_projects());
                    jira_ui::post_main_thread_task([this, profile, future_ptr]() mutable {
                        status_message_ = std::format("Connected to {} automatically", profile.name);
                        if (future_ptr && future_ptr->valid()) {
                            projects_future_ = std::move(*future_ptr);
                        }
                    });
                } catch (const std::exception& e) {
                    std::string err = e.what();
                    jira_ui::post_main_thread_task([this, err]() {
                        status_message_ = std::format("Failed to connect: {}", err);
                        JIRA_ERROR_FMT("Connection error: {}", err);
                    });
                }
            }).detach();
        }
    }

    void draw() override {
        jira_ui::poll_async_tasks();
        if (!jira_model_->is_connected()) {
            render_connection_prompt();
            return;
        }
        
        check_async_operations();
        
        if (projects_.empty()) {
            ImGui::TextColored(colors[3], "Loading projects...");
            return;
        }
        
        if (ImGui::BeginCombo("Select Project", selected_project_name_.c_str())) {
            for (const auto& project : projects_) {
                if (ImGui::Selectable(std::format("{}  -  {}", project.key, project.name).c_str(), 
                                     project.key == selected_project_key_)) {
                    selected_project_key_ = project.key;
                    selected_project_name_ = project.name;
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
    }

    [[nodiscard]] std::string title() const override {
        return "Jira Projects";
    }

    [[nodiscard]] std::string uri() const override {
        return "jira-projects";
    }
    
private:
    std::shared_ptr<models::jira_model> jira_model_;
    color_array colors;
    
    std::vector<models::jira_connection_profile> connection_profiles_;
    std::string status_message_;
    
    std::future<std::vector<models::jira_project>> projects_future_;
    std::vector<models::jira_project> projects_;
    
    std::string selected_project_key_;
    std::string selected_project_name_;
    
    std::future<std::vector<models::jira_issue>> project_issues_future_;
    std::vector<models::jira_issue> project_issues_;
    
    std::unordered_map<std::string, int> status_counts_;
    std::unordered_map<std::string, int> type_counts_;
    std::unordered_map<std::string, int> assignee_counts_;
    
    void check_async_operations() {
        if (projects_future_.valid() && projects_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                projects_ = projects_future_.get();
                if (!projects_.empty() && selected_project_key_.empty()) {
                    selected_project_key_ = projects_[0].key;
                    selected_project_name_ = projects_[0].name;
                    fetch_project_details();
                }
            } catch (const std::exception& e) {
                status_message_ = std::format("Error fetching projects: {}", e.what());
            }
        }
        
        if (project_issues_future_.valid() && project_issues_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                project_issues_ = project_issues_future_.get();
                calculate_statistics();
            } catch (const std::exception& e) {
                status_message_ = std::format("Error fetching issues: {}", e.what());
            }
        }
    }
    
    void fetch_project_details() {
        if (!selected_project_key_.empty()) {
            project_issues_future_ = jira_model_->get_issues(selected_project_key_, 100);
        }
    }
    
    void calculate_statistics() {
        status_counts_.clear();
        type_counts_.clear();
        assignee_counts_.clear();
        
        for (const auto& issue : project_issues_) {
            status_counts_[issue.status.name]++;
            type_counts_[issue.issue_type.name]++;
            
            std::string assignee_name = issue.assignee.display_name.empty() ? "Unassigned" : issue.assignee.display_name;
            assignee_counts_[assignee_name]++;
        }
    }
    
    void render_connection_prompt() {
        ImGui::TextColored(colors[6], "Not connected to JIRA");
        ImGui::Separator();
        
        if (!connection_profiles_.empty()) {
            ImGui::Text("Available profiles:");
            for (size_t i = 0; i < connection_profiles_.size(); ++i) {
                if (ImGui::Button(std::format("Connect to {}", connection_profiles_[i].name).c_str())) {
                    std::thread([this, profile = connection_profiles_[i]]() {
                        try {
                            jira_model_->connect(profile);
                            auto future_ptr = std::make_shared<std::future<std::vector<models::jira_project>>>(jira_model_->get_projects());
                            jira_ui::post_main_thread_task([this, profile, future_ptr]() mutable {
                                status_message_ = std::format("Connected to {}", profile.name);
                                if (future_ptr && future_ptr->valid()) {
                                    projects_future_ = std::move(*future_ptr);
                                }
                            });
                        } catch (const std::exception& e) {
                            std::string err = e.what();
                            jira_ui::post_main_thread_task([this, err]() {
                                status_message_ = std::format("Failed to connect: {}", err);
                            });
                        }
                    }).detach();
                }
            }
        } else {
            ImGui::Text("No JIRA connection profiles found.");
            if (ImGui::Button("Open JIRA Card")) {
                create_card_uri("jira");
            }
        }
        
        if (!status_message_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", status_message_.c_str());
        }
    }
    
    void render_projects_overview() {
        ImGui::TextColored(colors[0], "Projects Overview (%zu total)", projects_.size());
        ImGui::Separator();
        
        if (ImGui::BeginTable("ProjectsOverviewTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();
            
            for (const auto& project : projects_) {
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[2], "%s", project.key.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", project.name.c_str());
                
                ImGui::TableNextColumn();
                if (ImGui::Button(std::format("View##{}", project.key).c_str())) {
                    selected_project_key_ = project.key;
                    selected_project_name_ = project.name;
                    fetch_project_details();
                }
            }
            
            ImGui::EndTable();
        }
    }
    
    void render_project_details() {
        ImGui::TextColored(colors[0], "%s - %s", selected_project_key_.c_str(), selected_project_name_.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Back to Overview")) {
            selected_project_key_.clear();
            selected_project_name_.clear();
            project_issues_.clear();
            return;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Open JIRA Board")) {
            create_card_uri("jira");
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginTabBar("ProjectDetailsTabs")) {
            if (ImGui::BeginTabItem("Summary")) {
                render_project_summary();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Issues")) {
                render_project_issues_list();
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    
    void render_project_summary() {
        ImGui::Text("Total Issues: %zu", project_issues_.size());
        ImGui::Separator();
        
        if (ImGui::CollapsingHeader("Issues by Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& [status, count] : status_counts_) {
                float percentage = project_issues_.empty() ? 0.0f : (static_cast<float>(count) / static_cast<float>(project_issues_.size()));
                ImGui::Text("%s: %d (%.1f%%)", status.c_str(), count, percentage * 100.0f);
                ImGui::ProgressBar(percentage, ImVec2(-1, 0));
            }
        }
        
        if (ImGui::CollapsingHeader("Issues by Type", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& [type, count] : type_counts_) {
                float percentage = project_issues_.empty() ? 0.0f : (static_cast<float>(count) / static_cast<float>(project_issues_.size()));
                ImGui::Text("%s: %d (%.1f%%)", type.c_str(), count, percentage * 100.0f);
            }
        }
    }
    
    void render_project_issues_list() {
        if (ImGui::BeginTable("ProjectIssuesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Assignee", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();
            
            for (const auto& issue : project_issues_) {
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                ImGui::TextColored(colors[2], "%s", issue.key.c_str());
                
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", issue.summary.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", issue.status.name.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", issue.assignee.display_name.empty() ? "Unassigned" : issue.assignee.display_name.c_str());
            }
            
            ImGui::EndTable();
        }
    }
};

} // namespace rouen::cards
