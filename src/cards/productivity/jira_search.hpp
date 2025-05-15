#pragma once

#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include "../../helpers/imgui_include.hpp"
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <optional>
#include <chrono>
#include <format>

namespace rouen::cards {

class jira_search_card : public card {
public:
    jira_search_card() {
        // Set custom colors for the JIRA search card theme
        colors[0] = ImVec4{0.0f, 0.48f, 0.8f, 1.0f};  // JIRA blue primary
        colors[1] = ImVec4{0.1f, 0.58f, 0.9f, 0.7f};  // JIRA blue secondary
        get_color(2, ImVec4(0.0f, 0.67f, 1.0f, 1.0f));  // Highlighted item color
        get_color(3, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));   // Gray text
        get_color(4, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));   // Success green
        get_color(5, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));   // Error red
        get_color(6, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));   // Warning yellow
        
        name("JIRA Search");
        width = 500.0f;
        requested_fps = 5;
        
        // Initialize model
        jira_model_ = std::make_shared<models::jira_model>();
        
        // Default saved searches
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
    
    bool render() override {
        return render_window([this]() {
            // If not connected, show connection indicator
            if (!jira_model_->is_connected()) {
                render_connection_prompt();
                return;
            }
            
            // Check for completed async operations
            check_async_operations();
            
            // Search interface
            ImGui::TextColored(colors[0], "JIRA Advanced Search");
            ImGui::Separator();
            
            // Saved searches dropdown
            if (ImGui::BeginCombo("Saved Searches", "Select a saved search...")) {
                for (const auto& saved : saved_searches_) {
                    if (ImGui::Selectable(saved.name.c_str())) {
                        jql_query_ = saved.jql;
                    }
                    
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextWrapped("%s", saved.jql.c_str());
                        ImGui::EndTooltip();
                    }
                }
                ImGui::EndCombo();
            }
            
            // JQL query input
            ImGui::Text("JQL Query:");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
            if (ImGui::InputText("##jql", &jql_query_, ImGuiInputTextFlags_EnterReturnsTrue)) {
                execute_search();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Search", ImVec2(90, 0))) {
                execute_search();
            }
            
            // Save current search
            if (!jql_query_.empty()) {
                if (ImGui::Button("Save Search", ImVec2(100, 0))) {
                    ImGui::OpenPopup("SaveSearchPopup");
                }
            }
            
            // Save search popup
            if (ImGui::BeginPopup("SaveSearchPopup")) {
                ImGui::Text("Save Search As:");
                ImGui::InputText("##save_name", &save_search_name_);
                
                if (ImGui::Button("Save") && !save_search_name_.empty()) {
                    saved_searches_.push_back({save_search_name_, jql_query_});
                    save_search_name_.clear();
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    save_search_name_.clear();
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::Separator();
            
            // Quick filters
            if (ImGui::Button("My Issues")) {
                jql_query_ = "assignee = currentUser() ORDER BY updated DESC";
                execute_search();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Reported by Me")) {
                jql_query_ = "reporter = currentUser() ORDER BY updated DESC";
                execute_search();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Updated Today")) {
                jql_query_ = "updated >= startOfDay() ORDER BY updated DESC";
                execute_search();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Unresolved")) {
                jql_query_ = "resolution = Unresolved ORDER BY updated DESC";
                execute_search();
            }
            
            ImGui::Separator();
            
            // Results panel
            render_search_results();
        });
    }
    
    std::string get_uri() const override {
        return "jira-search";
    }
    
    // Allow setting initial JQL query via URI parameter
    static ptr create_with_query(std::string_view query) {
        auto card = std::make_shared<jira_search_card>();
        card->jql_query_ = query;
        card->execute_search();
        return card;
    }
    
private:
    struct saved_search {
        std::string name;
        std::string jql;
    };
    
    std::shared_ptr<models::jira_model> jira_model_;
    
    // Search state
    std::string jql_query_;
    std::future<models::jira_search_result> search_future_;
    std::optional<models::jira_search_result> search_result_;
    int current_page_ = 0;
    int items_per_page_ = 50;
    bool search_in_progress_ = false;
    std::string search_error_;
    
    // Issue details
    std::string selected_issue_key_;
    std::future<std::optional<models::jira_issue>> issue_future_;
    std::optional<models::jira_issue> selected_issue_;
    
    // Saved searches
    std::vector<saved_search> saved_searches_;
    std::string save_search_name_;
    
    void check_async_operations() {
        // Check if search results have been loaded
        if (search_future_.valid() && 
            search_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            search_result_ = search_future_.get();
            search_in_progress_ = false;
        }
        
        // Check if issue details have been loaded
        if (issue_future_.valid() && 
            issue_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            selected_issue_ = issue_future_.get();
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
    
    void execute_search() {
        current_page_ = 0;
        search_result_.reset();
        selected_issue_key_.clear();
        selected_issue_.reset();
        search_error_.clear();
        search_in_progress_ = true;
        
        if (jql_query_.empty()) {
            jql_query_ = "order by updated DESC";
        }
        
        search_future_ = jira_model_->search_issues(
            jql_query_, 
            current_page_ * items_per_page_, 
            items_per_page_
        );
    }
    
    void render_search_results() {
        if (search_in_progress_) {
            ImGui::TextColored(colors[3], "Searching...");
            return;
        }
        
        if (!search_result_.has_value()) {
            if (!search_future_.valid() && jql_query_.empty()) {
                ImGui::TextColored(colors[3], "Enter a JQL query to search");
            } else if (!search_future_.valid()) {
                execute_search();
                ImGui::TextColored(colors[3], "Searching...");
            }
            return;
        }
        
        // Check for empty results
        if (search_result_->issues.empty()) {
            ImGui::TextColored(colors[3], "No issues found for your query");
            return;
        }
        
        // Results count
        ImGui::TextColored(colors[0], "Results: %d issues found", search_result_->total);
        
        // Split view: issues list on left, details on right
        ImGui::Columns(2, "SearchResultsColumns", true);
        
        // Left column: issues list
        render_issues_list();
        
        ImGui::NextColumn();
        
        // Right column: issue details
        render_issue_details();
        
        ImGui::Columns(1);
    }
    
    void render_issues_list() {
        if (ImGui::BeginChild("SearchResults", ImVec2(0, -35), true)) {
            for (const auto& issue : search_result_->issues) {
                ImVec4 type_color = colors[0];
                
                // Different colors based on issue type
                if (issue.issue_type.name == "Bug") {
                    type_color = colors[5]; // Red for bugs
                } else if (issue.issue_type.name == "Task") {
                    type_color = colors[3]; // Gray for tasks
                } else if (issue.issue_type.name == "Story") {
                    type_color = colors[2]; // Blue for stories
                } else if (issue.issue_type.name == "Epic") {
                    type_color = colors[6]; // Yellow for epics
                }
                
                // Issue in the list
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(type_color.x, type_color.y, type_color.z, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(type_color.x, type_color.y, type_color.z, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(type_color.x, type_color.y, type_color.z, 0.7f));
                
                bool is_selected = selected_issue_key_ == issue.key;
                if (ImGui::Selectable(std::format("##{}_{}", issue.id, issue.key).c_str(), is_selected)) {
                    selected_issue_key_ = issue.key;
                    selected_issue_.reset();
                    issue_future_ = jira_model_->get_issue(issue.key);
                }
                
                ImGui::PopStyleColor(3);
                
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                    ImGui::OpenPopup(std::format("IssueContextMenu_{}", issue.key).c_str());
                }
                
                // Context menu for issue
                if (ImGui::BeginPopup(std::format("IssueContextMenu_{}", issue.key).c_str())) {
                    if (ImGui::MenuItem("Copy key")) {
                        ImGui::SetClipboardText(issue.key.c_str());
                    }
                    
                    if (ImGui::MenuItem("Open in browser")) {
                        std::string url = std::format("{}/browse/{}", jira_model_->get_server_url(), issue.key);
                        // Open URL in browser (platform-specific)
                        #ifdef _WIN32
                        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        #elif defined(__APPLE__)
                        system(std::format("open \"{}\"", url).c_str());
                        #else
                        system(std::format("xdg-open \"{}\"", url).c_str());
                        #endif
                    }
                    
                    ImGui::EndPopup();
                }
                
                ImGui::SameLine();
                
                // Draw colored rectangle for issue type
                ImVec2 rect_pos = ImGui::GetCursorScreenPos();
                ImVec2 rect_size(8, ImGui::GetTextLineHeight());
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rect_pos, 
                    ImVec2(rect_pos.x + rect_size.x, rect_pos.y + rect_size.y),
                    ImGui::GetColorU32(type_color));
                
                ImGui::Dummy(ImVec2(rect_size.x + 4, rect_size.y));
                ImGui::SameLine();
                
                // Project key
                ImGui::TextColored(colors[3], "[%s]", issue.project.key.c_str());
                ImGui::SameLine();
                
                // Issue key and summary
                ImGui::TextColored(colors[3], "%s", issue.key.c_str());
                ImGui::SameLine();
                ImGui::TextWrapped("%s", issue.summary.c_str());
                
                // Status indicator
                ImVec4 status_color = colors[3]; // Default gray
                
                if (issue.status.category == "To Do") {
                    status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
                } else if (issue.status.category == "In Progress") {
                    status_color = ImVec4(0.0f, 0.5f, 0.9f, 1.0f); // Blue
                } else if (issue.status.category == "Done") {
                    status_color = ImVec4(0.0f, 0.7f, 0.2f, 1.0f); // Green
                }
                
                ImGui::TextColored(status_color, "   %s", issue.status.name.c_str());
                
                // Assignee indicator if assigned
                if (!issue.assignee.display_name.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(colors[3], "   Assigned to: %s", issue.assignee.display_name.c_str());
                }
                
                ImGui::Separator();
            }
        }
        ImGui::EndChild();
        
        // Pagination controls
        if (search_result_->total > 0) {
            int max_page = (search_result_->total + items_per_page_ - 1) / items_per_page_ - 1;
            
            if (ImGui::Button("< Prev") && current_page_ > 0) {
                current_page_--;
                search_future_ = jira_model_->search_issues(
                    jql_query_, 
                    current_page_ * items_per_page_, 
                    items_per_page_
                );
                search_in_progress_ = true;
            }
            
            ImGui::SameLine();
            ImGui::Text("Page %d of %d", current_page_ + 1, max_page + 1);
            ImGui::SameLine();
            
            if (ImGui::Button("Next >") && current_page_ < max_page) {
                current_page_++;
                search_future_ = jira_model_->search_issues(
                    jql_query_, 
                    current_page_ * items_per_page_, 
                    items_per_page_
                );
                search_in_progress_ = true;
            }
        }
    }
    
    void render_issue_details() {
        if (selected_issue_key_.empty()) {
            ImGui::TextColored(colors[3], "Select an issue from the list to view details");
            return;
        }
        
        if (!selected_issue_.has_value()) {
            ImGui::TextColored(colors[3], "Loading issue details...");
            return;
        }
        
        const auto& issue = selected_issue_.value();
        
        // Issue header
        ImGui::BeginChild("IssueDetails", ImVec2(0, 0), true);
        
        // Issue key and summary
        ImGui::TextColored(colors[0], "%s", issue.key.c_str());
        ImGui::SameLine();
        
        // Add clickable link to open in browser
        if (ImGui::SmallButton("Open in browser")) {
            std::string url = std::format("{}/browse/{}", jira_model_->get_server_url(), issue.key);
            // Open URL in browser (platform-specific)
            #ifdef _WIN32
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            #elif defined(__APPLE__)
            system(std::format("open \"{}\"", url).c_str());
            #else
            system(std::format("xdg-open \"{}\"", url).c_str());
            #endif
        }
        
        ImGui::SameLine();
        
        // Add copy key button
        if (ImGui::SmallButton("Copy Key")) {
            ImGui::SetClipboardText(issue.key.c_str());
        }
        
        ImGui::SameLine();
        if (ImGui::SmallButton("Open in JIRA Card")) {
            // Open in main JIRA card
            "jira_open_issue"_sfn(issue.key);
            "create_card"_sfn("jira");
            grab_focus = false;  // Give focus to the new card
        }
        
        // Issue summary
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Use a larger font if available
        ImGui::TextWrapped("%s", issue.summary.c_str());
        ImGui::PopFont();
        
        ImGui::Separator();
        
        // Issue metadata
        ImGui::Columns(2, "IssueMetadataColumns", false);
        
        ImGui::TextColored(colors[3], "Type:");
        ImGui::SameLine();
        ImGui::Text("%s", issue.issue_type.name.c_str());
        
        ImGui::TextColored(colors[3], "Status:");
        ImGui::SameLine();
        ImGui::Text("%s", issue.status.name.c_str());
        
        ImGui::TextColored(colors[3], "Project:");
        ImGui::SameLine();
        ImGui::Text("%s", issue.project.name.c_str());
        
        ImGui::NextColumn();
        
        ImGui::TextColored(colors[3], "Reporter:");
        ImGui::SameLine();
        ImGui::Text("%s", issue.reporter.display_name.c_str());
        
        ImGui::TextColored(colors[3], "Assignee:");
        ImGui::SameLine();
        if (issue.assignee.display_name.empty()) {
            ImGui::TextColored(colors[3], "Unassigned");
        } else {
            ImGui::Text("%s", issue.assignee.display_name.c_str());
        }
        
        // Format dates
        auto format_time = [](const std::chrono::system_clock::time_point& time) {
            auto time_t = std::chrono::system_clock::to_time_t(time);
            std::tm tm = *std::localtime(&time_t);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
            return std::string(buffer);
        };
        
        ImGui::TextColored(colors[3], "Created:");
        ImGui::SameLine();
        ImGui::Text("%s", format_time(issue.created).c_str());
        
        ImGui::TextColored(colors[3], "Updated:");
        ImGui::SameLine();
        ImGui::Text("%s", format_time(issue.updated).c_str());
        
        ImGui::Columns(1);
        
        ImGui::Separator();
        
        // Issue description
        ImGui::TextColored(colors[0], "Description");
        ImGui::BeginChild("IssueDescription", ImVec2(0, 100), true);
        if (issue.description.empty()) {
            ImGui::TextColored(colors[3], "No description");
        } else {
            ImGui::TextWrapped("%s", issue.description.c_str());
        }
        ImGui::EndChild();
        
        // Comments indicator
        if (!issue.comments.empty()) {
            ImGui::TextColored(colors[0], "Comments (%zu)", issue.comments.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("View All")) {
                // Open in main JIRA card to view comments
                "jira_open_issue"_sfn(issue.key);
                "create_card"_sfn("jira");
                grab_focus = false;
            }
        }
        
        // Similar JQL searches
        ImGui::Separator();
        ImGui::TextColored(colors[0], "Related Searches");
        
        if (ImGui::Button("Same Project")) {
            jql_query_ = std::format("project = {} ORDER BY updated DESC", issue.project.key);
            execute_search();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Same Type")) {
            jql_query_ = std::format("project = {} AND issuetype = '{}' ORDER BY updated DESC", 
                                    issue.project.key, issue.issue_type.name);
            execute_search();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Same Reporter")) {
            jql_query_ = std::format("reporter = '{}' ORDER BY updated DESC", 
                                    issue.reporter.display_name);
            execute_search();
        }
        
        if (!issue.assignee.display_name.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Same Assignee")) {
                jql_query_ = std::format("assignee = '{}' ORDER BY updated DESC", 
                                        issue.assignee.display_name);
                execute_search();
            }
        }
        
        ImGui::EndChild(); // End issue details
    }
};

} // namespace rouen::cards
