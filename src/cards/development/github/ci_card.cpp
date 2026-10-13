#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../../helpers/glaze_include.hpp"
#include <imgui.h>

#include "ci_card.hpp"
#include "IconsMaterialDesign.h"
#include "models/github/host.hpp"
#include "models/github/login_host.hpp"
#include "registrar.hpp"
#include "../../../fonts.hpp"
#include "../../../helpers/fetch.hpp"
#include "../../../helpers/imgui_ui_context.hpp"
#include "../../../helpers/platform_utils.hpp"

namespace rouen::cards::github {

    WorkflowRun WorkflowRun::from_json(const glz::json_t& json) {
        WorkflowRun run;
        
        try {
            if (json.contains("id")) {
                run.id = std::to_string(static_cast<int64_t>(json["id"].get<double>()));
            }
            if (json.contains("name")) {
                run.name = json["name"].get<std::string>();
            }
            if (json.contains("workflow_name")) {
                run.workflow_name = json["workflow_name"].get<std::string>();
            }
            if (json.contains("head_branch")) {
                run.branch = json["head_branch"].get<std::string>();
            }
            if (json.contains("head_sha")) {
                run.commit_sha = json["head_sha"].get<std::string>();
                if (run.commit_sha.length() > 8) {
                    run.commit_sha = run.commit_sha.substr(0, 8);
                }
            }
            if (json.contains("head_commit") && json["head_commit"].contains("message")) {
                run.commit_message = json["head_commit"]["message"].get<std::string>();
                // Truncate to first line
                auto newline_pos = run.commit_message.find('\n');
                if (newline_pos != std::string::npos) {
                    run.commit_message = run.commit_message.substr(0, newline_pos);
                }
            }
            if (json.contains("actor") && json["actor"].contains("login")) {
                run.author = json["actor"]["login"].get<std::string>();
            }
            
            std::string status_str, conclusion_str;
            if (json.contains("status")) {
                status_str = json["status"].get<std::string>();
            }
            if (json.contains("conclusion") && !json["conclusion"].is_null()) {
                conclusion_str = json["conclusion"].get<std::string>();
            }
            run.status = parse_status(status_str, conclusion_str);
            run.conclusion = conclusion_str;
            
            if (json.contains("created_at")) {
                run.created_at = json["created_at"].get<std::string>();
            }
            if (json.contains("updated_at")) {
                run.updated_at = json["updated_at"].get<std::string>();
            }
            if (json.contains("run_started_at")) {
                run.run_started_at = json["run_started_at"].get<std::string>();
            }
            if (json.contains("html_url")) {
                run.html_url = json["html_url"].get<std::string>();
            }
            if (json.contains("run_number")) {
                run.run_number = static_cast<int>(json["run_number"].get<double>());
            }
            if (json.contains("run_attempt")) {
                run.run_attempt = static_cast<int>(json["run_attempt"].get<double>());
            }
            if (json.contains("event")) {
                run.event_type = json["event"].get<std::string>();
            }
            
        } catch (const std::exception& e) {
            // Handle JSON parsing errors gracefully
            (void)e;
        }
        
        return run;
    }

    WorkflowStatus WorkflowRun::parse_status(const std::string& status_str, const std::string& conclusion_str) {
        if (status_str == "queued") {
            return WorkflowStatus::Queued;
        } 
        if (status_str == "in_progress") {
            return WorkflowStatus::InProgress;
        } 
        if (status_str == "completed") {
            if (conclusion_str == "success") {
                return WorkflowStatus::Success;
            } 
            if (conclusion_str == "failure") {
                return WorkflowStatus::Failed;
            } 
            if (conclusion_str == "cancelled") {
                return WorkflowStatus::Cancelled;
            } 
            if (conclusion_str == "skipped") {
                return WorkflowStatus::Skipped;
            } 
            return WorkflowStatus::Completed;
        }
        return WorkflowStatus::Unknown;
    }

    ImVec4 WorkflowRun::get_status_color(WorkflowStatus status) {
        switch (status) {
            case WorkflowStatus::Unknown:
                return {0.5f, 0.5f, 0.5f, 1.0f};  // Dark gray
            case WorkflowStatus::Success:
                return {0.0f, 0.8f, 0.2f, 1.0f};  // Green
            case WorkflowStatus::Failed:
                return {0.9f, 0.2f, 0.2f, 1.0f};  // Red
            case WorkflowStatus::InProgress:
                return {0.2f, 0.6f, 0.9f, 1.0f};  // Blue
            case WorkflowStatus::Queued:
                return {0.9f, 0.7f, 0.2f, 1.0f};  // Yellow
            case WorkflowStatus::Cancelled:
                return {0.6f, 0.6f, 0.6f, 1.0f};  // Gray
            case WorkflowStatus::Skipped:
                return {0.7f, 0.5f, 0.9f, 1.0f};  // Purple
            case WorkflowStatus::Completed:
                return {0.2f, 0.8f, 0.8f, 1.0f};  // Cyan
            default:
                return {0.5f, 0.5f, 0.5f, 1.0f};  // Dark gray
        }
    }

    const char* WorkflowRun::get_status_icon(WorkflowStatus status) {
        switch (status) {
            case WorkflowStatus::Unknown:
                return ICON_MD_HELP;
            case WorkflowStatus::Success:
                return ICON_MD_CHECK_CIRCLE;
            case WorkflowStatus::Failed:
                return ICON_MD_ERROR;
            case WorkflowStatus::InProgress:
                return ICON_MD_SYNC;
            case WorkflowStatus::Queued:
                return ICON_MD_SCHEDULE;
            case WorkflowStatus::Cancelled:
                return ICON_MD_CANCEL;
            case WorkflowStatus::Skipped:
                return ICON_MD_SKIP_NEXT;
            case WorkflowStatus::Completed:
                return ICON_MD_DONE_ALL;
            default:
                return ICON_MD_HELP;
        }
    }

