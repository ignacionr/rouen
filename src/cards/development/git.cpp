#include "git.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "../../../external/IconsMaterialDesign.h"
#include "../../helpers/config_service.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../registrar.hpp"

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

    std::string trim_copy(std::string value) {
        auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        return value;
    }
}

git::git(std::string_view repo_path) {
    colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};  // Blue accent color
    colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
    width = 600.0f;

    git_model = std::make_unique<rouen::models::git>();

    if (!repo_path.empty()) {
        std::string_view actual_path = repo_path;
        if (actual_path.starts_with("git:")) {
            actual_path = actual_path.substr(4);
        }
        if (!actual_path.empty()) {
            select(std::string(actual_path));
        } else {
            name("Git Repos");
            trigger_async_status_update();
        }
    } else {
        name("Git Repos");
        trigger_async_status_update();
    }
}

std::string git::get_uri() const {
    if (!selected_repo.empty()) {
        return std::format("git:{}", selected_repo);
    }
    return "git";
}

bool git::matches_uri(std::string_view uri) const {
    return uri == "git" || uri.starts_with("git:");
}

void git::handle_uri(std::string_view uri) {
    if (uri.starts_with("git:")) {
        std::string path = std::string(uri.substr(4));
        if (!path.empty()) {
            select(path);
        } else {
            back_to_list();
        }
    } else if (uri == "git") {
        back_to_list();
    }
}

std::vector<card::mcp_function> git::get_mcp_functions() const {
    return {
        card::mcp_function(
            "get_repository_status",
            "Get status of git repositories. Returns the current state of git repositories. Status values: 'clean' (no changes), 'modified' (uncommitted changes), 'untracked' (contains untracked files), 'staged' (changes ready to commit), 'conflict' (merge conflicts), 'detached' (detached HEAD), 'unknown' (status unclear). If repo_path is provided, returns status for that specific repo, otherwise returns status for all repositories.",
            R"({"type":"object","properties":{"repo_path":{"type":"string","description":"Optional: specific repository path to check"}}})",
            [this](const std::string& params) { return get_repository_status_json(params); }
        ),
        card::mcp_function(
            "get_repositories_needing_push",
            "Get list of repositories that have commits ahead of their remote branches and need to be pushed.",
            R"({"type": "object", "properties": {}})",
            [this](const std::string&) { return get_repositories_needing_push_json(); }
        ),
        card::mcp_function(
            "get_modified_repositories", 
            "Get repositories with uncommitted changes (modified, staged, or untracked files).",
            R"({"type": "object", "properties": {}})",
            [this](const std::string&) { return get_modified_repositories_json(); }
        )
    };
}

void git::trigger_async_status_update() {
    if (status_update_in_progress.exchange(true)) {
        return;
    }

    std::string current_repo;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_repo = selected_repo;
    }

    std::thread([this, current_repo]() {
        if (!current_repo.empty()) {
            std::string status = git_model->getGitStatus(current_repo);
            std::string git_remote = git_model->getGitRemote(current_repo);
            std::string repo_name = extract_github_repo_name(git_remote);

            std::lock_guard<std::mutex> lock(state_mutex);
            if (selected_repo == current_repo) {
                repo_status = status;
                cached_git_remote = git_remote;
                cached_github_repo_name = repo_name;
            }
        } else {
            for (const auto& path : git_model->getRepoPaths()) {
                git_model->getGitStatus(path);
            }
        }
        status_update_in_progress = false;
    }).detach();
}

void git::fetch_punch_card_async(const std::string& repo_path) {
    if (repo_path.empty()) return;

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (punch_card_data_.loaded && punch_card_data_.repo_path == repo_path) {
            return; // Already loaded in memory for this repo
        }
        punch_card_data_ = punch_card_info{};
        punch_card_data_.loading = true;
        punch_card_data_.repo_path = repo_path;
    }

    std::thread([this, repo_path]() {
        std::string git_bin = CONFIG_SERVICE()->get_git_path();
        std::string cmd = std::format("{} log --since=\"3 months ago\" --format=\"%at|%an|%ae\"", git_bin);
        std::string output = rouen::models::GitProcessHelper::executeCommandInDirectory(repo_path, cmd);

        punch_card_info data;
        data.repo_path = repo_path;
        std::set<std::string> unique_authors;

        std::stringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
            line = trim_copy(line);
            if (line.empty()) continue;

            auto p1 = line.find('|');
            if (p1 == std::string::npos) continue;
            auto p2 = line.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;

            try {
                int64_t ts = std::stoll(line.substr(0, p1));
                std::string aname = line.substr(p1 + 1, p2 - (p1 + 1));
                std::string aemail = line.substr(p2 + 1);

                if (!aname.empty()) {
                    unique_authors.insert(aname);
                }

                data.commits.push_back(raw_commit_entry{
                    .timestamp = ts,
                    .author_name = aname,
                    .author_email = aemail
                });
            } catch (...) {
                // Ignore conversion errors
            }
        }

        data.author_list = {"All Contributors"};
        for (const auto& author : unique_authors) {
            data.author_list.push_back(author);
        }

        data.recalculate_stats();
        data.loaded = true;
        data.loading = false;

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (selected_repo == repo_path) {
                punch_card_data_ = std::move(data);
            }
        }
    }).detach();
}

