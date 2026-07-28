#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../interface/card.hpp"
#include "../../models/git.hpp"

struct punch_card_info {
    bool loaded{false};
    bool loading{false};
    std::string repo_path;

    int total_commits{0};
    int max_commits_per_cell{0};
    int max_commits_per_day{0};

    int hour_matrix[7][24]{};         // [wday 0..6 (Sun..Sat)][hour 0..23]
    int day_of_week_totals[7]{};     // [wday]
    int hour_totals[24]{};            // [hour]
    std::unordered_map<std::string, int> daily_counts; // "YYYY-MM-DD" -> count
};

struct git : public card {
    std::string repo_status;
    std::unique_ptr<rouen::models::git> git_model;
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

    explicit git(std::string_view repo_path = "");

    [[nodiscard]] std::string get_uri() const override;
    [[nodiscard]] bool matches_uri(std::string_view uri) const override;
    void handle_uri(std::string_view uri) override;
    [[nodiscard]] std::vector<mcp_function> get_mcp_functions() const override;
    bool render() override;

    void trigger_async_status_update();
    void fetch_punch_card_async(const std::string& repo_path);
    bool select(const std::string& repo_path);
    void back_to_list();
    bool updateRepoStatusSync();
    bool updateRepoStatus();

    void render_ai_busy_cue();
    void render_selected();
    void render_index();
    void render_github_status_indicator();

    void render_punch_card();
    void render_punch_card_matrix(const punch_card_info& data);
    void render_punch_card_heatmap(const punch_card_info& data);

    static std::string extract_github_repo_name(const std::string& remote_url);

private:
    void prepend_action_result(const std::string& action_name, const std::string& command_output);
    std::string generate_ai_commit_message(const std::string& staged_context, const std::string& repo_path);
    void generate_ai_summary();
    void commit_all_with_ai_message();

    std::string get_repository_status_json(const std::string& params) const;
    std::string get_repositories_needing_push_json() const;
    std::string get_modified_repositories_json() const;

    static std::string git_status_to_string(rouen::models::GitRepoStatus status);
};
