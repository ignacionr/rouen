#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <chrono>
#include <format>
#include <algorithm>
#include <expected>
#include <array>
#include <cstring>

#include <rouen_plugin_api.hpp>
#include "helpers/imgui_include.hpp"
#include "jira_model.hpp"
#include "helpers/views/json_view.hpp"
#include "helpers/api_keys.hpp"
#include "helpers/debug.hpp"
#include "IconsMaterialDesign.h"
#include "jira_ui_components.hpp"
#include "jira_connection.hpp"
#include "jira_issues.hpp"
#include "jira_create_issue.hpp"

namespace rouen::cards {

class jira_card : public rouen::plugin::plugin_card {
public:
    using color_array = std::array<ImVec4, 16>;

    explicit jira_card(std::string_view locator = "") {
        colors[0] = {0.0f, 0.4f, 0.8f, 1.0f};
        colors[1] = {0.1f, 0.5f, 0.9f, 0.7f};
        colors[2] = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
        colors[3] = ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
        colors[4] = ImVec4(0.9f, 0.7f, 0.0f, 1.0f);
        colors[5] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        colors[6] = ImVec4(1.0f, 1.0f, 1.0f, 0.8f);
        colors[7] = ImVec4(0.7f, 0.1f, 0.1f, 1.0f);
        colors[8] = ImVec4(0.7f, 0.7f, 0.1f, 1.0f);
        colors[9] = ImVec4(0.1f, 0.6f, 0.1f, 1.0f);
        
        handle_uri(locator);
        
        conn_handler_ = std::make_unique<jira_connection_handler>();
        auto jira_model = conn_handler_->get_jira_host();
        issues_handler_ = std::make_unique<jira_issues_handler>(jira_model);
        create_issue_handler_ = std::make_unique<jira_create_issue_handler>(jira_model);
    }
    
    void draw() override {
        jira_ui::poll_async_tasks();
        if (!conn_handler_->is_connected()) {
            conn_handler_->render_connection_screen();
        } else {
            render_main_interface();
        }
    }

    [[nodiscard]] std::string title() const override {
        return "Jira";
    }

    [[nodiscard]] std::string uri() const override {
        return locator_.empty() ? "jira" : "jira:" + locator_;
    }

    void handle_uri(std::string_view locator) override {
        locator_ = std::string(locator);
    }

    void render_main_interface() {
        if (ImGui::BeginTabBar("JiraTabs")) {
            render_tab("Projects", [this]{ render_projects_tab(); });
            render_tab("My Issues", [this]{ render_my_issues_tab(); });
            render_tab("Search", [this]{ render_search_tab(); });
            render_tab("Create Issue", [this]{ render_create_issue_tab(); });
            
            ImGui::EndTabBar();
        }
        
        ImGui::Separator();
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 100);
        if (ImGui::Button("Logout", ImVec2(100, 0))) {
            conn_handler_->disconnect();
        }
    }
    
    template <typename Func>
    void render_tab(const char* label, Func content_renderer) {
        if (ImGui::BeginTabItem(label)) {
            content_renderer();
            ImGui::EndTabItem();
        }
    }

private:
    void render_projects_tab() {
        if (projects_.empty() && !is_loading_projects_) {
            fetch_projects();
        }
        
        render_projects_header();
        
        if (is_loading_projects_) {
            ImGui::TextColored(colors[1], "Loading projects...");
            return;
        }
        
        if (projects_.empty()) {
            ImGui::TextColored(colors[5], "No projects found");
            return;
        }
        
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##project_filter", "Filter projects...", project_filter_, sizeof(project_filter_));
        ImGui::PopItemWidth();
        
        if (ImGui::BeginTable("ProjectsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            jira_ui::TableRenderers::setup_project_table_headers();
            
            jira_ui::render_filterable_table(
                projects_,
                project_filter_,
                colors,
                [this](const models::jira_project& project) {
                    selected_project_ = project;
                    issues_handler_->set_selected_project(project);
                    issues_handler_->load_project_issues(project.key);
                }
            );
            
            ImGui::EndTable();
        }
        
        issues_handler_->render_project_issues(colors);
    }

    void render_projects_header() {
        if (ImGui::Button("Refresh Projects")) {
            fetch_projects();
        }
        ImGui::Separator();
    }

    void fetch_projects() {
        is_loading_projects_ = true;
        auto future = conn_handler_->get_jira_host()->get_projects();
        
        jira_ui::execute_async<std::vector<models::jira_project>>(
            std::move(future),
            [this](const std::vector<models::jira_project>& result) {
                projects_ = result;
            },
            [](const std::string& error) {
                JIRA_ERROR_FMT("Error fetching projects: {}", error);
            },
            nullptr,
            &is_loading_projects_
        );
    }
    
    void render_my_issues_tab() {
        issues_handler_->render_my_issues_tab(colors);
    }
    
    void render_search_tab() {
        ImGui::TextColored(colors[0], "Search Issues with JQL");
        ImGui::TextWrapped("JQL (Jira Query Language) lets you search for issues with complex criteria");
        
        ImGui::PushItemWidth(-120);
        ImGui::InputTextWithHint("##jql_query", "Enter JQL query...", jql_query_, sizeof(jql_query_));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Search", ImVec2(100, 0))) {
            perform_jql_search();
        }
        
        if (is_searching_) {
            ImGui::TextColored(colors[1], "Searching...");
        } else if (!search_error_.empty()) {
            ImGui::TextColored(colors[2], "%s", search_error_.c_str());
        }
        
        if (!search_results_.issues.empty()) {
            ImGui::Separator();
            ImGui::TextColored(colors[0], "Search Results: %d issue(s) found", search_results_.total);
            
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
                    [this](const models::jira_issue& issue) {
                        jira_ui::TableRenderers::render_project_column(issue);
                        jira_ui::TableRenderers::render_status_column(issue, colors);
                    }
                );
                
                ImGui::EndTable();
            }
        }
        
        if (show_issue_details_) {
            issues_handler_->render_issue_details_popup(colors);
        }
    }

    void perform_jql_search() {
        if (std::string(jql_query_).empty()) return;
        
        is_searching_ = true;
        search_error_.clear();
        auto future = conn_handler_->get_jira_host()->search_issues(jql_query_);
        
        jira_ui::execute_async<models::jira_search_result>(
            std::move(future),
            [this](const models::jira_search_result& result) {
                search_results_ = result;
            },
            [this](const std::string& error) {
                search_error_ = error;
            },
            nullptr,
            &is_searching_
        );
    }
    
    void render_create_issue_tab() {
        create_issue_handler_->render_create_issue_tab(colors, projects_);
    }

    color_array colors;
    std::string locator_;
    std::unique_ptr<jira_connection_handler> conn_handler_;
    std::unique_ptr<jira_issues_handler> issues_handler_;
    std::unique_ptr<jira_create_issue_handler> create_issue_handler_;
    
    std::vector<models::jira_project> projects_;
    bool is_loading_projects_ = false;
    char project_filter_[256] = "";
    models::jira_project selected_project_;
    
    char jql_query_[1024] = "";
    models::jira_search_result search_results_;
    bool is_searching_ = false;
    std::string search_error_;
    models::jira_issue selected_issue_;
    bool show_issue_details_ = false;
};

} // namespace rouen::cards