bool git::select(const std::string& repo_path) {
    if (repo_path.empty()) {
        return false;
    }

    git_model->addRepository(repo_path);

    if (git_model->getRepos().find(repo_path) == git_model->getRepos().end()) {
        return false;
    }

    this->selected_repo = repo_path;
    name(std::format("Git: {}", std::filesystem::path(selected_repo).filename().string()));
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        cached_git_remote.clear();
        cached_github_repo_name.clear();
        punch_card_data_ = punch_card_info{};
    }
    trigger_async_status_update();
    fetch_punch_card_async(repo_path);
    
    return true;
}

void git::back_to_list() {
    selected_repo.clear();
    name("Git Repos");
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        cached_git_remote.clear();
        cached_github_repo_name.clear();
        punch_card_data_ = punch_card_info{};
    }
    trigger_async_status_update();
}

bool git::updateRepoStatusSync() {
    if (selected_repo.empty()) {
        return false;
    }
    
    std::string status = git_model->getGitStatus(selected_repo);
    std::string git_remote = git_model->getGitRemote(selected_repo);
    std::string repo_name = extract_github_repo_name(git_remote);

    std::lock_guard<std::mutex> lock(state_mutex);
    repo_status = status;
    cached_git_remote = git_remote;
    cached_github_repo_name = repo_name;
    return !repo_status.empty();
}

bool git::updateRepoStatus() {
    trigger_async_status_update();
    fetch_punch_card_async(selected_repo);
    return true;
}

void git::render_ai_busy_cue() {
    double time = ImGui::GetTime();
    auto dot_count = static_cast<std::size_t>(static_cast<long long>(time * 3.0) % 4);
    std::string dots(dot_count, '.');

    std::string cue_text;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        cue_text = ai_status_cue.empty() ? "AI request in progress" : ai_status_cue;
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.25f, 0.35f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, colors[0]);
    if (ImGui::BeginChild("AIBusyCue", ImVec2(0, 36.0f), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(colors[1], "%s  %s%-3s", ICON_MD_SYNC, cue_text.c_str(), dots.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
}

void git::render_selected() {
    std::string current_status;
    std::string current_commit_msg;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_status = repo_status;
        current_commit_msg = last_commit_message;
    }

    if (ImGui::Button("Back to Repository List")) {
        back_to_list();
        return;
    }
    
    ImGui::Text("Repository: %s", selected_repo.c_str());
                
    ImGui::Separator();
    ImGui::BeginChild("GitStatus", ImVec2(0, 100.0f));
    ImGui::TextWrapped("%s", current_status.c_str());
    ImGui::EndChild();

    if (ImGui::SmallButton("Refresh")) {
        updateRepoStatus();
    }
    
    ImGui::SameLine();
    if (ImGui::SmallButton("VS Code")) {
        git_model->openInVSCode(selected_repo);
    }
    
    ImGui::SameLine();
    if (ImGui::SmallButton("Terminal")) {
        "create_card"_sfn(std::format("terminal:{}", selected_repo));
    }
    
    ImGui::SameLine();
    if (ImGui::SmallButton("GitHub CI")) {
        std::string repo_name;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            repo_name = cached_github_repo_name;
        }
        if (!repo_name.empty()) {
            "create_card"_sfn(std::format("github-ci:{}", repo_name));
        }
    }
    
    ImGui::SameLine();
    if (ImGui::SmallButton("Push")) {
        prepend_action_result("Push", git_model->gitPush(selected_repo));
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Pull")) {
        prepend_action_result("Pull", git_model->gitPull(selected_repo));
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("AI Summary")) {
        generate_ai_summary();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Commit All")) {
        commit_all_with_ai_message();
    }

    if (!current_commit_msg.empty()) {
        ImGui::Separator();
        ImGui::Text("Last AI commit message:");
        ImGui::TextWrapped("%s", current_commit_msg.c_str());
    }
    
    render_github_status_indicator();
    render_punch_card();
}