    const char* WorkflowRun::get_status_text(WorkflowStatus status) {
        switch (status) {
            case WorkflowStatus::Unknown:
                return "Unknown";
            case WorkflowStatus::Success:
                return "Success";
            case WorkflowStatus::Failed:
                return "Failed";
            case WorkflowStatus::InProgress:
                return "In Progress";
            case WorkflowStatus::Queued:
                return "Queued";
            case WorkflowStatus::Cancelled:
                return "Cancelled";
            case WorkflowStatus::Skipped:
                return "Skipped";
            case WorkflowStatus::Completed:
                return "Completed";
            default:
                return "Unknown";
        }
    }

    Workflow Workflow::from_json(const glz::json_t& json) {
        Workflow workflow;
        
        try {
            if (json.contains("id")) {
                workflow.id = std::to_string(static_cast<int64_t>(json["id"].get<double>()));
            }
            if (json.contains("name")) {
                workflow.name = json["name"].get<std::string>();
            }
            if (json.contains("path")) {
                workflow.path = json["path"].get<std::string>();
            }
            if (json.contains("state")) {
                workflow.state = json["state"].get<std::string>();
            }
            if (json.contains("badge_url")) {
                workflow.badge_url = json["badge_url"].get<std::string>();
            }
            if (json.contains("html_url")) {
                workflow.html_url = json["html_url"].get<std::string>();
            }
        } catch (const std::exception& e) {
            // Handle JSON parsing errors gracefully
            (void)e;
        }
        
        return workflow;
    }

    // Helper to show temporary toast message
    void github_ci_card::show_toast(const std::string& message, bool is_error) {
        toast_message_ = message;
        toast_time_ = std::chrono::steady_clock::now();
        toast_is_error_ = is_error;
    }

    void github_ci_card::trigger_workflow_run(const std::string& workflow_id, const std::string& branch) {
        if (selected_repo_full_name_.empty()) return;
        
        std::string target_branch = branch;
        if (target_branch.empty()) {
            target_branch = "main";
            auto workflow_it = std::find_if(workflows_.begin(), workflows_.end(),
                [&workflow_id](const Workflow& w) { return w.id == workflow_id; });
                
            if (workflow_it != workflows_.end() && !workflow_it->recent_runs.empty()) {
                target_branch = workflow_it->recent_runs[0].branch;
            }
        }
        
        if (host_->dispatch_workflow(selected_repo_full_name_, workflow_id, target_branch)) {
            show_toast(std::format("Triggered workflow on branch '{}'", target_branch), false);
            // Schedule a refresh
            last_refresh_ = std::chrono::steady_clock::now() - refresh_interval_ + std::chrono::seconds(2);
        } else {
            show_toast("Failed to trigger workflow", true);
        }
    }

    void github_ci_card::rerun_workflow_run(const std::string& run_id) {
        if (selected_repo_full_name_.empty() || run_id.empty()) return;
        if (!login_host_ || login_host_->personal_token().empty()) return;
        try {
            std::string url = std::format("https://api.github.com/repos/{}/actions/runs/{}/rerun-failed-jobs",
                                          selected_repo_full_name_, run_id);
            http::fetch fetcher;
            fetcher.post(url, "{}", host_->header_client());
            show_toast(std::format("Requested rerun for run #{}", run_id), false);
        } catch (const std::exception& e) {
            show_toast(std::format("Failed to rerun run #{}: {}", run_id, e.what()), true);
        }
    }

    void github_ci_card::cancel_workflow_run(const std::string& run_id) {
        if (selected_repo_full_name_.empty() || run_id.empty()) return;
        if (!login_host_ || login_host_->personal_token().empty()) return;
        try {
            std::string url = std::format("https://api.github.com/repos/{}/actions/runs/{}/cancel",
                                          selected_repo_full_name_, run_id);
            http::fetch fetcher;
            fetcher.post(url, "{}", host_->header_client());
            show_toast(std::format("Cancelled run #{}", run_id), false);
        } catch (const std::exception& e) {
            show_toast(std::format("Failed to cancel run #{}: {}", run_id, e.what()), true);
        }
    }

    github_ci_card::github_ci_card(std::string_view config_name) 
        : config_name_(config_name) {
        if (config_name_.contains('/')) {
            selected_repo_full_name_ = config_name_;
            config_name_ = "default";
        }
        
        // Set card colors - GitHub CI theme
        colors[0] = {0.13f, 0.37f, 0.71f, 1.0f}; // GitHub blue
        colors[1] = {0.0f, 0.8f, 0.2f, 1.0f};    // Success green
        colors[2] = {0.9f, 0.2f, 0.2f, 1.0f};    // Failure red
        colors[3] = {0.9f, 0.7f, 0.2f, 1.0f};    // Warning yellow
        colors[4] = {0.2f, 0.6f, 0.9f, 1.0f};    // In progress blue
        colors[5] = {0.6f, 0.6f, 0.6f, 1.0f};    // Neutral gray
        
        name("GitHub CI/CD");
        width = 900.0f; // Wider for better workflow visualization
        
        // Initialize GitHub integration
        try {
            login_host_ = registrar::get<models::github::login_host>(std::string(config_name));
        } catch (std::exception const &) {
            login_host_ = std::make_shared<models::github::login_host>();
        }
        
        host_ = std::make_shared<models::github::host>();
        host_->set_login_host(login_host_);
        
        last_refresh_ = std::chrono::steady_clock::now() - refresh_interval_;
    }

