#include "git_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <map>
#include <string>

#include "../../../external/IconsMaterialDesign.h"
#include "models/git.hpp"

namespace {
    ImColor getStatusColor(rouen::models::GitRepoStatus status) {
        static const std::map<rouen::models::GitRepoStatus, ImColor> statusColorMap = {
            {rouen::models::GitRepoStatus::Clean,     ImColor(0, 255, 0, 255)},     // Green
            {rouen::models::GitRepoStatus::Modified,  ImColor(255, 165, 0, 255)},   // Orange
            {rouen::models::GitRepoStatus::Untracked, ImColor(200, 200, 0, 255)},   // Yellow
            {rouen::models::GitRepoStatus::Staged,    ImColor(0, 200, 255, 255)},   // Blue
            {rouen::models::GitRepoStatus::Conflict,  ImColor(255, 0, 0, 255)},     // Red
            {rouen::models::GitRepoStatus::Detached,  ImColor(128, 0, 128, 255)}    // Purple
        };
        
        auto it = statusColorMap.find(status);
        return it != statusColorMap.end() ? it->second : ImColor(255, 255, 255, 255);
    }
}

std::string git_overlay::git_status_to_string(rouen::models::GitRepoStatus status) {
    switch (status) {
        case rouen::models::GitRepoStatus::Clean: return "Clean";
        case rouen::models::GitRepoStatus::Modified: return "Modified";
        case rouen::models::GitRepoStatus::Untracked: return "Untracked";
        case rouen::models::GitRepoStatus::Staged: return "Staged";
        case rouen::models::GitRepoStatus::Conflict: return "Conflict";
        case rouen::models::GitRepoStatus::Detached: return "Detached";
        case rouen::models::GitRepoStatus::Unknown: return "Unknown";
    }
    return "Unknown";
}

ImVec2 git_overlay::calculate_position(git_overlay_position pos, ImVec2 win_size, ImVec2 display_size) {
    float const display_w = (display_size.x > 0.0f) ? display_size.x : 1920.0f;
    float const display_h = (display_size.y > 0.0f) ? display_size.y : 1080.0f;

    float const margin_x = std::max(20.0f, display_w * 0.03f);
    float const margin_y = std::max(40.0f, display_h * 0.05f);

    float x = display_w - win_size.x - margin_x;
    float y = margin_y;

    switch (pos) {
        case git_overlay_position::top_right:
            x = display_w - win_size.x - margin_x;
            y = margin_y;
            break;
        case git_overlay_position::top_left:
            x = margin_x;
            y = margin_y;
            break;
        case git_overlay_position::bottom_right:
            x = display_w - win_size.x - margin_x;
            y = display_h - win_size.y - margin_y;
            break;
        case git_overlay_position::bottom_left:
            x = margin_x;
            y = display_h - win_size.y - margin_y;
            break;
        case git_overlay_position::center:
            x = (display_w - win_size.x) * 0.5f;
            y = (display_h - win_size.y) * 0.5f;
            break;
        case git_overlay_position::top_center:
            x = (display_w - win_size.x) * 0.5f;
            y = margin_y;
            break;
        case git_overlay_position::bottom_center:
            x = (display_w - win_size.x) * 0.5f;
            y = display_h - win_size.y - margin_y;
            break;
    }

    return ImVec2(x, y);
}

