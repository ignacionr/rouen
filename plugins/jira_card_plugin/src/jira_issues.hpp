#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <format>
#include <array>

#include "helpers/imgui_include.hpp"
#include "jira_model.hpp"
#include "helpers/debug.hpp"
#include "jira_ui_components.hpp"

namespace rouen::cards {

class jira_issues_handler {
public:
    using color_array = std::array<ImVec4, 16>;

    jira_issues_handler(std::shared_ptr<models::jira_model> jira_host)
        : jira_host_(jira_host) {
    }
    
    void render_my_issues_tab(const color_array& colors) {
        render_my_issues_header();
        
        render_loading_or_error(is_loading_my_issues_, error_message_, my_issues_.empty(),
                             "Loading issues...", "No issues assigned to you found.", colors);
        
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##my_issue_filter", "Filter issues...", my_issue_filter_, sizeof(my_issue_filter_));
        ImGui::PopItemWidth();
        
        if (ImGui::BeginTable("MyIssuesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            
            jira_ui::render_filterable_table(
                my_issues_,
                my_issue_filter_,
                colors,
                [this](const models::jira_issue& issue) {
                    selected_issue_ = issue;
                    show_issue_details_ = true;
                },
                [colors](const models::jira_issue& issue) {
                    jira_ui::TableRenderers::render_project_column(issue);
                    jira_ui::TableRenderers::render_status_column(issue, colors);
                }
            );
            
            ImGui::EndTable();
        }
        
        render_issue_details_popup(colors);
    }
    
    void render_project_issues(const color_array& colors) {
        if (selected_project_.key.empty()) {
            return;
        }
        
        ImGui::Separator();
        ImGui::TextColored(colors[0], "Issues for %s: %s", 
                          selected_project_.key.c_str(), 
                          selected_project_.name.c_str());
        
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##issue_filter", "Filter issues...", issue_filter_, sizeof(issue_filter_));
        ImGui::PopItemWidth();
        
        if (is_loading_issues_) {
            ImGui::TextColored(colors[1], "Loading issues...");
        }
        
        if (ImGui::BeginTable("IssuesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            jira_ui::TableRenderers::setup_issue_table_headers();
            
            jira_ui::render_filterable_table(
                project_issues_,
                issue_filter_,
                colors,
                [this](const models::jira_issue& issue) {
                    selected_issue_ = issue;
                    show_issue_details_ = true;
                },
                [colors](const models::jira_issue& issue) {
                    jira_ui::TableRenderers::render_status_column(issue, colors);
                }
            );
            
            ImGui::EndTable();
        }
    }
    
    void render_issue_details_popup(const color_array& colors) {
        if (!show_issue_details_ || selected_issue_.key.empty()) {
            return;
        }
        
        ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
        ImGui::OpenPopup(std::format("Issue: {}", selected_issue_.key).c_str());
        
        if (ImGui::BeginPopupModal(std::format("Issue: {}", selected_issue_.key).c_str(), &show_issue_details_)) {
            render_issue_basic_info(colors);
            render_issue_description(colors);
            render_issue_transitions(colors);
            
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                show_issue_details_ = false;
                issue_transitions_.clear();
            }
            
            ImGui::EndPopup();
        }
    }
    
    void load_project_issues(const std::string& project_key) {
        is_loading_issues_ = true;
        
        std::string jql = std::format("project = {} ORDER BY updated DESC", project_key);
        auto future = jira_host_->search_issues(jql);
        
        jira_ui::execute_async<models::jira_search_result>(
            std::move(future),
            [this](const models::jira_search_result& result) {
                project_issues_ = result.issues;
            },
            [this](const std::string& error) {
                error_message_ = std::format("Failed to load issues: {}", error);
            },
            nullptr,
            &is_loading_issues_
        );
    }
    
    void refresh_my_issues() {
        is_loading_my_issues_ = true;
        error_message_.clear();
        
        std::string jql = "assignee = currentUser()";
        if (status_filter_ != "All") {
            jql += std::format(" AND statusCategory = \"{}\"", status_filter_);
        }
        jql += " ORDER BY updated DESC";
        
        auto future = jira_host_->search_issues(jql);
        
        jira_ui::execute_async<models::jira_search_result>(
            std::move(future),
            [this](const models::jira_search_result& result) {
                my_issues_ = result.issues;
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error loading your issues: {}", error);
                JIRA_ERROR_FMT("Error loading user issues: {}", error);
            },
            nullptr,
            &is_loading_my_issues_
        );
    }
    
    void load_issue_transitions(const std::string& issue_key) {
        is_loading_transitions_ = true;
        
        auto future = jira_host_->get_transitions(issue_key);
        
        jira_ui::execute_async<std::vector<models::jira_transition>>(
            std::move(future),
            [this](const std::vector<models::jira_transition>& result) {
                issue_transitions_ = result;
            },
            [this](const std::string& error) {
                error_message_ = std::format("Error loading transitions: {}", error);
                JIRA_ERROR_FMT("Error loading issue transitions: {}", error);
            },
            nullptr,
            &is_loading_transitions_
        );
    }
    
    void transition_issue(const std::string& issue_key, const std::string& transition_id) {
        if (jira_host_->transition_issue(issue_key, transition_id)) {
            auto future = jira_host_->get_issue(issue_key);
            
            jira_ui::execute_async<models::jira_issue>(
                std::move(future),
                [this, issue_key](const models::jira_issue& result) {
                    selected_issue_ = result;
                    issue_transitions_.clear();
                    load_issue_transitions(issue_key);
                    refresh_my_issues();
                    if (!selected_project_.key.empty()) {
                        load_project_issues(selected_project_.key);
                    }
                },
                [this](const std::string& error) {
                    error_message_ = std::format("Error refreshing issue: {}", error);
                    JIRA_ERROR_FMT("Error refreshing issue after transition: {}", error);
                }
            );
        } else {
            error_message_ = "Failed to transition issue";
            JIRA_ERROR("Failed to transition issue");
        }
    }
    
    void set_selected_project(const models::jira_project& project) {
        selected_project_ = project;
    }
    
    bool is_showing_issue_details() const { return show_issue_details_; }
    const models::jira_issue& get_selected_issue() const { return selected_issue_; }
    
private:
    void render_my_issues_header() {
        if (ImGui::Button("Refresh My Issues")) {
            refresh_my_issues();
        }
        
        ImGui::SameLine();
        
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("Status Filter", status_filter_.c_str())) {
            render_status_filter_options();
            ImGui::EndCombo();
        }
    }
    
