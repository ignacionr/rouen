#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <format>
#include <array>
#include <cstring>

#include "helpers/imgui_include.hpp"
#include "jira_model.hpp"
#include "helpers/debug.hpp"
#include "jira_ui_components.hpp"

namespace rouen::cards {

class jira_search_handler {
public:
    jira_search_handler(std::shared_ptr<models::jira_model> jira_host)
        : jira_host_(jira_host) {
    }
    
    void render_search_tab(const ImVec4* colors) {
        ImGui::TextColored(colors[0], "Search Issues with JQL");
        ImGui::TextWrapped("JQL (Jira Query Language) lets you search for issues with complex criteria");
        
        render_search_input();
        
        if (ImGui::TreeNode("Example Queries")) {
            render_example_jql_queries();
            ImGui::TreePop();
        }
        
        if (is_loading_search_results_) {
            ImGui::TextColored(colors[1], "Searching...");
        } else if (!search_error_.empty()) {
            ImGui::TextColored(colors[2], "%s", search_error_.c_str());
        }
        
        render_search_results(colors);
        render_issue_details_popup(colors);
    }
    
private:
    void render_search_input() {
        ImGui::PushItemWidth(-120);
        ImGui::InputTextWithHint("##jql_query", "Enter JQL query... (e.g., project = KEY AND status = \"In Progress\")", 
                              jql_query_, sizeof(jql_query_));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        
        if (ImGui::Button("Search", ImVec2(100, 0))) {
            perform_jql_search();
        }
    }
    
    void render_search_results(const ImVec4* colors) {
        if (search_results_.issues.empty()) {
            return;
        }
        
        ImGui::Separator();
        ImGui::TextColored(colors[0], "Search Results: %d issue(s) found", search_results_.total);
        
        if (search_results_.total > static_cast<int>(search_results_.issues.size())) {
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "(showing %zu of %d)", 
                            search_results_.issues.size(), search_results_.total);
            
            if (ImGui::Button("Load More")) {
                load_more_search_results();
            }
        }
        
        if (ImGui::BeginTable("SearchResultsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            
            jira_ui::render_filterable_table(
                search_results_.issues,
                "",
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
    }
    
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
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", query.c_str());
            ImGui::Separator();
        }
    }
    
    void render_issue_details_popup(const ImVec4* colors) {
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
    
    void render_issue_basic_info(const ImVec4* colors) {
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
    
    void render_issue_description(const ImVec4* colors) {
        ImGui::TextColored(colors[0], "Description:");
        ImGui::BeginChild("Description", ImVec2(0, 200), true);
        if (!selected_issue_.description.empty()) {
            ImGui::TextWrapped("%s", selected_issue_.description.c_str());
        } else {
            ImGui::TextColored(colors[5], "No description provided");
        }
        ImGui::EndChild();
    }
    
    void render_issue_transitions(const ImVec4* colors) {
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
    
    void perform_jql_search() {
        if (std::string(jql_query_).empty()) {
            search_error_ = "Please enter a JQL query";
            return;
        }
        
        search_error_.clear();
        is_loading_search_results_ = true;
        current_search_start_at_ = 0;
        
        auto future = jira_host_->search_issues(jql_query_, current_search_start_at_);
        
        jira_ui::execute_async<models::jira_search_result>(
            std::move(future),
            [this](const models::jira_search_result& result) {
                search_results_ = result;
            },
            [this](const std::string& error) {
                search_error_ = std::format("Search error: {}", error);
                JIRA_ERROR_FMT("JQL search error: {}", error);
            },
            nullptr,
            &is_loading_search_results_
        );
    }
    
    void load_more_search_results() {
        if (is_loading_search_results_) {
            return;
        }
        
        is_loading_search_results_ = true;
        current_search_start_at_ += search_results_.max_results;
        
        auto future = jira_host_->search_issues(jql_query_, current_search_start_at_);
        
        jira_ui::execute_async<models::jira_search_result>(
            std::move(future),
            [this](const models::jira_search_result& result) {
                search_results_.issues.insert(
                    search_results_.issues.end(), 
                    result.issues.begin(), 
                    result.issues.end()
                );
            },
            [this](const std::string& error) {
                search_error_ = std::format("Error loading more results: {}", error);
                JIRA_ERROR_FMT("Error loading more search results: {}", error);
            },
            nullptr,
            &is_loading_search_results_
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
                search_error_ = std::format("Error loading transitions: {}", error);
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
                    
                    if (!std::string(jql_query_).empty()) {
                        perform_jql_search();
                    }
                },
                [this](const std::string& error) {
                    search_error_ = std::format("Error refreshing issue: {}", error);
                    JIRA_ERROR_FMT("Error refreshing issue after transition: {}", error);
                }
            );
        } else {
            search_error_ = "Failed to transition issue";
            JIRA_ERROR("Failed to transition issue");
        }
    }
    
    std::string format_jira_date(const std::string& jira_date) {
        if (jira_date.empty()) {
            return "";
        }
        return jira_date.size() >= 10 ? jira_date.substr(0, 10) : jira_date;
    }

    std::shared_ptr<models::jira_model> jira_host_;
    
    char jql_query_[1024] = "";
    models::jira_search_result search_results_;
    bool is_loading_search_results_ = false;
    std::string search_error_;
    int current_search_start_at_ = 0;
    
    models::jira_issue selected_issue_;
    bool show_issue_details_ = false;
    std::vector<models::jira_transition> issue_transitions_;
    bool is_loading_transitions_ = false;
};

} // namespace rouen::cards