void git::render_punch_card() {
    punch_card_info data;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        data = punch_card_data_;
    }

    if (data.loading) {
        ImGui::Spacing();
        ImGui::TextColored(colors[1], "%s  Loading 3-month punch card...", ICON_MD_SYNC);
        return;
    }

    if (!data.loaded) {
        return;
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_MD_GRID_ON " 3-Month Activity Punch Card", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        if (data.author_list.size() > 1) {
            ImGui::PushItemWidth(260.0f);
            int current_idx = data.selected_author_idx;
            if (ImGui::Combo("Contributor", &current_idx, [](void* data_ptr, int idx, const char** out_text) -> bool {
                auto& list = *static_cast<const std::vector<std::string>*>(data_ptr);
                if (idx < 0 || idx >= static_cast<int>(list.size())) return false;
                *out_text = list[static_cast<size_t>(idx)].c_str();
                return true;
            }, const_cast<void*>(static_cast<const void*>(&data.author_list)), static_cast<int>(data.author_list.size()))) {
                std::lock_guard<std::mutex> lock(state_mutex);
                punch_card_data_.selected_author_idx = current_idx;
                punch_card_data_.recalculate_stats();
            }
            ImGui::PopItemWidth();
            ImGui::Spacing();
        }

        constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

        int peak_wday = 0;
        int peak_wday_count = -1;
        for (int w = 0; w < 7; ++w) {
            if (data.day_of_week_totals[w] > peak_wday_count) {
                peak_wday_count = data.day_of_week_totals[w];
                peak_wday = w;
            }
        }

        int peak_hour = 0;
        int peak_hour_count = -1;
        for (int h = 0; h < 24; ++h) {
            if (data.hour_totals[h] > peak_hour_count) {
                peak_hour_count = data.hour_totals[h];
                peak_hour = h;
            }
        }

        ImGui::TextColored(colors[0], "3-Month Total: %d commits", data.total_commits);
        if (data.total_commits > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("| Peak day: %s (%d) | Peak hour: %02d:00 (%d)",
                wday_names[peak_wday], peak_wday_count, peak_hour, peak_hour_count);
        }

        ImGui::Spacing();

        if (ImGui::BeginTabBar("PunchCardTabs")) {
            if (ImGui::BeginTabItem(ICON_MD_SCHEDULE " Day x Hour Matrix")) {
                render_punch_card_matrix(data);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(ICON_MD_CALENDAR_VIEW_MONTH " 90-Day Contribution Heatmap")) {
                render_punch_card_heatmap(data);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
}

void git::render_punch_card_matrix(const punch_card_info& data) {
    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr float label_width = 38.0f;
    constexpr float cell_size = 19.0f;
    constexpr float cell_padding = 2.0f;
    constexpr float grid_height = 7.0f * cell_size + 24.0f;

    ImGui::Spacing();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(label_width + 24.0f * cell_size, grid_height));

    for (int w = 0; w < 7; ++w) {
        float wf = static_cast<float>(w);
        float y = cursor_pos.y + wf * cell_size + cell_size * 0.5f;
        draw_list->AddText(ImVec2(cursor_pos.x + 4.0f, y - 6.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);

        for (int h = 0; h < 24; ++h) {
            float hf = static_cast<float>(h);
            float x = cursor_pos.x + label_width + hf * cell_size + cell_size * 0.5f;
            ImVec2 cell_min(cursor_pos.x + label_width + hf * cell_size, cursor_pos.y + wf * cell_size);
            ImVec2 cell_max(cell_min.x + cell_size - cell_padding, cell_min.y + cell_size - cell_padding);

            draw_list->AddRectFilled(cell_min, cell_max, ImColor(25, 30, 40, 180), 3.0f);

            int count = data.hour_matrix[w][h];
            if (count > 0 && data.max_commits_per_cell > 0) {
                float relative_count = static_cast<float>(count) / static_cast<float>(data.max_commits_per_cell);
                float radius = 2.5f + 6.0f * std::sqrt(relative_count);

                float alpha = 0.35f + 0.65f * relative_count;
                ImColor circle_color(
                    static_cast<int>(colors[1].x * 255),
                    static_cast<int>(colors[1].y * 255),
                    static_cast<int>(colors[1].z * 255),
                    static_cast<int>(alpha * 255)
                );

                draw_list->AddCircleFilled(ImVec2(x, y), radius, circle_color, 12);
            }

            if (ImGui::IsMouseHoveringRect(cell_min, cell_max)) {
                draw_list->AddRect(cell_min, cell_max, ImColor(255, 255, 255, 200), 3.0f);
                ImGui::SetTooltip("%s at %02d:00 — %d commit(s)", wday_names[w], h, count);
            }
        }
    }

    float hour_y = cursor_pos.y + 7.0f * cell_size + 4.0f;
    for (int h = 0; h < 24; h += 2) {
        float hf = static_cast<float>(h);
        float x = cursor_pos.x + label_width + hf * cell_size;
        draw_list->AddText(ImVec2(x, hour_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), std::format("{:02d}", h).c_str());
    }
}

void git::render_punch_card_heatmap(const punch_card_info& data) {
    constexpr float tile_size = 14.0f;
    constexpr float tile_padding = 3.0f;
    constexpr float label_width = 38.0f;
    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    std::time_t now_time = std::time(nullptr);
    std::tm now_tm{};
#ifdef _WIN32
    localtime_s(&now_tm, &now_time);
#else
    localtime_r(&now_time, &now_tm);
#endif

    std::time_t start_time = now_time - static_cast<std::time_t>(90 * 86400);

    constexpr int num_weeks = 14;
    ImGui::Spacing();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(label_width + num_weeks * (tile_size + tile_padding), 7.0f * (tile_size + tile_padding) + 10.0f));

    for (int w : {0, 2, 4, 6}) {
        float wf = static_cast<float>(w);
        float y = cursor_pos.y + wf * (tile_size + tile_padding);
        draw_list->AddText(ImVec2(cursor_pos.x + 4.0f, y - 2.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);
    }

    for (int day_offset = 0; day_offset <= 90; ++day_offset) {
        std::time_t day_time = start_time + static_cast<std::time_t>(day_offset * 86400);
        std::tm day_tm{};
#ifdef _WIN32
        localtime_s(&day_tm, &day_time);
#else
        localtime_r(&day_time, &day_tm);
#endif

        char date_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &day_tm);
        std::string date_str(date_buf);

        int wday = day_tm.tm_wday;
        int week_idx = day_offset / 7;

        if (week_idx >= num_weeks) continue;

        float week_idxf = static_cast<float>(week_idx);
        float wdayf = static_cast<float>(wday);

        ImVec2 tile_min(
            cursor_pos.x + label_width + week_idxf * (tile_size + tile_padding),
            cursor_pos.y + wdayf * (tile_size + tile_padding)
        );
        ImVec2 tile_max(tile_min.x + tile_size, tile_min.y + tile_size);

        int count = 0;
        if (auto it = data.daily_counts.find(date_str); it != data.daily_counts.end()) {
            count = it->second;
        }

        ImColor tile_color;
        if (count == 0) {
            tile_color = ImColor(25, 30, 40, 100);
        } else {
            float relative_count = (data.max_commits_per_day > 0)
                ? static_cast<float>(count) / static_cast<float>(data.max_commits_per_day)
                : 0.2f;

            float alpha = 0.35f + 0.65f * relative_count;
            tile_color = ImColor(
                static_cast<int>(colors[1].x * 255),
                static_cast<int>(colors[1].y * 255),
                static_cast<int>(colors[1].z * 255),
                static_cast<int>(alpha * 255)
            );
        }

        draw_list->AddRectFilled(tile_min, tile_max, tile_color, 2.5f);

        if (ImGui::IsMouseHoveringRect(tile_min, tile_max)) {
            draw_list->AddRect(tile_min, tile_max, ImColor(255, 255, 255, 220), 2.5f);
            ImGui::SetTooltip("%s: %d commit(s)", date_str.c_str(), count);
        }
    }
}

void git::render_video_ui() {
    if (selected_repo.empty()) {
        return;
    }

    punch_card_info data;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        data = punch_card_data_;
    }

    constexpr float win_width = 720.0f;
    constexpr float win_height = 420.0f;
    constexpr float margin_right = 60.0f;
    constexpr float margin_top = 80.0f;

    ImVec2 window_pos(1920.0f - win_width - margin_right, margin_top);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_width, win_height), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.09f, 0.14f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Border, colors[0]);

    std::string overlay_window_title = std::format("Git Video Overlay: {}##CastGitOverlay", std::filesystem::path(selected_repo).filename().string());

    if (ImGui::Begin(overlay_window_title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(colors[0], "%s Git Card Video Overlay", ICON_MD_CODE);
        ImGui::SameLine();
        ImGui::TextDisabled("— %s", std::filesystem::path(selected_repo).filename().string().c_str());
        ImGui::Separator();

        constexpr float slide_duration = 6.0f;
        constexpr float transition_duration = 0.8f;
        constexpr int num_slides = 3;

        float current_time = static_cast<float>(ImGui::GetTime());
        float cycle_time = std::fmod(current_time, slide_duration * static_cast<float>(num_slides));
        int current_slide = static_cast<int>(cycle_time / slide_duration) % num_slides;
        float slide_elapsed = std::fmod(cycle_time, slide_duration);

        int next_slide = (current_slide + 1) % num_slides;
        float t = 0.0f;
        if (slide_elapsed > (slide_duration - transition_duration)) {
            float raw_t = (slide_elapsed - (slide_duration - transition_duration)) / transition_duration;
            t = raw_t * raw_t * (3.0f - 2.0f * raw_t);
        }

        ImVec2 content_pos = ImGui::GetCursorScreenPos();
        ImVec2 content_size(win_width - 32.0f, win_height - 85.0f);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRect(content_pos, ImVec2(content_pos.x + content_size.x, content_pos.y + content_size.y), true);

        // Render current slide
        ImVec2 pos1(content_pos.x - t * content_size.x, content_pos.y);
        if (current_slide == 0) {
            render_video_slide_overview(data, pos1, content_size);
        } else if (current_slide == 1) {
            render_video_slide_matrix(data, pos1, content_size);
        } else {
            render_video_slide_heatmap(data, pos1, content_size);
        }

        // Render next slide during transition
        if (t > 0.001f) {
            ImVec2 pos2(content_pos.x + (1.0f - t) * content_size.x, content_pos.y);
            if (next_slide == 0) {
                render_video_slide_overview(data, pos2, content_size);
            } else if (next_slide == 1) {
                render_video_slide_matrix(data, pos2, content_size);
            } else {
                render_video_slide_heatmap(data, pos2, content_size);
            }
        }

        draw_list->PopClipRect();

        // Carousel progress bar & dots
        ImGui::SetCursorScreenPos(ImVec2(content_pos.x, content_pos.y + content_size.y + 4.0f));
        ImGui::Separator();
        
        constexpr const char* slide_titles[] = {"1/3 Overview", "2/3 Matrix Graph", "3/3 90-Day Heatmap"};
        ImGui::TextColored(colors[1], "%s %s", ICON_MD_SLIDESHOW, slide_titles[current_slide]);

        ImVec2 dot_start(content_pos.x + content_size.x - 60.0f, content_pos.y + content_size.y + 12.0f);
        for (int i = 0; i < num_slides; ++i) {
            ImVec2 dot_pos(dot_start.x + static_cast<float>(i) * 16.0f, dot_start.y);
            bool is_active = (i == current_slide);
            ImColor dot_color = is_active ? ImColor(colors[1]) : ImColor(100, 110, 130, 150);
            draw_list->AddCircleFilled(dot_pos, is_active ? 4.5f : 3.0f, dot_color);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void git::render_video_slide_overview(const punch_card_info& data, ImVec2 pos, [[maybe_unused]] ImVec2 size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    ImGui::TextColored(colors[0], "%s Repository General Overview", ICON_MD_INFO);
    ImGui::Spacing();

    std::string repo_filename = std::filesystem::path(selected_repo).filename().string();
    ImGui::Text("Repository: %s", repo_filename.c_str());
    ImGui::TextDisabled("Path: %s", selected_repo.c_str());

    rouen::models::GitRepoStatus status = git_model->getRepoStatus(selected_repo);
    ImColor status_color = getStatusColor(status);
    ImGui::Text("Status: ");
    ImGui::SameLine();
    ImGui::TextColored(status_color, "%s", git_status_to_string(status).c_str());

    if (!cached_github_repo_name.empty()) {
        ImGui::TextColored(colors[1], "GitHub: %s", cached_github_repo_name.c_str());
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

void git::render_video_slide_matrix(const punch_card_info& data, ImVec2 pos, [[maybe_unused]] ImVec2 size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    ImGui::TextColored(colors[0], "%s Commit Matrix (Day of Week x Hour of Day)", ICON_MD_SCHEDULE);
    ImGui::Spacing();

    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr float label_width = 38.0f;
    constexpr float cell_size = 20.0f;
    constexpr float cell_padding = 2.0f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 grid_pos(pos.x, pos.y + 30.0f);

    for (int w = 0; w < 7; ++w) {
        float wf = static_cast<float>(w);
        float y = grid_pos.y + wf * cell_size + cell_size * 0.5f;
        draw_list->AddText(ImVec2(grid_pos.x + 4.0f, y - 6.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);

        for (int h = 0; h < 24; ++h) {
            float hf = static_cast<float>(h);
            float x = grid_pos.x + label_width + hf * cell_size + cell_size * 0.5f;
            ImVec2 cell_min(grid_pos.x + label_width + hf * cell_size, grid_pos.y + wf * cell_size);
            ImVec2 cell_max(cell_min.x + cell_size - cell_padding, cell_min.y + cell_size - cell_padding);

            draw_list->AddRectFilled(cell_min, cell_max, ImColor(25, 30, 40, 180), 3.0f);

            int count = data.hour_matrix[w][h];
            if (count > 0 && data.max_commits_per_cell > 0) {
                float relative_count = static_cast<float>(count) / static_cast<float>(data.max_commits_per_cell);
                float radius = 2.5f + 6.5f * std::sqrt(relative_count);

                float alpha = 0.35f + 0.65f * relative_count;
                ImColor circle_color(
                    static_cast<int>(colors[1].x * 255),
                    static_cast<int>(colors[1].y * 255),
                    static_cast<int>(colors[1].z * 255),
                    static_cast<int>(alpha * 255)
                );

                draw_list->AddCircleFilled(ImVec2(x, y), radius, circle_color, 12);
            }
        }
    }

    float hour_y = grid_pos.y + 7.0f * cell_size + 4.0f;
    for (int h = 0; h < 24; h += 2) {
        float hf = static_cast<float>(h);
        float x = grid_pos.x + label_width + hf * cell_size;
        draw_list->AddText(ImVec2(x, hour_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), std::format("{:02d}", h).c_str());
    }

    ImGui::EndGroup();
}

void git::render_video_slide_heatmap(const punch_card_info& data, ImVec2 pos, [[maybe_unused]] ImVec2 size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginGroup();

    ImGui::TextColored(colors[0], "%s 90-Day Contribution Heatmap", ICON_MD_CALENDAR_VIEW_MONTH);
    ImGui::Spacing();

    constexpr float tile_size = 18.0f;
    constexpr float tile_padding = 4.0f;
    constexpr float label_width = 38.0f;
    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    std::time_t now_time = std::time(nullptr);
    std::time_t start_time = now_time - static_cast<std::time_t>(90 * 86400);

    constexpr int num_weeks = 14;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 grid_pos(pos.x, pos.y + 30.0f);

    for (int w : {0, 2, 4, 6}) {
        float wf = static_cast<float>(w);
        float y = grid_pos.y + wf * (tile_size + tile_padding);
        draw_list->AddText(ImVec2(grid_pos.x + 4.0f, y - 2.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);
    }

    for (int day_offset = 0; day_offset <= 90; ++day_offset) {
        std::time_t day_time = start_time + static_cast<std::time_t>(day_offset * 86400);
        std::tm day_tm{};
#ifdef _WIN32
        localtime_s(&day_tm, &day_time);
#else
        localtime_r(&day_time, &day_tm);
#endif

        char date_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &day_tm);
        std::string date_str(date_buf);

        int wday = day_tm.tm_wday;
        int week_idx = day_offset / 7;

        if (week_idx >= num_weeks) continue;

        float week_idxf = static_cast<float>(week_idx);
        float wdayf = static_cast<float>(wday);

        ImVec2 tile_min(
            grid_pos.x + label_width + week_idxf * (tile_size + tile_padding),
            grid_pos.y + wdayf * (tile_size + tile_padding)
        );
        ImVec2 tile_max(tile_min.x + tile_size, tile_min.y + tile_size);

        int count = 0;
        if (auto it = data.daily_counts.find(date_str); it != data.daily_counts.end()) {
            count = it->second;
        }

        ImColor tile_color;
        if (count == 0) {
            tile_color = ImColor(25, 30, 40, 120);
        } else {
            float relative_count = (data.max_commits_per_day > 0)
                ? static_cast<float>(count) / static_cast<float>(data.max_commits_per_day)
                : 0.2f;

            float alpha = 0.35f + 0.65f * relative_count;
            tile_color = ImColor(
                static_cast<int>(colors[1].x * 255),
                static_cast<int>(colors[1].y * 255),
                static_cast<int>(colors[1].z * 255),
                static_cast<int>(alpha * 255)
            );
        }

        draw_list->AddRectFilled(tile_min, tile_max, tile_color, 3.0f);
    }

    ImGui::EndGroup();
}

bool git::render() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_status_update >= std::chrono::seconds(20)) {
        last_status_update = now;
        trigger_async_status_update();
    }

    return render_window([this]() {
        bool disabled = ai_request_pending.load();

        if (disabled) {
            render_ai_busy_cue();
            ImGui::BeginDisabled();
        }

        if (selected_repo.empty()) {
            render_index();
        } else {
            render_selected();
        }

        if (disabled) {
            ImGui::EndDisabled();
        }
    });
}

void git::render_index() {
    const auto& repo_paths = git_model->getRepoPaths();

    for (const auto& repo_path : repo_paths) {
        rouen::models::GitRepoStatus status = git_model->getRepoStatus(repo_path);
        ImColor dotColor = getStatusColor(status);
        
        ImGui::BeginGroup();
        
        float dotRadius = 4.0f;
        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImVec2 dotPos(cursorPos.x + dotRadius + 4.0f, cursorPos.y + ImGui::GetTextLineHeight() / 2.0f);
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 absoluteDotPos(windowPos.x + dotPos.x, windowPos.y + dotPos.y);
        ImGui::GetWindowDrawList()->AddCircleFilled(
            absoluteDotPos, 
            dotRadius, 
            dotColor, 
            10);
        
        ImGui::SetCursorPosX(cursorPos.x + 2 * dotRadius + 8.0f);
        
        const char *repo_path_cstr = repo_path.c_str();
        if (repo_path.size() > 80) {
            repo_path_cstr += repo_path.size() - 80;
        }
        if (ImGui::Selectable(repo_path_cstr, false, 0, ImVec2(0, 0))) {
            if (ImGui::GetIO().KeyCtrl) {
                "create_card"_sfn(std::format("dir:{}", repo_path));
            } else {
                select(repo_path);
            }
        }
        
        ImGui::EndGroup();
    }
}

void git::prepend_action_result(const std::string& action_name, const std::string& command_output) {
    updateRepoStatusSync();

    std::string output = command_output;
    if (output.empty()) {
        output = std::format("{} completed.", action_name);
    }

    std::lock_guard<std::mutex> lock(state_mutex);
    repo_status = std::format("{} result:\n{}\n\n{}", action_name, output, repo_status);
}

std::string git::generate_ai_commit_message(const std::string& staged_context, const std::string& repo_path) {
    if (!rouen::helpers::LLMConfig::is_configured()) {
        return {};
    }

    auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance();
    if (!llm_instance) {
        return {};
    }

    auto settings = rouen::helpers::LLMConfig::get_current_config();
    auto fetcher = std::make_shared<http::fetch>();

    llm_instance->add_instructions(
        "You are an expert software engineer writing git commit messages. "
        "Return ONLY a commit message ready to pass to `git commit`. "
        "Use imperative mood, be specific, and keep the first line under 72 characters. "
        "If additional detail is helpful, include a blank line followed by concise bullet points. "
        "Do not wrap the message in quotes or markdown."
    );

    std::string prompt = std::format(
        "Repository: {}\n\n"
        "Write a git commit message for these staged changes.\n\n"
        "Staged context:\n{}\n",
        repo_path,
        staged_context
    );

    auto response = llm_instance->sendMessage(
        prompt,
        [fetcher](const std::string& url, const std::string& data, auto header_client) {
            return fetcher->post(url, data, header_client);
        },
        "user",
        settings.model_name
    );

    if (response.choices.empty() || response.choices[0].message.content.empty()) {
        return {};
    }

    return trim_copy(response.choices[0].message.content);
}

void git::generate_ai_summary() {
    if (ai_request_pending) return;
    if (selected_repo.empty()) return;

    ai_request_pending = true;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        ai_status_cue = "Generating AI repository summary";
    }

    std::string repo = selected_repo;

    std::thread([this, repo]() {
        std::string status_output = git_model->getGitStatus(repo);
        std::string diff_context = git_model->getCachedDiff(repo);

        if (diff_context.empty() || diff_context.find("---DIFF---") == std::string::npos) {
            std::string git_path = CONFIG_SERVICE()->get_git_path();
            diff_context = rouen::models::GitProcessHelper::executeCommandInDirectory(
                repo,
                git_path + " diff --stat && printf '\\n---DIFF---\\n' && " + git_path + " diff"
            );
        }

        constexpr std::size_t max_context_length = 12000;
        if (diff_context.size() > max_context_length) {
            diff_context.resize(max_context_length);
            diff_context += "\n\n[truncated]";
        }

        std::string prompt = std::format(
            "Repository: {}\n\n"
            "Provide a concise summary of the current git status and changes.\n\n"
            "Git Status:\n{}\n\n"
            "Diff Context:\n{}\n",
            repo,
            status_output,
            diff_context.empty() ? "(No active diff / working directory clean)" : diff_context
        );

        std::string summary_result;
        try {
            if (rouen::helpers::LLMConfig::is_configured()) {
                if (auto llm_instance = rouen::helpers::LLMConfig::create_llm_instance()) {
                    auto settings = rouen::helpers::LLMConfig::get_current_config();
                    auto fetcher = std::make_shared<http::fetch>();

                    llm_instance->add_instructions(
                        "You are an expert software developer. "
                        "Summarize the git status and changes concisely with bullet points. "
                        "Focus on key structural changes, additions, or fixes."
                    );

                    auto response = llm_instance->sendMessage(
                        prompt,
                        [fetcher](const std::string& url, const std::string& data, auto header_client) {
                            return fetcher->post(url, data, header_client);
                        },
                        "user",
                        settings.model_name
                    );

                    if (!response.choices.empty() && !response.choices[0].message.content.empty()) {
                        summary_result = trim_copy(response.choices[0].message.content);
                    }
                }
            }
            if (summary_result.empty()) {
                summary_result = "AI LLM is not configured or returned no summary.";
            }
        } catch (const std::exception& e) {
            summary_result = std::format("Error generating AI summary: {}", e.what());
        }

        prepend_action_result("AI Summary", summary_result);
        ai_request_pending = false;
    }).detach();
}

void git::commit_all_with_ai_message() {
    if (ai_request_pending) return;
    if (selected_repo.empty()) return;

    ai_request_pending = true;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        ai_status_cue = "Generating AI commit message & committing...";
        last_commit_message.clear();
    }

    std::string repo = selected_repo;

    std::thread([this, repo]() {
        std::string add_result = git_model->gitAddAll(repo);
        std::string staged_context = git_model->getCachedDiff(repo);

        if (!git_model->hasStagedChanges(repo)) {
            prepend_action_result(
                "Commit All",
                add_result.empty() ? "No changes to commit after staging." : add_result + "\nNo changes to commit after staging."
            );
            ai_request_pending = false;
            return;
        }

        constexpr std::size_t max_context_length = 12000;
        if (staged_context.size() > max_context_length) {
            staged_context.resize(max_context_length);
            staged_context += "\n\n[truncated]";
        }

        std::string commit_message;
        try {
            commit_message = generate_ai_commit_message(staged_context, repo);
        } catch (const std::exception& e) {
            prepend_action_result("Commit All", std::format("Failed to generate AI commit message: {}", e.what()));
            ai_request_pending = false;
            return;
        }

        commit_message = trim_copy(commit_message);
        if (commit_message.empty()) {
            prepend_action_result("Commit All", "AI did not return a usable commit message.");
            ai_request_pending = false;
            return;
        }

        std::string commit_result = git_model->gitCommit(repo, commit_message);
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            last_commit_message = commit_message;
        }
        prepend_action_result(
            "Commit All",
            std::format("Generated commit message:\n{}\n\n{}", commit_message, commit_result)
        );

        ai_request_pending = false;
    }).detach();
}

std::string git::extract_github_repo_name(const std::string& remote_url) {
    size_t github_pos = remote_url.find("github.com");
    if (github_pos == std::string::npos) {
        return "";
    }
    
    size_t path_start = remote_url.find('/', github_pos);
    if (path_start == std::string::npos) {
        path_start = remote_url.find(':', github_pos);
    }
    if (path_start == std::string::npos) {
        return "";
    }
    
    path_start++;
    
    std::string path = remote_url.substr(path_start);
    if (path.ends_with(".git")) {
        path = path.substr(0, path.length() - 4);
    }
    
    return path;
}

void git::render_github_status_indicator() {
    std::string git_remote;
    std::string repo_name;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        git_remote = cached_git_remote;
        repo_name = cached_github_repo_name;
    }

    if (git_remote.find("github.com") == std::string::npos) {
        return;
    }
    
    ImGui::Separator();
    ImGui::TextColored(colors[0], ICON_MD_CLOUD " GitHub Integration");
    
    if (!repo_name.empty()) {
        ImGui::Text("Repository: %s", repo_name.c_str());
        
        if (ImGui::SmallButton(ICON_MD_OPEN_IN_BROWSER " Open on GitHub")) {
            std::string github_url = std::format("https://github.com/{}", repo_name);
            rouen::platform::open_file(github_url);
        }
        
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_BUILD " CI/CD Status")) {
            std::string actions_url = std::format("https://github.com/{}/actions", repo_name);
            rouen::platform::open_file(actions_url);
        }
        
        ImGui::Text("CI Status: %s", "Click CI/CD Status to view");
        
    } else {
        ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Could not parse repository name");
    }
}

