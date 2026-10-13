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
#include "../../../helpers/adaptive_cards/parser.hpp"
#include "../../../helpers/adaptive_cards/renderer.hpp"
#include "../../../helpers/ui_context.hpp"
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
        bool render(rouen::ui::ui_context& ui) override;

        /// Returns the Adaptive Cards 1.5 JSON representation of the card state
        [[nodiscard]] std::string get_adaptive_card_json() const override;

        /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
        void handle_action(std::string_view action_json) override;

        [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
        void set_adaptive_view_mode(bool v) noexcept { adaptive_view_mode = v; }
        [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
        [[nodiscard]] const std::string& get_last_triggered_workflow() const noexcept { return last_triggered_workflow_; }
        [[nodiscard]] const std::string& get_last_target_branch() const noexcept { return last_target_branch_; }
        [[nodiscard]] const std::string& get_last_rerun_run_id() const noexcept { return last_rerun_run_id_; }
        [[nodiscard]] const std::string& get_last_cancelled_run_id() const noexcept { return last_cancelled_run_id_; }
        [[nodiscard]] bool is_ci_refreshed() const noexcept { return ci_refreshed_; }
        [[nodiscard]] int get_refresh_count() const noexcept { return refresh_count_; }

        [[nodiscard]] const std::string& get_selected_repo() const noexcept { return selected_repo_full_name_; }
        void set_selected_repo(std::string_view repo) { selected_repo_full_name_ = repo; }
        [[nodiscard]] const std::string& get_selected_run_id() const noexcept { return selected_run_id_; }
        void set_selected_run_id(std::string_view run_id) { selected_run_id_ = run_id; }
        [[nodiscard]] bool is_polling() const noexcept { return auto_refresh_enabled_; }
        void set_polling(bool polling) noexcept { auto_refresh_enabled_ = polling; }

        [[nodiscard]] const std::vector<Workflow>& get_workflows() const noexcept { return workflows_; }
        void set_workflows(std::vector<Workflow> w) { workflows_ = std::move(w); }

    private:
        void render_repository_selector();
        void render_workflows_overview();
        void render_workflow_details(const Workflow& workflow);
        void render_run_details(const WorkflowRun& run);
        void render_ci_diagnostics();
        static void render_workflow_status_indicator(const Workflow& workflow);
        static void render_run_status_badge(const WorkflowRun& run);
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
        void trigger_workflow_run(const std::string& workflow_id, const std::string& branch = "");
        void rerun_workflow_run(const std::string& run_id);
        void cancel_workflow_run(const std::string& run_id);
        
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

        // Adaptive Card presentation override state
        bool adaptive_view_mode{false};
        std::string last_action_{};
        std::string last_triggered_workflow_{};
        std::string last_target_branch_{};
        std::string last_rerun_run_id_{};
        std::string last_cancelled_run_id_{};
        bool ci_refreshed_{false};
        int refresh_count_{0};

        mutable helpers::adaptive_cards::parser adaptive_parser_{};
        mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
        mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
        mutable helpers::adaptive_cards::card_document adaptive_bound_{};
        std::string adaptive_error_{};
        std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
    };

} // namespace rouen::cards::github

namespace rouen::cards {
    using github::github_ci_card;
    using github::Workflow;
    using github::WorkflowRun;
    using github::WorkflowStatus;
}