    std::string github_ci_card::get_uri() const {
        if (config_name_ == "default") {
            return "github-ci";
        }
        return std::format("github-ci:{}", config_name_);
    }

    bool github_ci_card::render() {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool github_ci_card::render(rouen::ui::ui_context& ui) {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

            apply_pending_fetch();

            if (login_host_->personal_token().empty()) {
                ImGui::TextColored(colors[2], ICON_MD_WARNING " GitHub token not configured");
                ImGui::Text("Please configure GitHub integration first.");
                if (ImGui::Button("Open GitHub Settings##ci_open_settings")) {
                    // Could trigger opening the main GitHub card
                }
                return;
            }
            
            if (!initialized_) {
                if (selected_repo_full_name_.empty()) {
                    fetch_repositories();
                } else {
                    fetch_workflows_for_repo(selected_repo_full_name_);
                }
                initialized_ = true;
            }
            
            auto_refresh_if_needed();
            
            // Main UI tabs
            if (ImGui::BeginTabBar("GitHubCITabs")) {
                if (ImGui::BeginTabItem(ICON_MD_DASHBOARD " Overview")) {
                    render_repository_selector();
                    ImGui::Separator();
                    render_workflows_overview();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem(ICON_MD_TIMELINE " Timeline")) {
                    render_timeline_view();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem(ICON_MD_BUG_REPORT " Diagnostics")) {
                    render_ci_diagnostics();
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
            
            // Error display
            if (!last_error_.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto error_age = std::chrono::duration_cast<std::chrono::seconds>(now - error_time_).count();
                if (error_age < 10) { // Show error for 10 seconds
                    ImGui::Separator();
                    ImGui::TextColored(colors[2], ICON_MD_ERROR " %s", last_error_.c_str());
                } else {
                    last_error_.clear();
                }
            }

            // Toast display
            if (!toast_message_.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto toast_age = std::chrono::duration_cast<std::chrono::seconds>(now - toast_time_).count();
                if (toast_age < 5) { // Show toast for 5 seconds
                    ImGui::SetCursorPos(ImVec2(20, ImGui::GetWindowHeight() - 40));
                    ImGui::PushStyleColor(ImGuiCol_Text, toast_is_error_ ? colors[2] : colors[1]);
                    ImGui::Text("%s %s", toast_is_error_ ? ICON_MD_ERROR : ICON_MD_CHECK_CIRCLE, toast_message_.c_str());
                    ImGui::PopStyleColor();
                } else {
                    toast_message_.clear();
                }
            }
        });
    }

    void github_ci_card::render_repository_selector() {
        ImGui::TextColored(colors[0], ICON_MD_FOLDER " Repository");
        
        // Repository filter
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputTextWithHint("##ci_repo_filter", "Filter repositories...", 
                                repo_filter_, sizeof(repo_filter_));
        
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_REFRESH " Refresh##ci_refresh_repos")) {
            fetch_repositories();
        }