std::string git::get_repository_status_json(const std::string& params) const {
    try {
        std::string requested_repo;
        
        if (!params.empty() && params.find("repo_path") != std::string::npos) {
            auto start = params.find("\"repo_path\"");
            if (start != std::string::npos) {
                start = params.find(":", start);
                if (start != std::string::npos) {
                    start = params.find("\"", start);
                    if (start != std::string::npos) {
                        start++;
                        auto end = params.find("\"", start);
                        if (end != std::string::npos) {
                            requested_repo = params.substr(start, end - start);
                        }
                    }
                }
            }
        }
        
        std::string result = "{\"success\":true,\"repositories\":[";
        bool first = true;
        const auto& repos = git_model->getRepos();
        
        if (!requested_repo.empty()) {
            auto repo_it = repos.find(requested_repo);
            if (repo_it != repos.end()) {
                git_model->getGitStatus(requested_repo);
                auto updated_repos = git_model->getRepos();
                auto updated_it = updated_repos.find(requested_repo);
                if (updated_it != updated_repos.end()) {
                    result += "{\"path\":\"" + requested_repo + "\",";
                    result += "\"status\":\"" + git_status_to_string(updated_it->second) + "\",";
                    result += "\"ahead\":" + std::string(git_model->isBranchAhead(requested_repo) ? "true" : "false") + "}";
                }
            } else {
                return "{\"success\":false,\"error\":\"Repository not found: " + requested_repo + "\"}";
            }
        } else {
            for (const auto& [path, status] : repos) {
                git_model->getGitStatus(path);
            }
            const auto& updated_repos = git_model->getRepos();
            for (const auto& [path, status] : updated_repos) {
                if (!first) result += ",";
                result += "{\"path\":\"" + path + "\",";
                result += "\"status\":\"" + git_status_to_string(status) + "\",";
                result += "\"ahead\":" + std::string(git_model->isBranchAhead(path) ? "true" : "false") + "}";
                first = false;
            }
        }
        
        result += "]}";
        return result;
    } catch (const std::exception& e) {
        return "{\"success\":false,\"error\":\"Error getting repository status: " + std::string(e.what()) + "\"}";
    }
}