void git_overlay::render(
    const std::string& repo_path,
    const punch_card_info& punch_data,
    rouen::models::GitRepoStatus repo_status,
    const std::string& github_repo_name,
    git_overlay_state& overlay_state,
    const ImVec4* colors
) {
    if (repo_path.empty()) {
        return;
    }

    ImVec2 const display_size = ImGui::GetIO().DisplaySize;
    constexpr float win_width = 720.0f;
    constexpr float win_height = 430.0f;
    ImVec2 const win_size(
        std::min(win_width, display_size.x > 0.0f ? display_size.x * 0.9f : win_width),
        std::min(win_height, display_size.y > 0.0f ? display_size.y * 0.9f : win_height)
    );

    ImVec2 const window_pos = calculate_position(overlay_state.position, win_size, display_size);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.09f, 0.14f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, colors[0]);

    std::string repo_filename = std::filesystem::path(repo_path).filename().string();
    std::string const overlay_window_title = std::format("Git Video Overlay: {}##CastGitOverlay", repo_filename);

    if (ImGui::Begin(overlay_window_title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(colors[0], "%s Git Overlay", ICON_MD_CODE);
        ImGui::SameLine();
        ImGui::TextDisabled("— %s", repo_filename.c_str());

        // Header controls (Position Selector & Close/Deselect button)
        float const available_w = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(available_w - 230.0f);

        if (!overlay_state.selected_commit_hash.empty()) {
            if (ImGui::SmallButton(ICON_MD_CLOSE " Overview")) {
                overlay_state.selected_commit_hash.clear();
            }
            ImGui::SameLine();
        }

        const char* position_names[] = {
            "Top-Right", "Top-Left", "Bottom-Right", "Bottom-Left", "Center", "Top-Center", "Bottom-Center"
        };
        int current_pos_idx = static_cast<int>(overlay_state.position);
        ImGui::PushItemWidth(140.0f);
        if (ImGui::Combo("##OverlayPosCombo", &current_pos_idx, position_names, IM_ARRAYSIZE(position_names))) {
            overlay_state.position = static_cast<git_overlay_position>(current_pos_idx);
        }
        ImGui::PopItemWidth();

        ImGui::Separator();

        ImVec2 const content_pos = ImGui::GetCursorScreenPos();
        ImVec2 const content_size(win_size.x - 32.0f, win_size.y - 85.0f);

        if (overlay_state.selected_commit_hash.empty()) {
            render_slideshow(repo_path, punch_data, repo_status, github_repo_name, content_pos, content_size, colors);
        } else {
            render_commit_detail_view(overlay_state, content_pos, content_size, colors);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void git_overlay::render_slideshow(
    const std::string& repo_path,
    const punch_card_info& data,
    rouen::models::GitRepoStatus repo_status,
    const std::string& github_repo_name,
    ImVec2 pos,
    ImVec2 size,
    const ImVec4* colors
) {
    constexpr float slide_duration = 6.0f;
    constexpr float transition_duration = 0.8f;
    constexpr int num_slides = 3;

    float const current_time = static_cast<float>(ImGui::GetTime());
    float const cycle_time = std::fmod(current_time, slide_duration * static_cast<float>(num_slides));
    int const current_slide = static_cast<int>(cycle_time / slide_duration) % num_slides;
    float const slide_elapsed = std::fmod(cycle_time, slide_duration);

    int const next_slide = (current_slide + 1) % num_slides;
    float t = 0.0f;
    if (slide_elapsed > (slide_duration - transition_duration)) {
        float const raw_t = (slide_elapsed - (slide_duration - transition_duration)) / transition_duration;
        t = raw_t * raw_t * (3.0f - 2.0f * raw_t);
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);

    // Render current slide
    ImVec2 const pos1(pos.x - t * size.x, pos.y);
    if (current_slide == 0) {
        render_video_slide_overview(repo_path, data, repo_status, github_repo_name, pos1, size, colors);
    } else if (current_slide == 1) {
        render_video_slide_matrix(data, pos1, size, colors);
    } else {
        render_video_slide_heatmap(data, pos1, size, colors);
    }

    // Render next slide during transition
    if (t > 0.001f) {
        ImVec2 const pos2(pos.x + (1.0f - t) * size.x, pos.y);
        if (next_slide == 0) {
            render_video_slide_overview(repo_path, data, repo_status, github_repo_name, pos2, size, colors);
        } else if (next_slide == 1) {
            render_video_slide_matrix(data, pos2, size, colors);
        } else {
            render_video_slide_heatmap(data, pos2, size, colors);
        }
    }

    draw_list->PopClipRect();

    // Carousel progress bar & dots
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 4.0f));
    ImGui::Separator();
    
    constexpr const char* slide_titles[] = {"1/3 Overview", "2/3 Matrix Graph", "3/3 90-Day Heatmap"};
    ImGui::TextColored(colors[1], "%s %s", ICON_MD_SLIDESHOW, slide_titles[current_slide]);

    ImVec2 const dot_start(pos.x + size.x - 60.0f, pos.y + size.y + 12.0f);
    for (int i = 0; i < num_slides; ++i) {
        ImVec2 const dot_pos(dot_start.x + static_cast<float>(i) * 16.0f, dot_start.y);
        bool const is_active = (i == current_slide);
        ImColor const dot_color = is_active ? ImColor(colors[1]) : ImColor(100, 110, 130, 150);
        draw_list->AddCircleFilled(dot_pos, is_active ? 4.5f : 3.0f, dot_color);
    }
}

void git_overlay::render_commit_detail_view(
    git_overlay_state& overlay_state,
    ImVec2 pos,
    ImVec2 size,
    const ImVec4* colors
) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    if (overlay_state.loading_commit_detail) {
        ImGui::Spacing();
        ImGui::TextColored(colors[1], "%s Loading details for commit %s...", ICON_MD_SYNC, overlay_state.selected_commit_hash.c_str());
        ImGui::EndGroup();
        return;
    }

    const auto& detail = overlay_state.selected_commit_detail;
    if (!detail.loaded) {
        ImGui::Spacing();
        ImGui::TextColored(colors[1], "%s Fetching commit data...", ICON_MD_SYNC);
        ImGui::EndGroup();
        return;
    }

    // Commit Header
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.14f, 0.20f, 0.8f));
    if (ImGui::BeginChild("CommitHeaderBox", ImVec2(size.x, 62.0f), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextColored(colors[1], "%s Commit", ICON_MD_COMMIT);
        ImGui::SameLine();
        ImGui::TextColored(colors[0], "%s", detail.short_hash.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", detail.hash.c_str());

        ImGui::Text("%s %s <%s>", ICON_MD_PERSON, detail.author_name.c_str(), detail.author_email.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160.0f);
        ImGui::TextDisabled("%s %s", ICON_MD_ACCESS_TIME, detail.date_str.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Commit Message Body
    ImGui::TextColored(colors[0], "%s Description & Commit Message", ICON_MD_DESCRIPTION);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.11f, 0.16f, 0.9f));
    float const message_box_h = size.y - 180.0f;
    if (ImGui::BeginChild("CommitMessageBox", ImVec2(size.x, std::max(80.0f, message_box_h)), true)) {
        ImGui::TextWrapped("%s", detail.full_message.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Stats & Changed Files Summary
    if (!detail.stats_summary.empty() || !detail.changed_files.empty()) {
        ImGui::TextColored(colors[1], "%s Changes: %s", ICON_MD_INSERT_DRIVE_FILE,
            detail.stats_summary.empty() ? std::format("{} file(s)", detail.changed_files.size()).c_str() : detail.stats_summary.c_str());

        if (!detail.changed_files.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu files)", detail.changed_files.size());
        }
    }

    ImGui::EndGroup();
}

void git_overlay::render_video_slide_overview(
    const std::string& repo_path,
    const punch_card_info& data,
    rouen::models::GitRepoStatus repo_status,
    const std::string& github_repo_name,
    ImVec2 pos,
    [[maybe_unused]] ImVec2 size,
    const ImVec4* colors
) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    ImGui::TextColored(colors[0], "%s Repository General Overview", ICON_MD_INFO);
    ImGui::Spacing();

    std::string const repo_filename = std::filesystem::path(repo_path).filename().string();
    ImGui::Text("Repository: %s", repo_filename.c_str());
    ImGui::TextDisabled("Path: %s", repo_path.c_str());

    ImColor const status_color = getStatusColor(repo_status);
    ImGui::Text("Status: ");
    ImGui::SameLine();
    ImGui::TextColored(status_color, "%s", git_status_to_string(repo_status).c_str());

    if (!github_repo_name.empty()) {
        ImGui::TextColored(colors[1], "GitHub: %s", github_repo_name.c_str());
    }

    if (data.selected_author_idx >= 0 && data.selected_author_idx < static_cast<int>(data.author_list.size())) {
        ImGui::TextColored(colors[1], "Contributor Filter: %s", data.author_list[static_cast<size_t>(data.selected_author_idx)].c_str());
    }

    ImGui::Spacing();
    ImGui::TextColored(colors[0], "Activity Summary (Last 90 Days):");
    ImGui::BulletText("Filtered Commits: %d", data.total_commits);

    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    int peak_wday = 0, peak_wday_count = -1;
    for (int w = 0; w < 7; ++w) {
        if (data.day_of_week_totals[w] > peak_wday_count) {
            peak_wday_count = data.day_of_week_totals[w];
            peak_wday = w;
        }
    }

    int peak_hour = 0, peak_hour_count = -1;
    for (int h = 0; h < 24; ++h) {
        if (data.hour_totals[h] > peak_hour_count) {
            peak_hour_count = data.hour_totals[h];
            peak_hour = h;
        }
    }

    if (data.total_commits > 0) {
        ImGui::BulletText("Most Active Day: %s (%d commits)", wday_names[peak_wday], peak_wday_count);
        ImGui::BulletText("Most Active Hour: %02d:00 (%d commits)", peak_hour, peak_hour_count);
        ImGui::BulletText("Active Days: %zu / 90 days", data.daily_counts.size());
    } else {
        ImGui::TextDisabled("No commit activity recorded for selected contributor.");
    }

    ImGui::EndGroup();
}

void git_overlay::render_video_slide_matrix(
    const punch_card_info& data,
    ImVec2 pos,
    [[maybe_unused]] ImVec2 size,
    const ImVec4* colors
) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    ImGui::TextColored(colors[0], "%s Commit Matrix (Day of Week x Hour of Day)", ICON_MD_SCHEDULE);
    ImGui::Spacing();

    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr float label_width = 38.0f;
    constexpr float cell_size = 20.0f;
    constexpr float cell_padding = 2.0f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 const grid_pos(pos.x, pos.y + 30.0f);

    for (int w = 0; w < 7; ++w) {
        float const wf = static_cast<float>(w);
        float const y = grid_pos.y + wf * cell_size + cell_size * 0.5f;
        draw_list->AddText(ImVec2(grid_pos.x + 4.0f, y - 6.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);

        for (int h = 0; h < 24; ++h) {
            float const hf = static_cast<float>(h);
            float const x = grid_pos.x + label_width + hf * cell_size + cell_size * 0.5f;
            ImVec2 const cell_min(grid_pos.x + label_width + hf * cell_size, grid_pos.y + wf * cell_size);
            ImVec2 const cell_max(cell_min.x + cell_size - cell_padding, cell_min.y + cell_size - cell_padding);

            draw_list->AddRectFilled(cell_min, cell_max, ImColor(25, 30, 40, 180), 3.0f);

            int const count = data.hour_matrix[w][h];
            if (count > 0 && data.max_commits_per_cell > 0) {
                float const relative_count = static_cast<float>(count) / static_cast<float>(data.max_commits_per_cell);
                float const radius = 2.5f + 6.5f * std::sqrt(relative_count);

                float const alpha = 0.35f + 0.65f * relative_count;
                ImColor const circle_color(
                    static_cast<int>(colors[1].x * 255),
                    static_cast<int>(colors[1].y * 255),
                    static_cast<int>(colors[1].z * 255),
                    static_cast<int>(alpha * 255)
                );

                draw_list->AddCircleFilled(ImVec2(x, y), radius, circle_color, 12);
            }
        }
    }

    float const hour_y = grid_pos.y + 7.0f * cell_size + 4.0f;
    for (int h = 0; h < 24; h += 2) {
        float const hf = static_cast<float>(h);
        float const x = grid_pos.x + label_width + hf * cell_size;
        draw_list->AddText(ImVec2(x, hour_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), std::format("{:02d}", h).c_str());
    }

    ImGui::EndGroup();
}

void git_overlay::render_video_slide_heatmap(
    const punch_card_info& data,
    ImVec2 pos,
    [[maybe_unused]] ImVec2 size,
    const ImVec4* colors
) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    ImGui::TextColored(colors[0], "%s 90-Day Contribution Heatmap", ICON_MD_CALENDAR_VIEW_MONTH);
    ImGui::Spacing();

    constexpr float tile_size = 14.0f;
    constexpr float tile_padding = 3.0f;
    constexpr float label_width = 38.0f;
    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    std::time_t const now_time = std::time(nullptr);
    std::time_t const start_time = now_time - static_cast<std::time_t>(90 * 86400);

    constexpr int num_weeks = 14;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 const grid_pos(pos.x, pos.y + 30.0f);

    for (int const w : {0, 2, 4, 6}) {
        float const wf = static_cast<float>(w);
        float const y = grid_pos.y + wf * (tile_size + tile_padding);
        draw_list->AddText(ImVec2(grid_pos.x + 4.0f, y - 2.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);
    }

    for (int day_offset = 0; day_offset <= 90; ++day_offset) {
        std::time_t const day_time = start_time + static_cast<std::time_t>(day_offset * 86400);
        std::tm day_tm{};
#ifdef _WIN32
        localtime_s(&day_tm, &day_time);
#else
        localtime_r(&day_time, &day_tm);
#endif

        char date_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &day_tm);
        std::string const date_str(date_buf);

        int const wday = day_tm.tm_wday;
        int const week_idx = day_offset / 7;

        if (week_idx >= num_weeks) continue;

        float const week_idxf = static_cast<float>(week_idx);
        float const wdayf = static_cast<float>(wday);

        ImVec2 const tile_min(
            grid_pos.x + label_width + week_idxf * (tile_size + tile_padding),
            grid_pos.y + wdayf * (tile_size + tile_padding)
        );
        ImVec2 const tile_max(tile_min.x + tile_size, tile_min.y + tile_size);

        int count = 0;
        if (auto it = data.daily_counts.find(date_str); it != data.daily_counts.end()) {
            count = it->second;
        }

        ImColor tile_color;
        if (count == 0) {
            tile_color = ImColor(25, 30, 40, 100);
        } else {
            float const relative_count = (data.max_commits_per_day > 0)
                ? static_cast<float>(count) / static_cast<float>(data.max_commits_per_day)
                : 0.2f;

            float const alpha = 0.35f + 0.65f * relative_count;
            tile_color = ImColor(
                static_cast<int>(colors[1].x * 255),
                static_cast<int>(colors[1].y * 255),
                static_cast<int>(colors[1].z * 255),
                static_cast<int>(alpha * 255)
            );
        }

        draw_list->AddRectFilled(tile_min, tile_max, tile_color, 2.5f);
    }

    ImGui::EndGroup();
}
