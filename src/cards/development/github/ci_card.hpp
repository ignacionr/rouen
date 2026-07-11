#pragma once

#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "../../../helpers/imgui_include.hpp"
#include "../../../helpers/glaze_include.hpp"

#include "../../interface/card.hpp"
#include "../../../models/github/host.hpp"
#include "../../../models/github/login_host.hpp"
#include "../../../registrar.hpp"
#include "../../../helpers/views/json_view.hpp"
#include "../../../../external/IconsMaterialDesign.h"

namespace rouen::cards::github {

    // Enhanced workflow run status representation
    enum class WorkflowStatus {
        Unknown,
        Queued,
        InProgress, 
        Completed,
        Cancelled,
        Failed,
        Success,
        Skipped
    };

    // Enhanced workflow run information
    struct WorkflowRun {
        std::string id;
        std::string name;
        std::string workflow_name;
        std::string branch;
        std::string commit_sha;
        std::string commit_message;
        std::string author;
        WorkflowStatus status;
        std::string conclusion;
        std::string created_at;
        std::string updated_at;
        std::string run_started_at;
        std::string html_url;
        int run_number;
        int run_attempt;
        
        // Additional diagnostic info
        std::vector<std::string> job_names;
        std::map<std::string, std::string> job_statuses;
        bool has_artifacts{false};
        std::string event_type;
        
        static WorkflowRun from_json(const glz::json_t& json);
        static WorkflowStatus parse_status(const std::string& status_str, const std::string& conclusion_str);
        static ImVec4 get_status_color(WorkflowStatus status);
        static const char* get_status_icon(WorkflowStatus status);
        static const char* get_status_text(WorkflowStatus status);
    };

    // Enhanced workflow information
    struct Workflow {
        std::string id;
        std::string name;
        std::string path;
        std::string state;
        std::string badge_url;
        std::string html_url;
        std::vector<WorkflowRun> recent_runs;
        WorkflowStatus latest_status{WorkflowStatus::Unknown};
        
        static Workflow from_json(const glz::json_t& json);
    };

    struct github_ci_card : public card {
        github_ci_card(std::string_view config_name = "default");
        
        std::string get_uri() const override;
        bool render() override;

    private:
        void render_repository_selector();
        void render_workflows_overview();
        void render_workflow_details(const Workflow& workflow);
        void render_run_details(const WorkflowRun& run);
        void render_ci_diagnostics();
        void render_workflow_status_indicator(const Workflow& workflow);
        void render_run_status_badge(const WorkflowRun& run);
        void render_timeline_view();
        void render_logs_preview(const WorkflowRun& run);
        
        // Toast notification
        std::string toast_message_;
        std::chrono::steady_clock::time_point toast_time_;
        bool toast_is_error_{false};
        void show_toast(const std::string& message, bool is_error = false);
        
        void fetch_repositories();
        void fetch_workflows_for_repo(const std::string& repo_full_name);
        void fetch_workflow_runs(const std::string& workflow_id);
        void fetch_workflow_jobs(const std::string& run_id);
        void trigger_workflow_run(const std::string& workflow_id);
        
        void auto_refresh_if_needed();
        bool should_auto_refresh() const;
        void apply_pending_fetch();
        bool has_pending_fetch() const;
        void clear_loading_flags();
        
        // UI state
        std::string selected_repo_full_name_;
        std::string selected_workflow_id_;
        std::string selected_run_id_;
        char repo_filter_[256] = {0}; // Changed to char array for ImGui
        bool show_all_runs_{false};
        bool auto_refresh_enabled_{true};
        [[maybe_unused]] bool show_diagnostics_{false};
        
        // Data
        std::vector<std::string> repositories_;
        std::vector<Workflow> workflows_;
        std::unordered_map<std::string, std::vector<WorkflowRun>> workflow_runs_;
        std::unordered_map<std::string, glz::json_t> run_jobs_;
        
        // GitHub integration
        std::shared_ptr<models::github::host> host_;
        std::shared_ptr<models::github::login_host> login_host_;
        std::string config_name_;
        
        // Refresh timing
        std::chrono::steady_clock::time_point last_refresh_;
        std::chrono::seconds refresh_interval_{30}; // 30 seconds default
        
        // Error handling
        std::string last_error_;
        std::chrono::steady_clock::time_point error_time_;

        enum class FetchKind {
            None,
            Repositories,
            Workflows,
            Runs,
            Jobs
        };

        struct FetchResult {
            FetchKind kind{FetchKind::None};
            std::string repo_full_name;
            std::string workflow_id;
            std::string run_id;
            std::vector<std::string> repositories;
            std::vector<Workflow> workflows;
            std::vector<WorkflowRun> runs;
            glz::json_t jobs;
            std::string error;
        };

        std::future<FetchResult> pending_fetch_;
        bool loading_repositories_{false};
        bool loading_workflows_{false};
        bool loading_runs_{false};
        bool loading_jobs_{false};
        
        // UI helpers
        helpers::views::json_view json_view_;
        bool initialized_{false};
    };

} // namespace rouen::cards::github
