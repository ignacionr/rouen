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
#include <chrono>
#include <format>
#include <cstring>
#include <array>
#include <thread>

namespace rouen::cards {

class jira_search_card : public rouen::plugin::plugin_card {
public:
    using color_array = std::array<ImVec4, 16>;

    jira_search_card() {
        colors[0] = ImVec4{0.0f, 0.48f, 0.8f, 1.0f};  // JIRA blue primary
        colors[1] = ImVec4{0.1f, 0.58f, 0.9f, 0.7f};  // JIRA blue secondary
        colors[2] = ImVec4(0.0f, 0.67f, 1.0f, 1.0f);  // Highlighted item color
        colors[3] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);   // Gray text
        colors[4] = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);   // Success green
        colors[5] = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);   // Error red
        colors[6] = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);   // Warning yellow
        
        jira_model_ = std::make_shared<models::jira_model>();
        
        saved_searches_.push_back({
            "My Open Issues", 
            "assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC"
        });
        saved_searches_.push_back({
            "Created Recently", 
            "created >= -1w ORDER BY created DESC"
        });
        saved_searches_.push_back({
            "Updated Recently", 
            "updated >= -1w ORDER BY updated DESC"
        });
        saved_searches_.push_back({
            "Reported By Me", 
            "reporter = currentUser() ORDER BY updated DESC"
        });
    }

    void draw() override {
        jira_ui::poll_async_tasks();
        if (!jira_model_->is_connected()) {
            render_connection_prompt();
            return;
        }
        
        check_async_operations();
        
        ImGui::TextColored(colors[0], "JIRA Advanced Search");
        ImGui::Separator();
        
        if (ImGui::BeginCombo("Saved Searches", "Select a saved search...")) {
            for (const auto& saved : saved_searches_) {
                if (ImGui::Selectable(saved.name.c_str())) {
                    std::strncpy(jql_query_, saved.jql.c_str(), sizeof(jql_query_) - 1);
                    jql_query_[sizeof(jql_query_) - 1] = '\0';
                }
                
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextWrapped("%s", saved.jql.c_str());
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Text("JQL Query:");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
        if (ImGui::InputText("##jql", jql_query_, sizeof(jql_query_), ImGuiInputTextFlags_EnterReturnsTrue)) {
            execute_search();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Search", ImVec2(90, 0))) {
            execute_search();
        }
        
        if (strlen(jql_query_) > 0) {
            if (ImGui::Button("Save Search", ImVec2(100, 0))) {
                ImGui::OpenPopup("SaveSearchPopup");
            }
        }
        
        if (ImGui::BeginPopup("SaveSearchPopup")) {
            ImGui::Text("Save Search As:");
            ImGui::InputText("##save_name", save_search_name_, sizeof(save_search_name_));
            
            if (ImGui::Button("Save") && strlen(save_search_name_) > 0) {
                saved_searches_.push_back({std::string(save_search_name_), std::string(jql_query_)});
                memset(save_search_name_, 0, sizeof(save_search_name_));
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                memset(save_search_name_, 0, sizeof(save_search_name_));
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("My Issues")) {
            std::strncpy(jql_query_, "assignee = currentUser() ORDER BY updated DESC", sizeof(jql_query_) - 1);
            jql_query_[sizeof(jql_query_) - 1] = '\0';
            execute_search();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Reported by Me")) {
            std::strncpy(jql_query_, "reporter = currentUser() ORDER BY updated DESC", sizeof(jql_query_) - 1);
            jql_query_[sizeof(jql_query_) - 1] = '\0';
            execute_search();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Updated Today")) {
            std::strncpy(jql_query_, "updated >= startOfDay() ORDER BY updated DESC", sizeof(jql_query_) - 1);
            jql_query_[sizeof(jql_query_) - 1] = '\0';
            execute_search();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Unresolved")) {
            std::strncpy(jql_query_, "resolution = Unresolved ORDER BY updated DESC", sizeof(jql_query_) - 1);
            jql_query_[sizeof(jql_query_) - 1] = '\0';
            execute_search();
        }
        
        ImGui::Separator();
        
        render_search_results();
    }

    [[nodiscard]] std::string title() const override {
        return "Jira Search";
    }

    [[nodiscard]] std::string uri() const override {
        return locator_.empty() ? "jira-search" : "jira-search:" + locator_;
    }

    void handle_uri(std::string_view locator) override {
        locator_ = std::string(locator);
        if (!locator_.empty()) {
            std::strncpy(jql_query_, locator_.c_str(), sizeof(jql_query_) - 1);
            jql_query_[sizeof(jql_query_) - 1] = '\0';
            execute_search();
        }
    }

private:
    struct saved_search {
        std::string name;
        std::string jql;
    };

    std::shared_ptr<models::jira_model> jira_model_;
    color_array colors;
    std::string locator_;
    
    char jql_query_[1024] = "";
    char save_search_name_[128] = "";
    std::vector<saved_search> saved_searches_;
    
    std::future<models::jira_search_result> search_future_;
    models::jira_search_result search_results_;
    bool is_searching_ = false;
    std::string error_message_;

    void execute_search() {
        if (strlen(jql_query_) == 0) return;
        
        is_searching_ = true;
        error_message_.clear();
        search_future_ = jira_model_->search_issues(jql_query_);
    }

    void check_async_operations() {
        if (search_future_.valid() && search_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                search_results_ = search_future_.get();
                is_searching_ = false;
            } catch (const std::exception& e) {
                error_message_ = e.what();
                is_searching_ = false;
            }
        }
    }

    void render_connection_prompt() {
        ImGui::TextColored(colors[6], "Not connected to JIRA");
        ImGui::Separator();
        ImGui::Text("Please connect using the main JIRA card first.");
        if (ImGui::Button("Open JIRA Card")) {
            create_card_uri("jira");
        }
    }

    void render_search_results() {
        if (is_searching_) {
            ImGui::TextColored(colors[1], "Executing search query...");
            return;
        }

        if (!error_message_.empty()) {
            ImGui::TextColored(colors[5], "Error: %s", error_message_.c_str());
            return;
        }

        if (search_results_.issues.empty()) {
            if (strlen(jql_query_) > 0) {
                ImGui::TextColored(colors[3], "No issues matched the query.");
            } else {
                ImGui::TextColored(colors[3], "Enter a JQL query above to search for issues.");
            }
            return;
        }

        ImGui::Text("Found %d issues (total %d):", (int)search_results_.issues.size(), search_results_.total);

        if (ImGui::BeginTable("SearchResultsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            jira_ui::TableRenderers::setup_issue_table_headers();

            jira_ui::render_filterable_table(
                search_results_.issues,
                "",
                colors,
                [](const models::jira_issue&) {
                    create_card_uri("jira");
                },
                [this](const models::jira_issue& issue) {
                    jira_ui::TableRenderers::render_status_column(issue, colors);
                }
            );

            ImGui::EndTable();
        }
    }
};

} // namespace rouen::cards
