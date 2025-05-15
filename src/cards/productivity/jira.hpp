#pragma once

#include "../interface/card.hpp"
#include "../../models/jira_model.hpp"
#include "../../helpers/imgui_include.hpp"  // Use our wrapper to avoid warnings
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <format>

namespace rouen::cards {

class jira_card : public card {
public:
    jira_card() {
        // Set custom colors for the JIRA card theme
        colors[0] = ImVec4{0.0f, 0.48f, 0.8f, 1.0f};  // JIRA blue primary
        colors[1] = ImVec4{0.1f, 0.58f, 0.9f, 0.7f};  // JIRA blue secondary
        get_color(2, ImVec4(0.0f, 0.67f, 1.0f, 1.0f));  // Highlighted item color
        get_color(3, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));   // Gray text
        get_color(4, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));   // Success green
        get_color(5, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));   // Error red
        get_color(6, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));   // Warning yellow
        
        name("JIRA");
        width = 500.0f;
        requested_fps = 5;
        
        // Initialize model
        jira_model_ = std::make_shared<models::jira_model>();
        
        // Load available profiles
        connection_profiles_ = models::jira_model::load_profiles();
        
        // We'll attempt to connect in the first render call rather than in the constructor
        // to avoid potential thread safety issues during initialization
    }
    
    bool render() override {
        // Check if we need to initiate first-time connection
        if (!first_render_done_ && !connection_profiles_.empty() && !connection_started_) {
            connection_started_ = true;
            // Schedule the connection for later to prevent blocking the UI
            std::thread([this]() {
                // Add a small delay to ensure the UI is fully initialized
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                bool success = jira_model_->connect(connection_profiles_[0]);
                if (success) {
                    status_message_ = std::format("Connected to {} automatically", connection_profiles_[0].name);
                    
                    // Fetch projects after connection
                    projects_future_ = jira_model_->get_projects();
                }
            }).detach();
        }
        first_render_done_ = true;
        
        return render_window([this]() {
            // If not connected, show login form
            if (!jira_model_->is_connected()) {
                render_login_form();
                return;
            }
            
            // Top tabs for different views
            if (ImGui::BeginTabBar("JiraTabs")) {
                if (ImGui::BeginTabItem("Issues")) {
                    render_issues_tab();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Create Issue")) {
                    render_create_issue_tab();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Settings")) {
                    render_settings_tab();
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
        });
    }
    
    std::string get_uri() const override {
        return "jira";
    }
    
private:
    std::shared_ptr<models::jira_model> jira_model_;
    
    // Login form state
    std::string server_url_ = "https://";
    std::string username_;
    std::string api_token_;
    std::string status_message_;
    bool login_in_progress_ = false;
    std::vector<models::jira_connection_profile> connection_profiles_;
    bool show_add_profile_ = false;
    std::string new_profile_name_;
    
    // Issues view state
    std::string selected_project_;
    std::string filter_text_;
    int current_page_ = 0;
    int items_per_page_ = 20;
    std::future<models::jira_search_result> search_future_;
    std::optional<models::jira_search_result> search_result_;
    std::string selected_issue_key_;
    std::future<std::optional<models::jira_issue>> issue_future_;
    std::optional<models::jira_issue> selected_issue_;
    
    // Projects state
    std::future<std::vector<models::jira_project>> projects_future_;
    std::vector<models::jira_project> projects_;
    
    // Create issue state
    std::string create_project_key_;
    std::string create_issue_type_id_;
    std::string create_summary_;
    std::string create_description_;
    bool issue_creation_in_progress_ = false;
    
    void render_login_form() {
        ImGui::TextColored(colors[0], "Connect to JIRA");
        ImGui::Separator();
        
        // Display available connection profiles
        if (!connection_profiles_.empty()) {
            ImGui::TextColored(colors[0], "Available Connections");
            ImGui::BeginChild("ConnectionProfiles", ImVec2(0, 150), true);
            
            for (const auto& profile : connection_profiles_) {
                if (ImGui::Selectable(std::format("{}##profile", profile.name).c_str())) {
                    connect_to_profile(profile);
                }
                
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Server: %s", profile.server_url.c_str());
                    ImGui::Text("Username: %s", profile.username.c_str());
                    ImGui::EndTooltip();
                }
            }
            
            ImGui::EndChild();
            
            if (ImGui::Button("Connect with New Credentials")) {
                show_add_profile_ = true;
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Refresh Profiles")) {
                connection_profiles_ = models::jira_model::load_profiles();
            }
            
            ImGui::Separator();
        }
        
        // Manual credentials entry
        if (connection_profiles_.empty() || show_add_profile_) {
            ImGui::TextColored(colors[0], "Manual Connection");
            
            ImGui::Text("Connection Name (optional):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##profile_name", &new_profile_name_);
            
            ImGui::Text("Server URL:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##server_url", &server_url_);
            
            ImGui::Text("Username:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##username", &username_);
            
            ImGui::Text("API Token:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##api_token", &api_token_, ImGuiInputTextFlags_Password);
            
            ImGui::TextWrapped("Note: For Atlassian Cloud instances, use your email as username and an API token from your Atlassian account.");
            
            if (ImGui::Button("Connect", ImVec2(120, 0)) && !server_url_.empty() && !username_.empty() && !api_token_.empty()) {
                login_in_progress_ = true;
                status_message_ = "Connecting...";
                
                // Run connection in background
                std::thread([this]() {
                    bool success = jira_model_->connect(server_url_, username_, api_token_);
                    
                    if (success) {
                        status_message_ = "Connected successfully!";
                        
                        // Save the profile if it has a name
                        if (!new_profile_name_.empty()) {
                            models::jira_connection_profile profile;
                            profile.name = new_profile_name_;
                            profile.server_url = server_url_;
                            profile.username = username_;
                            profile.api_token = api_token_;
                            
                            connection_profiles_.push_back(profile);
                            models::jira_model::save_profiles(connection_profiles_);
                        }
                        
                        // Fetch projects after connection
                        projects_future_ = jira_model_->get_projects();
                    } else {
                        status_message_ = "Connection failed. Check credentials and URL.";
                    }
                    
                    login_in_progress_ = false;
                }).detach();
            }
            
            if (show_add_profile_ && ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_add_profile_ = false;
                new_profile_name_.clear();
            }
        }
        
        if (!status_message_.empty()) {
            ImVec4 color = status_message_.starts_with("Connected") ? colors[4] : colors[5];
            ImGui::TextColored(color, "%s", status_message_.c_str());
        }
        
        if (login_in_progress_) {
            ImGui::TextColored(colors[3], "Connecting, please wait...");
            
            // Simple spinner
            float time = ImGui::GetTime();
            const float radius = 10.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x += radius + 10.0f;
            pos.y += radius + 5.0f;
            ImGui::GetWindowDrawList()->AddCircleFilled(pos, radius, ImGui::GetColorU32(colors[0]), 12);
            
            for (int i = 0; i < 4; i++) {
                float a = time * 8.0f + i * 3.14159f * 0.5f;
                ImVec2 p(pos.x + cosf(a) * radius * 0.8f, pos.y + sinf(a) * radius * 0.8f);
                ImGui::GetWindowDrawList()->AddCircleFilled(p, radius * 0.25f, 
                                                           ImGui::GetColorU32(ImVec4(1,1,1,1)), 12);
            }
            
            ImGui::Dummy(ImVec2(radius * 2 + 20, radius * 2 + 10));
        }
    }
    
    void connect_to_profile(const models::jira_connection_profile& profile) {
        login_in_progress_ = true;
        status_message_ = std::format("Connecting to {}...", profile.name);
        
        // Run connection in background
        std::thread([this, profile]() {
            bool success = jira_model_->connect(profile);
            
            if (success) {
                status_message_ = std::format("Connected to {} successfully!", profile.name);
                
                // Fetch projects after connection
                projects_future_ = jira_model_->get_projects();
            } else {
                status_message_ = std::format("Connection to {} failed. Check credentials and URL.", profile.name);
            }
            
            login_in_progress_ = false;
        }).detach();
    }
    
    void check_async_operations() {
        // Check if projects have been loaded
        if (projects_future_.valid() && 
            projects_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            projects_ = projects_future_.get();
        }
        
        // Check if search results have been loaded
        if (search_future_.valid() && 
            search_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            search_result_ = search_future_.get();
        }
        
        // Check if issue details have been loaded
        if (issue_future_.valid() && 
            issue_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            selected_issue_ = issue_future_.get();
        }
    }
    
    void render_issues_tab() {
        check_async_operations();
        
        // Project selection dropdown
        if (ImGui::BeginCombo("Project", selected_project_.empty() ? "All Projects" : selected_project_.c_str())) {
            if (ImGui::Selectable("All Projects", selected_project_.empty())) {
                selected_project_ = "";
            }
            
            for (const auto& project : projects_) {
                if (ImGui::Selectable(std::format("{}  -  {}", project.key, project.name).c_str(), 
                                     selected_project_ == project.key)) {
                    selected_project_ = project.key;
                }
                
                if (ImGui::IsItemHovered() && !project.description.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::TextWrapped("%s", project.description.c_str());
                    ImGui::EndTooltip();
                }
            }
            
            ImGui::EndCombo();
        }
        
        // Filter and search
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);
        if (ImGui::InputText("##filter", &filter_text_, ImGuiInputTextFlags_EnterReturnsTrue)) {
            search_issues();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Search", ImVec2(100, 0))) {
            search_issues();
        }
        
        ImGui::Separator();
        
        // Split view: issues list on left, details on right
        ImGui::Columns(2, "IssuesColumns", true);
        
        // Left column: issues list
        render_issues_list();
        
        ImGui::NextColumn();
        
        // Right column: issue details
        render_issue_details();
        
        ImGui::Columns(1);
    }
    
    void search_issues() {
        std::string jql;
        
        if (!selected_project_.empty()) {
            jql = std::format("project = {}", selected_project_);
        }
        
        if (!filter_text_.empty()) {
            if (!jql.empty()) {
                jql += " AND ";
            }
            
            // Check if filter_text is a key directly
            if (filter_text_.find('-') != std::string::npos) {
                jql += std::format("key = {} OR ", filter_text_);
            }
            
            jql += std::format("summary ~ \"{}\" OR description ~ \"{}\"", 
                              filter_text_, filter_text_);
        }
        
        // Explicitly include status conditions to get backlog items
        // This is crucial for showing backlog issues that might be filtered out by default
        if (jql.empty()) {
            jql = "status in (Open, \"In Progress\", Reopened, \"To Do\", Backlog, \"Selected for Development\", New, \"In Review\", Done, Closed) order by updated DESC";
        } else {
            jql += " AND status in (Open, \"In Progress\", Reopened, \"To Do\", Backlog, \"Selected for Development\", New, \"In Review\", Done, Closed) order by updated DESC";
        }
        
        JIRA_DEBUG_FMT("Searching JIRA with JQL: {}", jql);
        search_future_ = jira_model_->search_issues(jql, current_page_ * items_per_page_, items_per_page_);
    }
    
    void render_issues_list() {
        if (!search_result_.has_value()) {
            if (!search_future_.valid()) {
                search_issues();
            }
            
            ImGui::TextColored(colors[3], "Loading issues...");
            return;
        }
        
        if (search_result_->issues.empty()) {
            ImGui::TextColored(colors[3], "No issues found.");
            return;
        }
        
        // Issues list with pagination
        if (ImGui::BeginChild("IssuesList", ImVec2(0, -35), true)) {
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
                search_issues();
            }
            
            ImGui::SameLine();
            ImGui::Text("Page %d of %d", current_page_ + 1, max_page + 1);
            ImGui::SameLine();
            
            if (ImGui::Button("Next >") && current_page_ < max_page) {
                current_page_++;
                search_issues();
            }
        }
    }
    
    void render_issue_details() {
        if (selected_issue_key_.empty()) {
            ImGui::TextColored(colors[3], "Select an issue to view details");
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
        
        // Comments section
        ImGui::TextColored(colors[0], "Comments (%zu)", issue.comments.size());
        
        if (ImGui::BeginChild("IssueComments", ImVec2(0, 150), true)) {
            for (const auto& comment : issue.comments) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                ImGui::BeginChild(std::format("Comment_{}", comment.id).c_str(), 
                                ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysUseWindowPadding);
                
                ImGui::TextColored(colors[0], "%s", comment.author.display_name.c_str());
                ImGui::SameLine();
                ImGui::TextColored(colors[3], "%s", format_time(comment.created).c_str());
                
                ImGui::Separator();
                ImGui::TextWrapped("%s", comment.body.c_str());
                
                ImGui::EndChild();
                ImGui::PopStyleColor();
                
                ImGui::Separator();
            }
        }
        ImGui::EndChild();
        
        // Add comment
        static std::string new_comment;
        ImGui::TextColored(colors[0], "Add Comment");
        ImGui::InputTextMultiline("##new_comment", &new_comment, ImVec2(-1, 80));
        
        static bool comment_in_progress = false;
        if (!comment_in_progress && ImGui::Button("Add Comment", ImVec2(120, 0)) && !new_comment.empty()) {
            comment_in_progress = true;
            
            // Add comment in background
            std::thread([this, comment_text = new_comment]() {
                auto future = jira_model_->add_comment(selected_issue_key_, comment_text);
                bool success = future.get();
                
                if (success) {
                    // Refresh issue details
                    issue_future_ = jira_model_->get_issue(selected_issue_key_);
                }
                
                comment_in_progress = false;
            }).detach();
            
            new_comment.clear();
        }
        
        if (comment_in_progress) {
            ImGui::SameLine();
            ImGui::TextColored(colors[3], "Adding comment...");
        }
        
        // Attachments section
        if (!issue.attachments.empty()) {
            ImGui::TextColored(colors[0], "Attachments (%zu)", issue.attachments.size());
            
            if (ImGui::BeginChild("IssueAttachments", ImVec2(0, 100), true)) {
                for (const auto& attachment : issue.attachments) {
                    if (ImGui::Selectable(std::format("{}  ({:.2f} KB)", 
                                        attachment.filename, 
                                        attachment.size / 1024.0f).c_str())) {
                        // Open attachment in browser
                        #ifdef _WIN32
                        ShellExecuteA(NULL, "open", attachment.url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        #elif defined(__APPLE__)
                        system(std::format("open \"{}\"", attachment.url).c_str());
                        #else
                        system(std::format("xdg-open \"{}\"", attachment.url).c_str());
                        #endif
                    }
                }
            }
            ImGui::EndChild();
        }
        
        ImGui::EndChild(); // End issue details
    }
    
    void render_create_issue_tab() {
        check_async_operations();
        
        ImGui::TextColored(colors[0], "Create New Issue");
        ImGui::Separator();
        
        // Project dropdown
        if (ImGui::BeginCombo("Project", create_project_key_.empty() ? "Select Project" : create_project_key_.c_str())) {
            for (const auto& project : projects_) {
                if (ImGui::Selectable(std::format("{}  -  {}", project.key, project.name).c_str(), 
                                    create_project_key_ == project.key)) {
                    create_project_key_ = project.key;
                }
                
                if (ImGui::IsItemHovered() && !project.description.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::TextWrapped("%s", project.description.c_str());
                    ImGui::EndTooltip();
                }
            }
            
            ImGui::EndCombo();
        }
        
        // Issue type dropdown (only if project is selected)
        if (!create_project_key_.empty()) {
            const auto project = std::find_if(projects_.begin(), projects_.end(), 
                                           [this](const auto& p) { return p.key == create_project_key_; });
            
            if (project != projects_.end()) {
                std::string issue_type_name;
                for (const auto& type : project->issue_types) {
                    if (type.id == create_issue_type_id_) {
                        issue_type_name = type.name;
                        break;
                    }
                }
                
                if (ImGui::BeginCombo("Issue Type", issue_type_name.empty() ? "Select Type" : issue_type_name.c_str())) {
                    for (const auto& type : project->issue_types) {
                        // Skip subtasks
                        if (type.is_subtask) continue;
                        
                        if (ImGui::Selectable(type.name.c_str(), create_issue_type_id_ == type.id)) {
                            create_issue_type_id_ = type.id;
                        }
                    }
                    
                    ImGui::EndCombo();
                }
            }
        }
        
        // Summary and description
        ImGui::Text("Summary:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##create_summary", &create_summary_);
        
        ImGui::Text("Description:");
        ImGui::InputTextMultiline("##create_description", &create_description_, 
                                ImVec2(-1, ImGui::GetContentRegionAvail().y - 50));
        
        // Create button
        bool can_create = !create_project_key_.empty() && 
                        !create_issue_type_id_.empty() && 
                        !create_summary_.empty();
        
        if (!can_create) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        }
        
        if (ImGui::Button("Create Issue", ImVec2(120, 0)) && can_create && !issue_creation_in_progress_) {
            issue_creation_in_progress_ = true;
            
            // Create issue in background
            std::thread([this]() {
                auto future = jira_model_->create_issue(
                    create_project_key_,
                    create_issue_type_id_,
                    create_summary_,
                    create_description_
                );
                
                auto new_issue = future.get();
                if (new_issue) {
                    // Clear form
                    create_summary_.clear();
                    create_description_.clear();
                    
                    // Select the new issue
                    selected_issue_key_ = new_issue->key;
                    selected_issue_ = new_issue;
                }
                
                issue_creation_in_progress_ = false;
            }).detach();
        }
        
        if (!can_create) {
            ImGui::PopStyleVar();
        }
        
        if (issue_creation_in_progress_) {
            ImGui::SameLine();
            ImGui::TextColored(colors[3], "Creating issue...");
        }
    }
    
    void render_settings_tab() {
        ImGui::TextColored(colors[0], "JIRA Connection Settings");
        ImGui::Separator();
        
        // Show current connection info
        auto current_profile = jira_model_->get_current_profile();
        
        ImGui::TextColored(colors[3], "Connected to:");
        ImGui::SameLine();
        ImGui::TextColored(colors[0], "%s", current_profile.name.c_str());
        
        ImGui::TextColored(colors[3], "Server:");
        ImGui::SameLine();
        ImGui::TextColored(colors[0], "%s", jira_model_->get_server_url().c_str());
        
        ImGui::TextColored(colors[3], "Username:");
        ImGui::SameLine();
        ImGui::TextColored(colors[0], "%s", current_profile.username.c_str());
        
        if (ImGui::Button("Disconnect", ImVec2(120, 0))) {
            jira_model_ = std::make_shared<models::jira_model>();
            selected_issue_key_.clear();
            selected_issue_.reset();
            search_result_.reset();
            projects_.clear();
            connection_profiles_ = models::jira_model::load_profiles();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Switch Account", ImVec2(120, 0))) {
            jira_model_ = std::make_shared<models::jira_model>();
            selected_issue_key_.clear();
            selected_issue_.reset();
            search_result_.reset();
            projects_.clear();
        }
        
        ImGui::Separator();
        
        // Connection profiles management
        ImGui::TextColored(colors[0], "Manage Saved Connections");
        
        if (ImGui::Button("Reload Profiles", ImVec2(120, 0))) {
            connection_profiles_ = models::jira_model::load_profiles();
        }
        
        if (ImGui::BeginChild("SavedProfiles", ImVec2(0, 150), true)) {
            for (size_t i = 0; i < connection_profiles_.size(); ++i) {
                const auto& profile = connection_profiles_[i];
                
                // Skip environment variable derived profiles for deletion
                bool is_env_profile = false;
                for (const auto& env_profile : models::jira_model::get_env_profiles()) {
                    if (env_profile.name == profile.name) {
                        is_env_profile = true;
                        break;
                    }
                }
                
                ImGui::PushID(static_cast<int>(i));
                
                if (ImGui::Selectable(profile.name.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth)) {
                    connect_to_profile(profile);
                }
                
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Server: %s", profile.server_url.c_str());
                    ImGui::Text("Username: %s", profile.username.c_str());
                    if (is_env_profile) {
                        ImGui::TextColored(colors[6], "From environment variables");
                    }
                    ImGui::EndTooltip();
                }
                
                ImGui::SameLine(ImGui::GetWindowWidth() - 30);
                
                // Only allow deletion of user-defined profiles
                if (!is_env_profile && ImGui::SmallButton("X")) {
                    // Ask for confirmation
                    ImGui::OpenPopup("DeleteProfileConfirm");
                }
                
                if (ImGui::BeginPopup("DeleteProfileConfirm")) {
                    ImGui::Text("Delete profile '%s'?", profile.name.c_str());
                    ImGui::Separator();
                    
                    if (ImGui::Button("Yes", ImVec2(80, 0))) {
                        connection_profiles_.erase(connection_profiles_.begin() + i);
                        models::jira_model::save_profiles(connection_profiles_);
                        ImGui::CloseCurrentPopup();
                    }
                    
                    ImGui::SameLine();
                    
                    if (ImGui::Button("No", ImVec2(80, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    
                    ImGui::EndPopup();
                }
                
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        
        if (ImGui::Button("Add New Connection", ImVec2(150, 0))) {
            jira_model_ = std::make_shared<models::jira_model>();
            selected_issue_key_.clear();
            selected_issue_.reset();
            search_result_.reset();
            projects_.clear();
            show_add_profile_ = true;
        }
        
        ImGui::Separator();
        
        // View refresh settings
        ImGui::TextColored(colors[0], "Refresh Settings");
        
        static int refresh_interval = 5;
        ImGui::SliderInt("Auto-refresh interval (minutes)", &refresh_interval, 1, 30);
        
        if (ImGui::Button("Manual Refresh", ImVec2(120, 0))) {
            // Refresh data
            if (!selected_issue_key_.empty()) {
                issue_future_ = jira_model_->get_issue(selected_issue_key_);
            }
            
            if (search_result_.has_value()) {
                search_issues();
            }
            
            projects_future_ = jira_model_->get_projects();
        }
    }
    
    // State for first-time connection
    bool first_render_done_ = false;
    bool connection_started_ = false;
};

} // namespace rouen::cards