    void render_status_filter_options() {
        const std::array<std::string, 4> status_options = {"All", "To Do", "In Progress", "Done"};
        
        for (const auto& option : status_options) {
            if (ImGui::Selectable(option.c_str(), status_filter_ == option)) {
                status_filter_ = option;
                refresh_my_issues();
            }
        }
    }
    
    void render_issue_basic_info(const color_array& colors) {
        ImGui::TextColored(colors[0], "%s: %s", selected_issue_.key.c_str(), selected_issue_.summary.c_str());
        
        ImVec4 status_color = colors[5];
        if (selected_issue_.status.category == "To Do") {
            status_color = colors[5];
        } else if (selected_issue_.status.category == "In Progress") {
            status_color = colors[8];
        } else if (selected_issue_.status.category == "Done") {
            status_color = colors[9];
        }
        ImGui::TextColored(status_color, "Status: %s", selected_issue_.status.name.c_str());
        
        ImGui::Text("Type: %s", selected_issue_.issue_type.name.c_str());
        
        if (!selected_issue_.assignee.display_name.empty()) {
            ImGui::Text("Assignee: %s", selected_issue_.assignee.display_name.c_str());
        } else {
            ImGui::TextColored(colors[5], "Assignee: Unassigned");
        }
        
        ImGui::Text("Created: %s", format_jira_date(selected_issue_.created).c_str());
        ImGui::Text("Updated: %s", format_jira_date(selected_issue_.updated).c_str());
        
        ImGui::Separator();
    }
    
    void render_issue_description(const color_array& colors) {
        ImGui::TextColored(colors[0], "Description:");
        ImGui::BeginChild("Description", ImVec2(0, 200), true);
        if (!selected_issue_.description.empty()) {
            ImGui::TextWrapped("%s", selected_issue_.description.c_str());
        } else {
            ImGui::TextColored(colors[5], "No description provided");
        }
        ImGui::EndChild();
    }
    
    void render_issue_transitions(const color_array& colors) {
        ImGui::Separator();
        ImGui::TextColored(colors[0], "Transitions:");
        
        if (issue_transitions_.empty() && !is_loading_transitions_) {
            load_issue_transitions(selected_issue_.key);
        }
        
        if (is_loading_transitions_) {
            ImGui::TextColored(colors[1], "Loading available transitions...");
        } else if (issue_transitions_.empty()) {
            ImGui::TextColored(colors[5], "No transitions available");
        } else {
            for (size_t i = 0; i < issue_transitions_.size(); i++) {
                const auto& transition = issue_transitions_[i];
                
                if (ImGui::Button(transition.name.c_str(), ImVec2(150, 0))) {
                    transition_issue(selected_issue_.key, transition.id);
                }
                ImGui::SameLine();
                
                ImGui::TextColored(colors[5], "-> %s", transition.to_status.name.c_str());
                
                bool is_last = (i == issue_transitions_.size() - 1);
                bool wrap_line = (!is_last && (i + 1) % 2 == 0);
                
                if (wrap_line) {
                    ImGui::NewLine();
                } else if (!is_last) {
                    ImGui::SameLine();
                }
            }
        }
        
        ImGui::NewLine();
    }
    
    std::string format_jira_date(const std::string& jira_date) {
        if (jira_date.empty()) {
            return "";
        }
        return jira_date.size() >= 10 ? jira_date.substr(0, 10) : jira_date;
    }
    
    void render_loading_or_error(bool is_loading, const std::string& error, bool is_empty,
                              const char* loading_msg, const char* empty_msg, const color_array& colors) {
        if (is_loading) {
            ImGui::TextColored(colors[1], "%s", loading_msg);
        } else if (!error.empty()) {
            ImGui::TextColored(colors[2], "%s", error.c_str());
        } else if (is_empty) {
            ImGui::TextColored(colors[5], "%s", empty_msg);
        }
    }

    std::shared_ptr<models::jira_model> jira_host_;
    std::string error_message_;
    
    std::vector<models::jira_issue> my_issues_;
    bool is_loading_my_issues_ = false;
    char my_issue_filter_[256] = "";
    std::string status_filter_ = "All";
    
    std::vector<models::jira_issue> project_issues_;
    bool is_loading_issues_ = false;
    char issue_filter_[256] = "";
    models::jira_project selected_project_;
    
    models::jira_issue selected_issue_;
    bool show_issue_details_ = false;
    std::vector<models::jira_transition> issue_transitions_;
    bool is_loading_transitions_ = false;
};

} // namespace rouen::cards
