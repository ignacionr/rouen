#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../helpers/imgui_include.hpp"
#include "../../models/git.hpp"

struct raw_commit_entry {
    int64_t timestamp{0};
    std::string author_name;
    std::string author_email;
};

struct punch_card_info {
    bool loaded{false};
    bool loading{false};
    std::string repo_path;

    std::vector<raw_commit_entry> commits;
    std::vector<std::string> author_list{"All Contributors"};
    int selected_author_idx{0};

    int total_commits{0};
    int max_commits_per_cell{0};
    int max_commits_per_day{0};

    int hour_matrix[7][24]{};         // [wday 0..6 (Sun..Sat)][hour 0..23]
    int day_of_week_totals[7]{};     // [wday]
    int hour_totals[24]{};            // [hour]
    std::unordered_map<std::string, int> daily_counts; // "YYYY-MM-DD" -> count

    void recalculate_stats() {
        total_commits = 0;
        max_commits_per_cell = 0;
        max_commits_per_day = 0;
        std::memset(hour_matrix, 0, sizeof(hour_matrix));
        std::memset(day_of_week_totals, 0, sizeof(day_of_week_totals));
        std::memset(hour_totals, 0, sizeof(hour_totals));
        daily_counts.clear();

        std::string filter_author;
        if (selected_author_idx > 0 && static_cast<size_t>(selected_author_idx) < author_list.size()) {
            filter_author = author_list[static_cast<size_t>(selected_author_idx)];
        }

        for (const auto& commit : commits) {
            if (!filter_author.empty() && commit.author_name != filter_author && commit.author_email != filter_author) {
                continue;
            }

            std::time_t t = static_cast<std::time_t>(commit.timestamp);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &t);
#else
            localtime_r(&t, &tm_buf);
#endif
            int wday = tm_buf.tm_wday;
            int hour = tm_buf.tm_hour;

            if (wday >= 0 && wday < 7 && hour >= 0 && hour < 24) {
                hour_matrix[wday][hour]++;
                day_of_week_totals[wday]++;
                hour_totals[hour]++;
                total_commits++;

                if (hour_matrix[wday][hour] > max_commits_per_cell) {
                    max_commits_per_cell = hour_matrix[wday][hour];
                }
            }

            char date_buf[32];
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_buf);
            std::string date_str(date_buf);
            daily_counts[date_str]++;
            if (daily_counts[date_str] > max_commits_per_day) {
                max_commits_per_day = daily_counts[date_str];
            }
        }
    }
};

enum class git_overlay_position {
    top_right,
    top_left,
    bottom_right,
    bottom_left,
    center,
    top_center,
    bottom_center
};

inline const char* get_overlay_position_name(git_overlay_position pos) {
    switch (pos) {
        case git_overlay_position::top_right: return "Top-Right";
        case git_overlay_position::top_left: return "Top-Left";
        case git_overlay_position::bottom_right: return "Bottom-Right";
        case git_overlay_position::bottom_left: return "Bottom-Left";
        case git_overlay_position::center: return "Center";
        case git_overlay_position::top_center: return "Top-Center";
        case git_overlay_position::bottom_center: return "Bottom-Center";
    }
    return "Top-Right";
}

struct git_commit_summary {
    std::string hash;
    std::string short_hash;
    int64_t timestamp{0};
    std::string author_name;
    std::string author_email;
    std::string summary;
    std::string date_str;
};

struct git_commit_detail {
    std::string hash;
    std::string short_hash;
    int64_t timestamp{0};
    std::string author_name;
    std::string author_email;
    std::string full_message;
    std::string date_str;
    std::vector<std::string> changed_files;
    std::string stats_summary;
    std::string raw_stat_text;
    bool loaded{false};
};

struct git_overlay_state {
    git_overlay_position position{git_overlay_position::top_right};
    std::string selected_commit_hash;
    git_commit_detail selected_commit_detail;
    bool loading_commit_detail{false};

    std::vector<git_commit_summary> recent_commits_7d;
    bool loading_recent_commits{false};
    bool recent_commits_loaded{false};
    std::string loaded_repo_path;
};

class git_overlay {
public:
    static ImVec2 calculate_position(git_overlay_position pos, ImVec2 win_size, ImVec2 display_size);

    static void render(
        const std::string& repo_path,
        const punch_card_info& punch_data,
        rouen::models::GitRepoStatus repo_status,
        const std::string& github_repo_name,
        git_overlay_state& overlay_state,
        const ImVec4* colors
    );

private:
    static void render_slideshow(
        const std::string& repo_path,
        const punch_card_info& data,
        rouen::models::GitRepoStatus repo_status,
        const std::string& github_repo_name,
        ImVec2 pos,
        ImVec2 size,
        const ImVec4* colors
    );

    static void render_commit_detail_view(
        git_overlay_state& overlay_state,
        ImVec2 pos,
        ImVec2 size,
        const ImVec4* colors
    );

    static void render_video_slide_overview(
        const std::string& repo_path,
        const punch_card_info& data,
        rouen::models::GitRepoStatus repo_status,
        const std::string& github_repo_name,
        ImVec2 pos,
        ImVec2 size,
        const ImVec4* colors
    );

    static void render_video_slide_matrix(
        const punch_card_info& data,
        ImVec2 pos,
        ImVec2 size,
        const ImVec4* colors
    );

    static void render_video_slide_heatmap(
        const punch_card_info& data,
        ImVec2 pos,
        ImVec2 size,
        const ImVec4* colors
    );

    static std::string git_status_to_string(rouen::models::GitRepoStatus status);
};