std::string git::get_repositories_needing_push_json() const {
    try {
        std::string result = "{\"success\":true,\"repositories\":[";
        bool first = true;
        const auto& repos = git_model->getRepos();
        
        for (const auto& [path, status] : repos) {
            if (git_model->isBranchAhead(path)) {
                if (!first) result += ",";
                result += "{\"path\":\"" + path + "\",";
                result += "\"status\":\"" + git_status_to_string(status) + "\"}";
                first = false;
            }
        }
        
        result += "]}";
        return result;
    } catch (const std::exception& e) {
        return "{\"success\":false,\"error\":\"Error getting repositories needing push: " + std::string(e.what()) + "\"}";
    }
}

std::string git::get_modified_repositories_json() const {
    try {
        std::string result = "{\"success\":true,\"repositories\":[";
        bool first = true;
        const auto& repos = git_model->getRepos();
        
        for (const auto& [path, status] : repos) {
            if (status != rouen::models::GitRepoStatus::Clean && 
                status != rouen::models::GitRepoStatus::Unknown) {
                if (!first) result += ",";
                result += "{\"path\":\"" + path + "\",";
                result += "\"status\":\"" + git_status_to_string(status) + "\",";
                result += "\"ahead\":" + std::string(git_model->isBranchAhead(path) ? "true" : "false") + "}";
                first = false;
            }
        }
        
        result += "]}";
        return result;
    } catch (const std::exception& e) {
        return "{\"success\":false,\"error\":\"Error getting modified repositories: " + std::string(e.what()) + "\"}";
    }
}

std::string git::git_status_to_string(rouen::models::GitRepoStatus status) {
    switch (status) {
        case rouen::models::GitRepoStatus::Clean: return "clean";
        case rouen::models::GitRepoStatus::Modified: return "modified";
        case rouen::models::GitRepoStatus::Untracked: return "untracked";
        case rouen::models::GitRepoStatus::Staged: return "staged";
        case rouen::models::GitRepoStatus::Conflict: return "conflict";
        case rouen::models::GitRepoStatus::Detached: return "detached";
        case rouen::models::GitRepoStatus::Unknown: return "unknown";
        default: return "unknown";
    }
}
