#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../interface/card.hpp"
#include "../../models/git.hpp"
#include "git_overlay.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"

struct file_status_entry {
    std::string path;
    std::string status;
};

struct git : public card {
    std::string repo_status;
    std::shared_ptr<rouen::models::git> git_model;
    std::shared_ptr<std::atomic<bool>> alive_flag_{std::make_shared<std::atomic<bool>>(true)};
    std::string last_commit_message;

    std::atomic<bool> ai_request_pending{false};
    std::string ai_status_cue;
    mutable std::mutex state_mutex;

    std::atomic<bool> status_update_in_progress{false};
    std::chrono::steady_clock::time_point last_status_update{std::chrono::steady_clock::now() - std::chrono::seconds(20)};
    std::string cached_git_remote;
    std::string cached_github_repo_name;
    std::string selected_repo;
    punch_card_info punch_card_data_;
    git_overlay_state overlay_state_;

    // AI Summary State
    std::string last_ai_summary;
    std::chrono::system_clock::time_point last_ai_summary_time{};
    std::string last_ai_summary_repo;
    bool ai_summary_expanded{true};

    // State Synchronization
    mutable std::string repo_path;
    mutable std::string current_branch;
    mutable std::vector<std::string> branches;
    mutable std::vector<file_status_entry> changed_files;
    mutable bool is_clean{true};
    mutable std::string last_action;
    mutable std::string last_staged_file;
    mutable std::string last_checked_out_branch;

    explicit git(std::string_view initial_repo_path = "");
    ~git() override;

    [[nodiscard]] std::string get_effective_repo_path() const;
    void refresh_git_state() const;

    [[nodiscard]] std::string get_uri() const override;
    [[nodiscard]] bool matches_uri(std::string_view uri) const override;
    void handle_uri(std::string_view uri) override;
    [[nodiscard]] std::vector<mcp_function> get_mcp_functions() const override;
    bool render() override;
    bool render(rouen::ui::ui_context& ui) override;

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool v) { adaptive_view_mode = v; }
    [[nodiscard]] const std::string& get_last_action() const { return last_action; }
    [[nodiscard]] const std::string& get_last_staged_file() const { return last_staged_file; }
    [[nodiscard]] const std::string& get_last_checked_out_branch() const { return last_checked_out_branch; }
    [[nodiscard]] const std::string& get_current_branch() const { return current_branch; }
    [[nodiscard]] const std::vector<std::string>& get_branches() const { return branches; }
    [[nodiscard]] const std::vector<file_status_entry>& get_changed_files() const { return changed_files; }
    [[nodiscard]] bool get_is_clean() const { return is_clean; }

    bool has_video_overlay() const override { return true; }
    void render_video_ui() override;

    void trigger_async_status_update();
    void fetch_punch_card_async(const std::string& target_repo_path);
    void fetch_recent_commits_7d_async(const std::string& target_repo_path);
    void fetch_commit_detail_async(const std::string& target_repo_path, const std::string& commit_hash);

    bool select(const std::string& target_repo_path);
    void back_to_list();
    bool updateRepoStatusSync();
    bool updateRepoStatus();

    void render_ai_busy_cue();
    void render_ai_summary_box();
    void render_selected();
    void render_index();
    void render_github_status_indicator();

    void render_punch_card();
    void render_punch_card_matrix(const punch_card_info& data);
    void render_punch_card_heatmap(const punch_card_info& data);
    void render_recent_commits_7d();

    static std::string extract_github_repo_name(const std::string& remote_url);

    bool show_commit_dialog_{false};
    char commit_message_buf_[2048] = "";
    void open_conventional_commit_dialog();
    void commit_with_reviewed_message();
    void render_conventional_commit_modal();

private:
    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable rouen::helpers::adaptive_cards::parser adaptive_parser_{};
    mutable rouen::helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable rouen::helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable rouen::helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    void prepend_action_result(const std::string& action_name, const std::string& command_output);
    std::string generate_ai_commit_message(const std::string& staged_context, const std::string& target_repo_path);
    void generate_ai_summary();
    void commit_all_with_ai_message();

    std::string get_repository_status_json(const std::string& params) const;
    std::string get_repositories_needing_push_json() const;
    std::string get_modified_repositories_json() const;

    static std::string git_status_to_string(rouen::models::GitRepoStatus status);
};