        if (loading_repositories_) {
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "Loading repositories...");
        }
        
        // Repository selection
        if (ImGui::BeginCombo("##ci_repository_selector", selected_repo_full_name_.empty() ? 
                             "Select a repository..." : selected_repo_full_name_.c_str())) {
            
            for (size_t i = 0; i < repositories_.size(); ++i) {
                const auto& repo = repositories_[i];
                if (repo_filter_[0] != '\0' && 
                    repo.find(repo_filter_) == std::string::npos) {
                    continue;
                }
                
                ImGui::PushID(static_cast<int>(i)); // Unique ID per repository
                
                bool const is_selected = (repo == selected_repo_full_name_);
                if (ImGui::Selectable(repo.c_str(), is_selected)) {
                    selected_repo_full_name_ = repo;
                    selected_workflow_id_.clear();
                    selected_run_id_.clear();
                    workflow_runs_.clear();
                    run_jobs_.clear();
                    fetch_workflows_for_repo(repo);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        
        // Auto-refresh toggle
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto-refresh##ci_auto_refresh", &auto_refresh_enabled_)) {
            if (auto_refresh_enabled_) {
                last_refresh_ = std::chrono::steady_clock::now();
            }
        }
    }

    void github_ci_card::render_workflows_overview() {
        if (selected_repo_full_name_.empty()) {
            ImGui::TextColored(colors[5], "Select a repository to view workflows");
            return;
        }

        if (loading_workflows_) {
            ImGui::TextColored(colors[4], "Loading workflows for %s...", selected_repo_full_name_.c_str());
            return;
        }
        
        if (workflows_.empty()) {
            ImGui::TextColored(colors[5], "No workflows found for this repository");
            return;
        }
        
        ImGui::TextColored(colors[0], ICON_MD_SETTINGS " Workflows");
        
        // Workflows table
        if (ImGui::BeginTable("ci_workflows_table", 4)) {
            
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Workflow");
            ImGui::TableSetupColumn("Latest Run");
            ImGui::TableSetupColumn("Actions");
            ImGui::TableHeadersRow();
            
            for (size_t i = 0; i < workflows_.size(); ++i) {
                const auto& workflow = workflows_[i];
                ImGui::TableNextRow();
                
                ImGui::PushID(static_cast<int>(i)); // Unique ID per workflow row
                
                // Status column
                ImGui::TableNextColumn();
                render_workflow_status_indicator(workflow);
                
                // Workflow name column
                ImGui::TableNextColumn();
                ImGui::Text("%s", workflow.name.c_str());
                ImGui::TextColored(colors[5], "%s", workflow.path.c_str());
                
                // Latest run column
                ImGui::TableNextColumn();
                if (!workflow.recent_runs.empty()) {
                    const auto& latest_run = workflow.recent_runs[0];
                    render_run_status_badge(latest_run);
                    ImGui::Text("#%d", latest_run.run_number);
                } else {
                    ImGui::TextColored(colors[5], "No runs");
                }
                
                // Actions column
                ImGui::TableNextColumn();
                
                if (ImGui::SmallButton(ICON_MD_PLAY_ARROW " Runs##workflow_runs")) {
                    fetch_workflow_runs(workflow.id);
                    selected_workflow_id_ = workflow.id;
                }
                
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_PLAY_CIRCLE_FILLED " Run##workflow_trigger")) {
                    trigger_workflow_run(workflow.id);
                }

                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " View##workflow_view")) {
                    host_->open_url(workflow.html_url);
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
        
        // Show selected workflow details
        if (!selected_workflow_id_.empty()) {
            auto workflow_it = std::find_if(workflows_.begin(), workflows_.end(),
                [this](const Workflow& w) { return w.id == selected_workflow_id_; });
            
            if (workflow_it != workflows_.end()) {
                ImGui::Separator();
                render_workflow_details(*workflow_it);
            }
        }
    }

    void github_ci_card::render_workflow_details(const Workflow& workflow) {
        ImGui::TextColored(colors[0], ICON_MD_SETTINGS " %s", workflow.name.c_str());
        
        auto runs_it = workflow_runs_.find(workflow.id);
        if (loading_runs_ && selected_workflow_id_ == workflow.id && (runs_it == workflow_runs_.end() || runs_it->second.empty())) {
            ImGui::TextColored(colors[4], "Loading workflow runs...");
            return;
        }
        if (runs_it == workflow_runs_.end() || runs_it->second.empty()) {
            ImGui::Text("Click 'Runs' to load workflow runs");
            return;
        }
        
        const auto& runs = runs_it->second;
        
        // Filter controls
        ImGui::Checkbox("Show all runs##ci_show_all_runs", &show_all_runs_);
        
        // Runs table
        if (ImGui::BeginTable("ci_workflow_runs_table", 6)) {
            
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Run");
            ImGui::TableSetupColumn("Branch");
            ImGui::TableSetupColumn("Commit");
            ImGui::TableSetupColumn("Author");
            ImGui::TableSetupColumn("Actions");
            ImGui::TableHeadersRow();
            
            int displayed_runs = 0;
            const int max_runs = show_all_runs_ ? 50 : 10;
            
            for (size_t i = 0; i < runs.size() && displayed_runs < max_runs; ++i) {
                const auto& run = runs[i];
                
                ImGui::TableNextRow();
                
                ImGui::PushID(static_cast<int>(i)); // Unique ID per run row
                
                // Status
                ImGui::TableNextColumn();
                render_run_status_badge(run);
                
                // Run number
                ImGui::TableNextColumn();
                ImGui::Text("#%d", run.run_number);
                
                // Branch
                ImGui::TableNextColumn();
                ImGui::Text("%s", run.branch.c_str());
                
                // Commit
                ImGui::TableNextColumn();
                ImGui::Text("%s", run.commit_sha.c_str());
                
                // Author
                ImGui::TableNextColumn();
                ImGui::Text("%s", run.author.c_str());
                
                // Actions
                ImGui::TableNextColumn();
                
                if (ImGui::SmallButton(ICON_MD_INFO " Details##run_details")) {
                    selected_run_id_ = run.id;
                    fetch_workflow_jobs(run.id);
                }
                
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " View##run_view")) {
                    host_->open_url(run.html_url);
                }
                
                // Show commit message as tooltip
                if (ImGui::IsItemHovered() && !run.commit_message.empty()) {
                    ImGui::SetTooltip("%s", run.commit_message.c_str());
                }
                
                ImGui::PopID();
                displayed_runs++;
            }
            
            ImGui::EndTable();
        }
        
        // Show selected run details
        if (!selected_run_id_.empty()) {
            auto run_it = std::find_if(runs.begin(), runs.end(),
                [this](const WorkflowRun& r) { return r.id == selected_run_id_; });
            
            if (run_it != runs.end()) {
                ImGui::Separator();
                render_run_details(*run_it);
            }
        }
    }

    void github_ci_card::render_run_details(const WorkflowRun& run) {
        ImGui::TextColored(colors[0], ICON_MD_PLAY_ARROW " Run #%d", run.run_number);
        
        // Run info
        ImGui::Text("Workflow: %s", run.workflow_name.c_str());
        ImGui::Text("Branch: %s", run.branch.c_str());
        ImGui::Text("Commit: %s - %s", run.commit_sha.c_str(), run.commit_message.c_str());
        ImGui::Text("Author: %s", run.author.c_str());
        ImGui::Text("Event: %s", run.event_type.c_str());
        
        // Status with color
        ImVec4 const status_color = WorkflowRun::get_status_color(run.status);
        const char* status_icon = WorkflowRun::get_status_icon(run.status);
        const char* status_text = WorkflowRun::get_status_text(run.status);
        
        ImGui::TextColored(status_color, "%s %s", status_icon, status_text);
        
        // Jobs information
        auto jobs_it = run_jobs_.find(run.id);
        if (jobs_it != run_jobs_.end()) {
            ImGui::Separator();
            ImGui::Text("Jobs:");
            
            // Render jobs information here
            json_view_.render(jobs_it->second);
        } else if (loading_jobs_ && selected_run_id_ == run.id) {
            ImGui::Separator();
            ImGui::TextColored(colors[4], "Loading jobs...");
        }
        
        // Action buttons
        if (ImGui::Button(ICON_MD_REFRESH " Refresh Jobs##ci_refresh_jobs")) {
            fetch_workflow_jobs(run.id);
        }
        
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_ASSIGNMENT " View Logs##ci_view_logs")) {
            render_logs_preview(run);
        }
        
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_OPEN_IN_BROWSER " Open in GitHub##ci_open_github")) {
            host_->open_url(run.html_url);
        }
    }

    void github_ci_card::render_workflow_status_indicator(const Workflow& workflow) {
        ImVec4 const color = WorkflowRun::get_status_color(workflow.latest_status);
        const char* icon = WorkflowRun::get_status_icon(workflow.latest_status);
        
        ImGui::TextColored(color, "%s", icon);
    }

    void github_ci_card::render_run_status_badge(const WorkflowRun& run) {
        ImVec4 const color = WorkflowRun::get_status_color(run.status);
        const char* icon = WorkflowRun::get_status_icon(run.status);
        
        ImGui::TextColored(color, "%s", icon);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", WorkflowRun::get_status_text(run.status));
        }
    }

    void github_ci_card::render_timeline_view() {
        ImGui::TextColored(colors[0], ICON_MD_TIMELINE " CI/CD Timeline");
        
        if (selected_repo_full_name_.empty()) {
            ImGui::Text("Select a repository to view timeline");
            return;
        }
        
        // Collect all runs from all workflows for timeline
        std::vector<WorkflowRun> all_runs;
        for (const auto& [workflow_id, runs] : workflow_runs_) {
            all_runs.insert(all_runs.end(), runs.begin(), runs.end());
        }
        
        // Sort by creation time (most recent first)
        std::sort(all_runs.begin(), all_runs.end(), 
            [](const WorkflowRun& a, const WorkflowRun& b) {
                return a.created_at > b.created_at;
            });
        
        // Render timeline
        if (ImGui::BeginChild("ci_timeline", ImVec2(0, 0), true)) {
            for (const auto& run : all_runs) {
                ImGui::PushID(("timeline_" + run.id).c_str());
                
                // Status indicator
                render_run_status_badge(run);
                ImGui::SameLine();
                
                // Run info
                ImGui::Text("%s #%d", run.workflow_name.c_str(), run.run_number);
                ImGui::SameLine();
                ImGui::TextColored(colors[5], "(%s)", run.branch.c_str());
                
                // Commit and time info
                ImGui::Indent();
                ImGui::Text("%s - %s", run.commit_sha.c_str(), run.commit_message.c_str());
                ImGui::TextColored(colors[5], "by %s", run.author.c_str());
                ImGui::Unindent();
                
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void github_ci_card::render_ci_diagnostics() {
        ImGui::TextColored(colors[0], ICON_MD_BUG_REPORT " CI/CD Diagnostics");
        
        // System status
        ImGui::Text("GitHub API Status: %s", 
                   login_host_->personal_token().empty() ? "Not configured" : "Connected");
        
        if (!selected_repo_full_name_.empty()) {
            ImGui::Text("Selected Repository: %s", selected_repo_full_name_.c_str());
            ImGui::Text("Workflows Loaded: %zu", workflows_.size());
            ImGui::Text("Total Runs Cached: %zu", workflow_runs_.size());
        }
        
        // Refresh status
        auto now = std::chrono::steady_clock::now();
        const auto seconds_since_refresh = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_refresh_).count();
        ImGui::Text("Last Refresh: %lld seconds ago", static_cast<long long>(seconds_since_refresh));
        ImGui::Text("Auto-refresh: %s", auto_refresh_enabled_ ? "Enabled" : "Disabled");
        
        // Error log
        if (!last_error_.empty()) {
            ImGui::Separator();
            ImGui::TextColored(colors[2], "Last Error:");
            ImGui::TextWrapped("%s", last_error_.c_str());
        }
        
        // Configuration info
        ImGui::Separator();
        ImGui::Text("Configuration:");
        ImGui::Text("  Config Name: %s", config_name_.c_str());
        ImGui::Text("  Refresh Interval: %lld seconds", static_cast<long long>(refresh_interval_.count()));
        ImGui::Text("  Card Width: %.0f", static_cast<double>(width));
    }

    void github_ci_card::render_logs_preview(const WorkflowRun& run) {
        // This would open a separate window or expand to show log preview
        // For now, just open in browser
        host_->open_url(run.html_url);
    }

    void github_ci_card::fetch_repositories() {
        if (has_pending_fetch()) {
            return;
        }

        loading_repositories_ = true;
        auto host = host_;
        pending_fetch_ = std::async(std::launch::async, [host]() {
            FetchResult result;
            result.kind = FetchKind::Repositories;
            try {
                auto repos_json = host->user_repos();
                if (repos_json.is_array()) {
                    for (const auto& repo : repos_json.get<std::vector<glz::json_t>>()) {
                        if (repo.contains("full_name")) {
                            result.repositories.push_back(repo["full_name"].get<std::string>());
                        }
                    }
                }
            } catch (const std::exception& e) {
                result.error = std::format("Failed to fetch repositories: {}", e.what());
            }
            return result;
        });
    }

    void github_ci_card::fetch_workflows_for_repo(const std::string& repo_full_name) {
        if (has_pending_fetch()) {
            return;
        }

        loading_workflows_ = true;
        auto host = host_;
        pending_fetch_ = std::async(std::launch::async, [host, repo_full_name]() noexcept { // NOLINT(bugprone-exception-escape)
            try {
                FetchResult result;
                result.kind = FetchKind::Workflows;
                result.repo_full_name = repo_full_name;
                try {
                    auto workflows_json = host->repo_workflows(repo_full_name);
                    if (workflows_json.contains("workflows") && workflows_json["workflows"].is_array()) {
                        for (const auto& workflow_json : workflows_json["workflows"].get<std::vector<glz::json_t>>()) {
                            result.workflows.push_back(Workflow::from_json(workflow_json));
                        }
                    }
                } catch (const std::exception& e) {
                    result.error = std::format("Failed to fetch workflows: {}", e.what());
                }
                return result;
            } catch (...) {
                FetchResult result;
                result.kind = FetchKind::Workflows;
                result.repo_full_name = repo_full_name;
                result.error = "Fatal unknown exception occurred.";
                return result;
            }
        });
    }

    void github_ci_card::fetch_workflow_runs(const std::string& workflow_id) {
        if (has_pending_fetch() || selected_repo_full_name_.empty()) {
            return;
        }

        loading_runs_ = true;
        auto host = host_;
        auto repo_full_name = selected_repo_full_name_;
        pending_fetch_ = std::async(std::launch::async, [host, repo_full_name, workflow_id]() noexcept { // NOLINT(bugprone-exception-escape)
            try {
                FetchResult result;
                result.kind = FetchKind::Runs;
                result.repo_full_name = repo_full_name;
                result.workflow_id = workflow_id;
                try {
                    std::string const runs_url = std::format(
                        "https://api.github.com/repos/{}/actions/workflows/{}/runs",
                        repo_full_name, workflow_id);

                    auto runs_json = host->fetch(runs_url);
                    if (runs_json.contains("workflow_runs") && runs_json["workflow_runs"].is_array()) {
                        for (const auto& run_json : runs_json["workflow_runs"].get<std::vector<glz::json_t>>()) {
                            result.runs.push_back(WorkflowRun::from_json(run_json));
                        }
                    }
                } catch (const std::exception& e) {
                    result.error = std::format("Failed to fetch workflow runs: {}", e.what());
                }
                return result;
            } catch (...) {
                FetchResult result;
                result.kind = FetchKind::Runs;
                result.repo_full_name = repo_full_name;
                result.workflow_id = workflow_id;
                result.error = "Fatal unknown exception occurred.";
                return result;
            }
        });
    }

    void github_ci_card::fetch_workflow_jobs(const std::string& run_id) {
        if (has_pending_fetch() || selected_repo_full_name_.empty()) {
            return;
        }

        loading_jobs_ = true;
        auto host = host_;
        auto repo_full_name = selected_repo_full_name_;
        pending_fetch_ = std::async(std::launch::async, [host, repo_full_name, run_id]() noexcept { // NOLINT(bugprone-exception-escape)
            try {
                FetchResult result;
                result.kind = FetchKind::Jobs;
                result.repo_full_name = repo_full_name;
                result.run_id = run_id;
                try {
                    std::string const jobs_url = std::format(
                        "https://api.github.com/repos/{}/actions/runs/{}/jobs",
                        repo_full_name, run_id);
                    result.jobs = host->fetch(jobs_url);
                } catch (const std::exception& e) {
                    result.error = std::format("Failed to fetch workflow jobs: {}", e.what());
                }
                return result;
            } catch (...) {
                FetchResult result;
                result.kind = FetchKind::Jobs;
                result.repo_full_name = repo_full_name;
                result.run_id = run_id;
                result.error = "Fatal unknown exception occurred.";
                return result;
            }
        });
    }

    void github_ci_card::auto_refresh_if_needed() {
        if (!auto_refresh_enabled_ || has_pending_fetch()) return;
        
        auto now = std::chrono::steady_clock::now();
        if (now - last_refresh_ >= refresh_interval_) {
            if (!selected_repo_full_name_.empty()) {
                fetch_workflows_for_repo(selected_repo_full_name_);
                
                // Refresh active workflow runs
                if (!selected_workflow_id_.empty()) {
                    fetch_workflow_runs(selected_workflow_id_);
                }
            }
            
            last_refresh_ = now;
        }
    }

    bool github_ci_card::should_auto_refresh() const {
        auto now = std::chrono::steady_clock::now();
        return auto_refresh_enabled_ && (now - last_refresh_ >= refresh_interval_);
    }

    bool github_ci_card::has_pending_fetch() const {
        if (!pending_fetch_.valid()) {
            return false;
        }
        return pending_fetch_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }

    void github_ci_card::clear_loading_flags() {
        loading_repositories_ = false;
        loading_workflows_ = false;
        loading_runs_ = false;
        loading_jobs_ = false;
    }

    void github_ci_card::apply_pending_fetch() {
        if (!pending_fetch_.valid()) {
            return;
        }

        if (pending_fetch_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }

        FetchResult result = pending_fetch_.get();
        clear_loading_flags();

        if (!result.error.empty()) {
            last_error_ = result.error;
            error_time_ = std::chrono::steady_clock::now();
            return;
        }

        switch (result.kind) {
            case FetchKind::Repositories:
                repositories_ = std::move(result.repositories);
                break;
            case FetchKind::Workflows:
                selected_repo_full_name_ = result.repo_full_name;
                workflows_ = std::move(result.workflows);
                workflow_runs_.clear();
                run_jobs_.clear();
                selected_workflow_id_.clear();
                selected_run_id_.clear();
                break;
            case FetchKind::Runs: {
                workflow_runs_[result.workflow_id] = result.runs;
                auto workflow_it = std::find_if(workflows_.begin(), workflows_.end(),
                    [&result](const Workflow& w) { return w.id == result.workflow_id; });
                if (workflow_it != workflows_.end() && !result.runs.empty()) {
                    workflow_it->latest_status = result.runs.front().status;
                    workflow_it->recent_runs = result.runs;
                }
                break;
            }
            case FetchKind::Jobs:
                run_jobs_[result.run_id] = std::move(result.jobs);
                break;
            case FetchKind::None:
                break;
        }

        last_error_.clear();
    }

    std::string github_ci_card::get_adaptive_card_json() const {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";
        card["refreshIntervalMs"] = 10000;

        std::vector<glz::json_t> body;

        std::string repo_slug = selected_repo_full_name_.empty() ? "ignacionr/rouen" : selected_repo_full_name_;

        // Determine status and latest run if available
        const WorkflowRun* latest_run = nullptr;
        for (const auto& w : workflows_) {
            if (!w.recent_runs.empty()) {
                if (!latest_run || w.recent_runs[0].created_at > latest_run->created_at) {
                    latest_run = &w.recent_runs[0];
                }
            }
        }
        if (!latest_run) {
            for (const auto& [wf_id, runs] : workflow_runs_) {
                if (!runs.empty()) {
                    if (!latest_run || runs[0].created_at > latest_run->created_at) {
                        latest_run = &runs[0];
                    }
                }
            }
        }

        std::string status_badge_text = "PASSING";
        std::string status_badge_color = "Good";

        if (latest_run) {
            switch (latest_run->status) {
                case WorkflowStatus::Success:
                    status_badge_text = "PASSING";
                    status_badge_color = "Good";
                    break;
                case WorkflowStatus::Failed:
                    status_badge_text = "FAILING";
                    status_badge_color = "Attention";
                    break;
                case WorkflowStatus::InProgress:
                    status_badge_text = "RUNNING";
                    status_badge_color = "Accent";
                    break;
                case WorkflowStatus::Queued:
                    status_badge_text = "QUEUED";
                    status_badge_color = "Warning";
                    break;
                case WorkflowStatus::Cancelled:
                    status_badge_text = "CANCELLED";
                    status_badge_color = "Attention";
                    break;
                case WorkflowStatus::Skipped:
                    status_badge_text = "SKIPPED";
                    status_badge_color = "Default";
                    break;
                case WorkflowStatus::Completed:
                    status_badge_text = "PASSING";
                    status_badge_color = "Good";
                    break;
                case WorkflowStatus::Unknown:
                default:
                    status_badge_text = "PASSING";
                    status_badge_color = "Good";
                    break;
            }
        }

        // 1. Header Container
        {
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Col 1: emoji / lightning
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "\u26a1"; // ⚡
                tb["size"] = "Large";
                items.push_back(std::move(tb));
                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Col 2: Title and Subtitle
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = "GitHub Actions CI Telemetry";
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = std::format("{}: workflows and status checks", repo_slug);
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                tb_sub["spacing"] = "None";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Col 3: Status Badge
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                glz::json_t tb_status;
                tb_status["type"] = "TextBlock";
                tb_status["text"] = status_badge_text;
                tb_status["weight"] = "Bolder";
                tb_status["color"] = status_badge_color;
                tb_status["size"] = "Medium";
                items.push_back(std::move(tb_status));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header_container));
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            std::string latest_build_val = latest_run
                ? std::format("Run #{}: {} ({})", latest_run->run_number, latest_run->name.empty() ? latest_run->workflow_name : latest_run->name, latest_run->branch)
                : "Run #142: macOS arm64 + Windows x64";

            std::string commit_sha_val = latest_run && !latest_run->commit_sha.empty()
                ? std::format("{} ({})", latest_run->commit_sha, latest_run->branch)
                : "a7f3e1b (main)";

            std::string duration_val = "3m 42s";

            std::string status_val = latest_run
                ? (latest_run->conclusion.empty() ? WorkflowRun::get_status_text(latest_run->status) : std::format("Completed: {}", latest_run->conclusion))
                : "Completed successfully (2/2 jobs green)";

            {
                glz::json_t f;
                f["title"] = "Latest Build:";
                f["value"] = latest_build_val;
                facts.push_back(std::move(f));
            }
            {
                glz::json_t f;
                f["title"] = "Commit SHA:";
                f["value"] = commit_sha_val;
                facts.push_back(std::move(f));
            }
            {
                glz::json_t f;
                f["title"] = "Duration:";
                f["value"] = duration_val;
                facts.push_back(std::move(f));
            }
            {
                glz::json_t f;
                f["title"] = "Status:";
                f["value"] = status_val;
                facts.push_back(std::move(f));
            }

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Manual Workflow Dispatch Container
        {
            glz::json_t dispatch_container;
            dispatch_container["type"] = "Container";
            dispatch_container["separator"] = true;
            std::vector<glz::json_t> items;

            {
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "Manual Workflow Dispatch";
                tb["weight"] = "Bolder";
                items.push_back(std::move(tb));
            }

            // Choices
            glz::json_t choice_input;
            choice_input["type"] = "Input.ChoiceSet";
            choice_input["id"] = "workflow_choice";
            choice_input["label"] = "Select Workflow";

            std::vector<glz::json_t> choices;
            if (!workflows_.empty()) {
                for (const auto& w : workflows_) {
                    std::string title = w.name;
                    std::string val = w.path.empty() ? w.id : std::filesystem::path(w.path).filename().string();
                    if (!w.path.empty()) {
                        title = std::format("{} ({})", w.name, val);
                    }
                    glz::json_t c;
                    c["title"] = title;
                    c["value"] = val;
                    choices.push_back(std::move(c));
                }
            }
            if (choices.empty()) {
                {
                    glz::json_t c;
                    c["title"] = "Continuous Integration (ci.yml)";
                    c["value"] = "ci.yml";
                    choices.push_back(std::move(c));
                }
                {
                    glz::json_t c;
                    c["title"] = "Release Build & Pack (release.yml)";
                    c["value"] = "release.yml";
                    choices.push_back(std::move(c));
                }
                {
                    glz::json_t c;
                    c["title"] = "Windows Headless Tests (windows.yml)";
                    c["value"] = "windows.yml";
                    choices.push_back(std::move(c));
                }
            }

            std::string default_choice = choices.empty() ? "ci.yml" : choices[0]["value"].get<std::string>();
            choice_input["value"] = default_choice;
            choice_input["choices"] = std::move(choices);
            items.push_back(std::move(choice_input));

            // Target branch
            {
                glz::json_t input_branch;
                input_branch["type"] = "Input.Text";
                input_branch["id"] = "target_branch";
                input_branch["label"] = "Target Branch";
                input_branch["value"] = last_target_branch_.empty() ? "main" : last_target_branch_;
                items.push_back(std::move(input_branch));
            }

            dispatch_container["items"] = std::move(items);
            body.push_back(std::move(dispatch_container));
        }

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Trigger Workflow";
            act["verb"] = "trigger_workflow";
            actions.push_back(std::move(act));
        }
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Rerun Failed Jobs";
            act["verb"] = "rerun_run";
            actions.push_back(std::move(act));
        }
        {
            glz::json_t act;
            act["type"] = "Action.Execute";
            act["title"] = "Refresh CI Status";
            act["verb"] = "refresh_ci";
            actions.push_back(std::move(act));
        }

        card["actions"] = std::move(actions);

        std::string out;
        auto ec = glz::write_json(card, out);
        if (ec) {
            return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"body\":[]}";
        }
        return out;
    }

    void github_ci_card::handle_action(std::string_view action_json) {
        if (action_json.empty()) return;
        try {
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb.empty()) return;

            last_action_ = verb;

            if (verb == "trigger_workflow") {
                std::string workflow = "ci.yml";
                std::string branch = "main";
                if (data.is_object()) {
                    if (data.contains("workflow_choice") && data["workflow_choice"].holds<std::string>()) {
                        workflow = data["workflow_choice"].get<std::string>();
                    } else if (data.contains("workflow_id") && data["workflow_id"].holds<std::string>()) {
                        workflow = data["workflow_id"].get<std::string>();
                    } else if (data.contains("workflow") && data["workflow"].holds<std::string>()) {
                        workflow = data["workflow"].get<std::string>();
                    }
                    if (data.contains("target_branch") && data["target_branch"].holds<std::string>()) {
                        branch = data["target_branch"].get<std::string>();
                    } else if (data.contains("branch") && data["branch"].holds<std::string>()) {
                        branch = data["branch"].get<std::string>();
                    }
                }
                last_triggered_workflow_ = workflow;
                last_target_branch_ = branch;
                trigger_workflow_run(workflow, branch);
            } else if (verb == "rerun_run") {
                std::string run_id;
                if (data.is_object()) {
                    if (data.contains("run_id") && data["run_id"].holds<std::string>()) {
                        run_id = data["run_id"].get<std::string>();
                    } else if (data.contains("id") && data["id"].holds<std::string>()) {
                        run_id = data["id"].get<std::string>();
                    } else if (data.contains("run_id") && data["run_id"].holds<double>()) {
                        run_id = std::to_string(static_cast<int64_t>(data["run_id"].get<double>()));
                    }
                }
                if (run_id.empty() && !selected_run_id_.empty()) {
                    run_id = selected_run_id_;
                }
                last_rerun_run_id_ = run_id;
                if (!run_id.empty()) {
                    selected_run_id_ = run_id;
                    rerun_workflow_run(run_id);
                }
            } else if (verb == "cancel_run") {
                std::string run_id;
                if (data.is_object()) {
                    if (data.contains("run_id") && data["run_id"].holds<std::string>()) {
                        run_id = data["run_id"].get<std::string>();
                    } else if (data.contains("id") && data["id"].holds<std::string>()) {
                        run_id = data["id"].get<std::string>();
                    } else if (data.contains("run_id") && data["run_id"].holds<double>()) {
                        run_id = std::to_string(static_cast<int64_t>(data["run_id"].get<double>()));
                    }
                }
                if (run_id.empty() && !selected_run_id_.empty()) {
                    run_id = selected_run_id_;
                }
                last_cancelled_run_id_ = run_id;
                if (!run_id.empty()) {
                    cancel_workflow_run(run_id);
                }
            } else if (verb == "refresh_ci") {
                ci_refreshed_ = true;
                refresh_count_++;
                if (!selected_repo_full_name_.empty()) {
                    fetch_workflows_for_repo(selected_repo_full_name_);
                    if (!selected_workflow_id_.empty()) {
                        fetch_workflow_runs(selected_workflow_id_);
                    }
                } else {
                    fetch_repositories();
                }
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
            }
        } catch (const std::exception& e) {
            std::cerr << "[github-ci] Action error: " << e.what() << std::endl;
        }
    }

} // namespace rouen::cards::github
