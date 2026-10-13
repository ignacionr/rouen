#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <imgui.h>

#include "git.hpp"
#include "cards/interface/card.hpp"
#include "git_overlay.hpp"

#include "../../../external/IconsMaterialDesign.h"
#include "../../helpers/config_service.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"
#include "llm_host.hpp"
#include "models/git.hpp"
#include "models/git_process_helper.hpp"
#include "../../helpers/conventional_commit.hpp"
#include "../../fonts.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/glaze_include.hpp"

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

git::git(std::string_view initial_repo_path) {
    colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};  // Blue accent color
    colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color
    width = 600.0f;

    git_model = std::make_shared<rouen::models::git>();

    if (!initial_repo_path.empty()) {
        std::string_view actual_path = initial_repo_path;
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
    refresh_git_state();
}

git::~git() {
    if (alive_flag_) {
        alive_flag_->store(false);
    }
}

std::string git::get_effective_repo_path() const {
    if (!selected_repo.empty()) return selected_repo;
    if (!repo_path.empty()) return repo_path;
    if (git_model && !git_model->getRepoPaths().empty()) {
        return git_model->getRepoPaths().front();
    }
    try {
        auto cwd = std::filesystem::current_path().string();
        if (std::filesystem::exists(std::filesystem::path(cwd) / ".git")) {
            return cwd;
        }
    } catch (...) {}
    return "";
}

void git::refresh_git_state() const {
    std::string const repo = get_effective_repo_path();
    if (repo.empty()) {
        if (current_branch.empty()) current_branch = "main";
        if (branches.empty()) branches.push_back("main");
        return;
    }

    repo_path = repo;
    std::string git_path = CONFIG_SERVICE()->get_git_path();

    // 1. Current branch
    std::string br = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " branch --show-current");
    br = trim_copy(br);
    if (br.empty() || br == "HEAD") {
        br = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " rev-parse --abbrev-ref HEAD");
        br = trim_copy(br);
    }
    if (!br.empty() && br != "HEAD") {
        current_branch = br;
    } else if (current_branch.empty()) {
        current_branch = "main";
    }

    // 2. Branches
    std::string br_list_out = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " branch --format=\"%(refname:short)\"");
    std::vector<std::string> b_list;
    std::stringstream b_ss(br_list_out);
    std::string b_line;
    while (std::getline(b_ss, b_line)) {
        b_line = trim_copy(b_line);
        if (!b_line.empty()) {
            b_list.push_back(b_line);
        }
    }
    if (std::find(b_list.begin(), b_list.end(), current_branch) == b_list.end()) {
        b_list.insert(b_list.begin(), current_branch);
    }
    branches = std::move(b_list);

    // 3. Changed files
    std::string status_out = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " status --porcelain");
    std::vector<file_status_entry> files;
    std::stringstream s_ss(status_out);
    std::string s_line;
    while (std::getline(s_ss, s_line)) {
        if (s_line.size() >= 3) {
            std::string code = s_line.substr(0, 2);
            std::string path = trim_copy(s_line.substr(3));
            std::string st = "modified";
            if (code.find('?') != std::string::npos) st = "untracked";
            else if (code[0] != ' ' && code[0] != '?') st = "staged";
            files.push_back(file_status_entry{ .path = path, .status = st });
        }
    }
    changed_files = std::move(files);
    is_clean = changed_files.empty();
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
        std::string const path = std::string(uri.substr(4));
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
        std::lock_guard<std::mutex> const lock(state_mutex);
        current_repo = selected_repo;
    }

    auto alive = alive_flag_;
    auto model = git_model;

    std::thread([this, current_repo, alive, model]() {
        if (!alive || !alive->load() || !model) return;
        if (!current_repo.empty()) {
            std::string const status = model->getGitStatus(current_repo);
            std::string const git_remote = model->getGitRemote(current_repo);
            std::string const repo_name = extract_github_repo_name(git_remote);

            if (!alive->load()) return;
            std::lock_guard<std::mutex> const lock(state_mutex);
            if (!alive->load()) return;
            if (selected_repo == current_repo) {
                repo_status = status;
                cached_git_remote = git_remote;
                cached_github_repo_name = repo_name;
            }
        } else {
            for (const auto& path : model->getRepoPaths()) {
                if (!alive->load()) return;
                model->getGitStatus(path);
            }
        }
        if (alive->load()) {
            status_update_in_progress = false;
        }
    }).detach();
}

void git::fetch_punch_card_async(const std::string& target_repo_path) {
    if (target_repo_path.empty()) return;

    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        if (punch_card_data_.loaded && punch_card_data_.repo_path == target_repo_path) {
            return; // Already loaded in memory for this repo
        }
        punch_card_data_ = punch_card_info{};
        punch_card_data_.loading = true;
        punch_card_data_.repo_path = target_repo_path;
    }

    auto alive = alive_flag_;
    std::thread([this, target_repo_path, alive]() {
        if (!alive || !alive->load()) return;
        std::string git_bin = CONFIG_SERVICE()->get_git_path();
        std::string const cmd = std::format("{} --no-pager log --since=\"3 months ago\" --format=\"%at|%an|%ae\"", git_bin);
        std::string const output = rouen::models::GitProcessHelper::executeCommandInDirectory(target_repo_path, cmd);
        if (!alive || !alive->load()) return;

        punch_card_info data;
        data.repo_path = target_repo_path;
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
                int64_t const ts = std::stoll(line.substr(0, p1));
                std::string const aname = line.substr(p1 + 1, p2 - (p1 + 1));
                std::string const aemail = line.substr(p2 + 1);

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

        if (!alive || !alive->load()) return;
        {
            std::lock_guard<std::mutex> const lock(state_mutex);
            if (!alive || !alive->load()) return;
            if (selected_repo == target_repo_path) {
                punch_card_data_ = std::move(data);
            }
        }
    }).detach();
}

void git::fetch_recent_commits_7d_async(const std::string& target_repo_path) {
    if (target_repo_path.empty()) return;

    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        if (overlay_state_.recent_commits_loaded && overlay_state_.loaded_repo_path == target_repo_path) {
            return;
        }
        overlay_state_.loading_recent_commits = true;
        overlay_state_.recent_commits_loaded = false;
        overlay_state_.loaded_repo_path = target_repo_path;
        overlay_state_.recent_commits_7d.clear();
    }

    auto alive = alive_flag_;
    std::thread([this, target_repo_path, alive]() {
        if (!alive || !alive->load()) return;
        std::string git_bin = CONFIG_SERVICE()->get_git_path();
        std::time_t const now = std::time(nullptr);
        std::time_t seven_days_ago = now - static_cast<std::time_t>(7 * 86400);

        std::string const cmd = std::format("{} --no-pager log --since=\"{}\" --format=\"%H|%h|%at|%an|%ae|%s\"", git_bin, static_cast<int64_t>(seven_days_ago));
        std::string const output = rouen::models::GitProcessHelper::executeCommandInDirectory(target_repo_path, cmd);
        if (!alive || !alive->load()) return;

        std::vector<git_commit_summary> list;
        std::stringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
            line = trim_copy(line);
            if (line.empty()) continue;

            size_t const p1 = line.find('|');
            if (p1 == std::string::npos) continue;
            size_t const p2 = line.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;
            size_t const p3 = line.find('|', p2 + 1);
            if (p3 == std::string::npos) continue;
            size_t const p4 = line.find('|', p3 + 1);
            if (p4 == std::string::npos) continue;
            size_t const p5 = line.find('|', p4 + 1);
            if (p5 == std::string::npos) continue;

            git_commit_summary item;
            item.hash = line.substr(0, p1);
            item.short_hash = line.substr(p1 + 1, p2 - (p1 + 1));
            try {
                item.timestamp = std::stoll(line.substr(p2 + 1, p3 - (p2 + 1)));
            } catch (...) {
                item.timestamp = 0;
            }
            item.author_name = line.substr(p3 + 1, p4 - (p3 + 1));
            item.author_email = line.substr(p4 + 1, p5 - (p4 + 1));
            item.summary = line.substr(p5 + 1);

            auto const t = static_cast<std::time_t>(item.timestamp);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &t);
#else
            localtime_r(&t, &tm_buf);
#endif
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
            item.date_str = buf;

            list.push_back(std::move(item));
        }

        if (!alive || !alive->load()) return;
        {
            std::lock_guard<std::mutex> const lock(state_mutex);
            if (!alive || !alive->load()) return;
            if (selected_repo == target_repo_path) {
                overlay_state_.recent_commits_7d = std::move(list);
                overlay_state_.loading_recent_commits = false;
                overlay_state_.recent_commits_loaded = true;
            }
        }
    }).detach();
}

void git::fetch_commit_detail_async(const std::string& target_repo_path, const std::string& commit_hash) {
    if (target_repo_path.empty() || commit_hash.empty()) return;

    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        if (overlay_state_.selected_commit_detail.loaded && overlay_state_.selected_commit_detail.hash == commit_hash) {
            return;
        }
        overlay_state_.loading_commit_detail = true;
        overlay_state_.selected_commit_detail = git_commit_detail{};
        overlay_state_.selected_commit_detail.hash = commit_hash;
    }

    auto alive = alive_flag_;
    std::thread([this, target_repo_path, commit_hash, alive]() {
        if (!alive || !alive->load()) return;
        std::string git_bin = CONFIG_SERVICE()->get_git_path();
        std::string const cmd = std::format("{} --no-pager show --stat --format=\"%H%n%h%n%at%n%an%n%ae%n%B%n---END_HEADER---\" {}", git_bin, commit_hash);
        std::string const output = rouen::models::GitProcessHelper::executeCommandInDirectory(target_repo_path, cmd);
        if (!alive || !alive->load()) return;

        git_commit_detail detail;
        detail.hash = commit_hash;

        std::stringstream ss(output);
        std::string line;
        std::vector<std::string> header_lines;

        for (int i = 0; i < 5 && std::getline(ss, line); ++i) {
            header_lines.push_back(trim_copy(line));
        }

        if (header_lines.size() >= 5) {
            detail.hash = header_lines[0];
            detail.short_hash = header_lines[1];
            try {
                detail.timestamp = std::stoll(header_lines[2]);
            } catch (...) {
                detail.timestamp = 0;
            }
            detail.author_name = header_lines[3];
            detail.author_email = header_lines[4];

            auto const t = static_cast<std::time_t>(detail.timestamp);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &t);
#else
            localtime_r(&t, &tm_buf);
#endif
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
            detail.date_str = buf;
        }

        std::string message_body;
        while (std::getline(ss, line)) {
            if (line.starts_with("---END_HEADER---")) {
                break;
            }
            if (!message_body.empty()) message_body += "\n";
            message_body += line;
        }
        detail.full_message = trim_copy(message_body);

        std::string raw_stat;
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            raw_stat += line + "\n";
            std::string const trimmed = trim_copy(line);
            if (trimmed.find('|') != std::string::npos) {
                std::string const file_part = trim_copy(trimmed.substr(0, trimmed.find('|')));
                if (!file_part.empty()) {
                    detail.changed_files.push_back(file_part);
                }
            } else if (trimmed.find("changed") != std::string::npos || trimmed.find("insertion") != std::string::npos || trimmed.find("deletion") != std::string::npos) {
                detail.stats_summary = trimmed;
            }
        }
        detail.raw_stat_text = raw_stat;
        detail.loaded = true;

        {
            std::lock_guard<std::mutex> const lock(state_mutex);
            if (selected_repo == repo_path && overlay_state_.selected_commit_hash == commit_hash) {
                overlay_state_.selected_commit_detail = std::move(detail);
                overlay_state_.loading_commit_detail = false;
            }
        }
    }).detach();
}

bool git::select(const std::string& target_repo_path) {
    if (target_repo_path.empty()) {
        return false;
    }

    git_model->addRepository(target_repo_path);

    if (git_model->getRepos().find(target_repo_path) == git_model->getRepos().end()) {
        return false;
    }

    this->selected_repo = target_repo_path;
    this->repo_path = target_repo_path;
    name(std::format("Git: {}", std::filesystem::path(selected_repo).filename().string()));
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        cached_git_remote.clear();
        cached_github_repo_name.clear();
        punch_card_data_ = punch_card_info{};
        overlay_state_ = git_overlay_state{};
    }
    refresh_git_state();
    trigger_async_status_update();
    fetch_punch_card_async(target_repo_path);
    fetch_recent_commits_7d_async(target_repo_path);
    
    return true;
}

void git::back_to_list() {
    selected_repo.clear();
    repo_path.clear();
    name("Git Repos");
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        cached_git_remote.clear();
        cached_github_repo_name.clear();
        punch_card_data_ = punch_card_info{};
        overlay_state_ = git_overlay_state{};
    }
    refresh_git_state();
    trigger_async_status_update();
}

bool git::updateRepoStatusSync() {
    if (selected_repo.empty()) {
        return false;
    }
    
    std::string const status = git_model->getGitStatus(selected_repo);
    std::string const git_remote = git_model->getGitRemote(selected_repo);
    std::string const repo_name = extract_github_repo_name(git_remote);

    std::lock_guard<std::mutex> const lock(state_mutex);
    repo_status = status;
    cached_git_remote = git_remote;
    cached_github_repo_name = repo_name;
    return !repo_status.empty();
}

bool git::updateRepoStatus() {
    trigger_async_status_update();
    fetch_punch_card_async(selected_repo);
    fetch_recent_commits_7d_async(selected_repo);
    return true;
}

void git::render_ai_busy_cue() {
    double const time = ImGui::GetTime();
    auto dot_count = static_cast<std::size_t>(static_cast<long long>(time * 3.0) % 4);
    std::string const dots(dot_count, '.');

    std::string cue_text;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
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

void git::render_ai_summary_box() {
    std::string summary;
    std::chrono::system_clock::time_point timestamp;
    std::string summary_repo;
    bool const pending = ai_request_pending.load();
    std::string cue;

    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        summary = last_ai_summary;
        timestamp = last_ai_summary_time;
        summary_repo = last_ai_summary_repo;
        cue = ai_status_cue;
    }

    // If summary belongs to another repository, suppress it
    if (summary_repo != selected_repo && !summary.empty()) {
        return;
    }

    // Loading State
    if (pending) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
        ImGui::Text("%s  %s", ICON_MD_SYNC, cue.empty() ? "Generating AI summary..." : cue.c_str());
        ImGui::PopStyleColor();
        return;
    }

    if (summary.empty()) {
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Format header title with local time
    std::time_t const t_c = std::chrono::system_clock::to_time_t(timestamp);
    std::tm const tm_val = *std::localtime(&t_c);
    std::string const time_str = std::format("{:02d}:{:02d}:{:02d}", tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
    std::string const header_label = std::format("{} AI Summary ({})###GitAISummaryHeader", ICON_MD_AUTO_AWESOME, time_str);

    ImGuiTreeNodeFlags const tree_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;
    bool const open = ImGui::CollapsingHeader(header_label.c_str(), &ai_summary_expanded, tree_flags);

    if (open) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

        float const avail_height = std::clamp(ImGui::GetContentRegionAvail().y * 0.45f, 140.0f, 280.0f);

        if (ImGui::BeginChild("GitAISummaryBox", ImVec2(0.0f, avail_height), true, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            // Top action bar
            if (ImGui::SmallButton(ICON_MD_CONTENT_COPY " Copy Markdown")) {
                ImGui::SetClipboardText(summary.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_REFRESH " Regenerate")) {
                generate_ai_summary();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_CLEAR " Clear")) {
                std::lock_guard<std::mutex> const lock(state_mutex);
                last_ai_summary.clear();
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Render Markdown primitives
            const rouen::helpers::markdown_render_config md_config{
                .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
            };

            rouen::helpers::render_markdown_block(
                summary,
                md_config,
                [](const std::string& url) {
                    rouen::platform::open_url(url);
                }
            );
        }
        ImGui::EndChild();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}

void git::render_selected() {
    std::string current_status;
    std::string current_commit_msg;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
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
            std::lock_guard<std::mutex> const lock(state_mutex);
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
    if (ImGui::SmallButton("Visual Diff")) {
        "create_card"_sfn(std::format("diff:{}", selected_repo));
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Conventional Commit")) {
        open_conventional_commit_dialog();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Commit All")) {
        commit_all_with_ai_message();
    }

    render_conventional_commit_modal();
    render_ai_summary_box();

    if (!current_commit_msg.empty()) {
        ImGui::Separator();
        ImGui::Text("Last AI commit message:");
        ImGui::TextWrapped("%s", current_commit_msg.c_str());
    }
    
    render_github_status_indicator();
    render_recent_commits_7d();
    render_punch_card();
}

void git::render_punch_card() {
    punch_card_info data;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
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
                std::lock_guard<std::mutex> const lock(state_mutex);
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
    ImVec2 const cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(label_width + 24.0f * cell_size, grid_height));

    for (int w = 0; w < 7; ++w) {
        float const wf = static_cast<float>(w);
        float const y = cursor_pos.y + wf * cell_size + cell_size * 0.5f;
        draw_list->AddText(ImVec2(cursor_pos.x + 4.0f, y - 6.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), wday_names[w]);

        for (int h = 0; h < 24; ++h) {
            float const hf = static_cast<float>(h);
            float const x = cursor_pos.x + label_width + hf * cell_size + cell_size * 0.5f;
            ImVec2 const cell_min(cursor_pos.x + label_width + hf * cell_size, cursor_pos.y + wf * cell_size);
            ImVec2 const cell_max(cell_min.x + cell_size - cell_padding, cell_min.y + cell_size - cell_padding);

            draw_list->AddRectFilled(cell_min, cell_max, ImColor(25, 30, 40, 180), 3.0f);

            int const count = data.hour_matrix[w][h];
            if (count > 0 && data.max_commits_per_cell > 0) {
                float const relative_count = static_cast<float>(count) / static_cast<float>(data.max_commits_per_cell);
                float const radius = 2.5f + 6.0f * std::sqrt(relative_count);

                float const alpha = 0.35f + 0.65f * relative_count;
                ImColor const circle_color(
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

    float const hour_y = cursor_pos.y + 7.0f * cell_size + 4.0f;
    for (int h = 0; h < 24; h += 2) {
        float const hf = static_cast<float>(h);
        float const x = cursor_pos.x + label_width + hf * cell_size;
        draw_list->AddText(ImVec2(x, hour_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), std::format("{:02d}", h).c_str());
    }
}

void git::render_punch_card_heatmap(const punch_card_info& data) {
    constexpr float tile_size = 14.0f;
    constexpr float tile_padding = 3.0f;
    constexpr float label_width = 38.0f;
    constexpr const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    std::time_t const now_time = std::time(nullptr);
    std::tm now_tm{};
#ifdef _WIN32
    localtime_s(&now_tm, &now_time);
#else
    localtime_r(&now_time, &now_tm);
#endif

    std::time_t const start_time = now_time - static_cast<std::time_t>(90 * 86400);

    constexpr int num_weeks = 14;
    ImGui::Spacing();
    ImVec2 const cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(label_width + num_weeks * (tile_size + tile_padding), 7.0f * (tile_size + tile_padding) + 10.0f));

    for (int const w : {0, 2, 4, 6}) {
        float const wf = static_cast<float>(w);
        float const y = cursor_pos.y + wf * (tile_size + tile_padding);
        draw_list->AddText(ImVec2(cursor_pos.x + 4.0f, y - 2.0f),
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
            cursor_pos.x + label_width + week_idxf * (tile_size + tile_padding),
            cursor_pos.y + wdayf * (tile_size + tile_padding)
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

    punch_card_info punch_data;
    rouen::models::GitRepoStatus status;
    std::string github_repo_name;
    git_overlay_state local_overlay_state;

    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        punch_data = punch_card_data_;
        github_repo_name = cached_github_repo_name;
        local_overlay_state = overlay_state_;
    }

    status = git_model->getRepoStatus(selected_repo);

    git_overlay::render(selected_repo, punch_data, status, github_repo_name, local_overlay_state, colors.data());

    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        overlay_state_.position = local_overlay_state.position;
        overlay_state_.selected_commit_hash = local_overlay_state.selected_commit_hash;
    }
}

void git::render_recent_commits_7d() {
    git_overlay_state state;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        state = overlay_state_;
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_MD_SETTINGS " Video Overlay Position & Recent Commits", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        // 1) Overlay Position Selector
        const char* position_names[] = {
            "Top-Right", "Top-Left", "Bottom-Right", "Bottom-Left", "Center", "Top-Center", "Bottom-Center"
        };
        int current_pos_idx = static_cast<int>(state.position);
        ImGui::PushItemWidth(220.0f);
        if (ImGui::Combo("Overlay Screen Position", &current_pos_idx, position_names, IM_ARRAYSIZE(position_names))) {
            std::lock_guard<std::mutex> const lock(state_mutex);
            overlay_state_.position = static_cast<git_overlay_position>(current_pos_idx);
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        // 2) Recent Commits (Up to 7 Days Ago)
        ImGui::TextColored(colors[0], "%s Commits (Last 7 Days):", ICON_MD_HISTORY);
        ImGui::Spacing();

        if (state.loading_recent_commits) {
            ImGui::TextColored(colors[1], "%s Loading commits from the last 7 days...", ICON_MD_SYNC);
        } else if (!state.recent_commits_7d.empty()) {
            if (!state.selected_commit_hash.empty()) {
                ImGui::TextColored(colors[1], "Active Overlay Commit: %s", state.selected_commit_hash.substr(0, 7).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Return to Overview Slideshow")) {
                    std::lock_guard<std::mutex> const lock(state_mutex);
                    overlay_state_.selected_commit_hash.clear();
                }
                ImGui::Spacing();
            }

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.11f, 0.16f, 0.7f));
            if (ImGui::BeginChild("RecentCommits7dList", ImVec2(0, 160.0f), true)) {
                for (const auto& commit : state.recent_commits_7d) {
                    bool const is_selected = (commit.hash == state.selected_commit_hash);

                    std::string const label = std::format("{} | {} — {}", commit.short_hash, commit.author_name, commit.summary);
                    if (ImGui::Selectable(label.c_str(), is_selected)) {
                        std::string const target_hash = commit.hash;
                        std::string target_repo;
                        {
                            std::lock_guard<std::mutex> const lock(state_mutex);
                            overlay_state_.selected_commit_hash = target_hash;
                            target_repo = selected_repo;
                        }
                        fetch_commit_detail_async(target_repo, target_hash);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\nAuthor: %s <%s>\nDate: %s", commit.summary.c_str(), commit.author_name.c_str(), commit.author_email.c_str(), commit.date_str.c_str());
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else if (state.recent_commits_loaded) {
            ImGui::TextDisabled("No commits found in the last 7 days.");
        }
    }
}

bool git::render() {
    rouen::ui::imgui_ui_context_impl ui;
    return render(ui);
}

bool git::render(rouen::ui::ui_context& ui) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_status_update >= std::chrono::seconds(20)) {
        last_status_update = now;
        trigger_async_status_update();
    }

    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now_time = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_time - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now_time;
            }

            if (!adaptive_error_.empty()) {
                ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
            } else {
                adaptive_renderer_.render(
                    adaptive_bound_, adaptive_input_state_,
                    rouen::helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    rouen::helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

        bool const disabled = ai_request_pending.load();

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

std::string git::get_adaptive_card_json() const {
    refresh_git_state();

    std::string const repo = get_effective_repo_path();
    std::string const display_repo = !repo.empty() ? repo : (!repo_path.empty() ? repo_path : "/Users/ignaciorodriguez/src/rouen");
    std::string const display_branch = !current_branch.empty() ? current_branch : "main";

    std::string git_path = CONFIG_SERVICE()->get_git_path();

    // Determine tracking branch
    std::string tracking_branch;
    if (!repo.empty()) {
        std::string tb_out = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " rev-parse --abbrev-ref --symbolic-full-name @{u}");
        tracking_branch = trim_copy(tb_out);
        if (tracking_branch.starts_with("fatal:") || tracking_branch.find("no upstream") != std::string::npos) {
            tracking_branch.clear();
        }
    }

    // Branch Fact
    std::string branch_fact_val;
    if (!tracking_branch.empty()) {
        branch_fact_val = std::format("{} (tracking {})", display_branch, tracking_branch);
    } else {
        branch_fact_val = display_branch;
    }

    // Working Tree Fact
    std::string working_tree_fact_val;
    if (is_clean || changed_files.empty()) {
        working_tree_fact_val = "Working tree clean";
    } else {
        size_t mod_count = 0;
        size_t untracked_count = 0;
        size_t staged_count = 0;
        for (const auto& f : changed_files) {
            if (f.status == "untracked") untracked_count++;
            else if (f.status == "staged") staged_count++;
            else mod_count++;
        }
        std::vector<std::string> parts;
        if (mod_count > 0) parts.push_back(std::format("{} modified file{}", mod_count, mod_count > 1 ? "s" : ""));
        if (staged_count > 0) parts.push_back(std::format("{} staged file{}", staged_count, staged_count > 1 ? "s" : ""));
        if (untracked_count > 0) parts.push_back(std::format("{} untracked", untracked_count));

        if (!parts.empty()) {
            working_tree_fact_val = parts[0];
            for (size_t i = 1; i < parts.size(); ++i) {
                working_tree_fact_val += ", " + parts[i];
            }
        } else {
            working_tree_fact_val = "Working tree clean";
        }
    }

    // Upstream Sync Fact
    std::string upstream_sync_fact_val;
    if (!repo.empty() && !tracking_branch.empty()) {
        std::string rev_out = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " rev-list --left-right --count HEAD...@{u}");
        rev_out = trim_copy(rev_out);
        size_t tab_pos = rev_out.find('\t');
        if (tab_pos == std::string::npos) tab_pos = rev_out.find(' ');
        if (tab_pos != std::string::npos) {
            try {
                int ahead = std::stoi(rev_out.substr(0, tab_pos));
                int behind = std::stoi(trim_copy(rev_out.substr(tab_pos + 1)));
                upstream_sync_fact_val = std::format("{} commit{} ahead, {} behind", ahead, ahead == 1 ? "" : "s", behind);
            } catch (...) {
                upstream_sync_fact_val = "Up to date";
            }
        } else {
            upstream_sync_fact_val = "Up to date";
        }
    } else if (tracking_branch.empty()) {
        upstream_sync_fact_val = "No upstream branch configured";
    } else {
        upstream_sync_fact_val = "Up to date";
    }

    // Latest Commit Fact
    std::string latest_commit_fact_val;
    if (!repo.empty()) {
        std::string log_out = rouen::models::GitProcessHelper::executeCommandInDirectory(repo, git_path + " log -1 --format=\"%h - %s\"");
        latest_commit_fact_val = trim_copy(log_out);
    }
    if (latest_commit_fact_val.empty()) {
        latest_commit_fact_val = "No commits yet";
    }

    // Summary Text
    std::string summary_text;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        summary_text = last_ai_summary;
    }
    if (summary_text.empty()) {
        summary_text = "- No AI summary generated yet.\n- Click 'AI Summary' to analyze working changes.";
    }

    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";
    card["refreshIntervalMs"] = 5000.0;

    std::vector<glz::json_t> body;

    // 1. Header Container
    {
        glz::json_t header_container;
        header_container["type"] = "Container";
        header_container["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> columns;

        // Col 1: emoji
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> items;
            glz::json_t tb;
            tb["type"] = "TextBlock";
            tb["text"] = "📦";
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
            tb_title["text"] = "Git Repository Dashboard";
            tb_title["weight"] = "Bolder";
            tb_title["size"] = "Large";
            items.push_back(std::move(tb_title));

            glz::json_t tb_sub;
            tb_sub["type"] = "TextBlock";
            tb_sub["text"] = display_repo;
            tb_sub["isSubtle"] = true;
            tb_sub["size"] = "Small";
            tb_sub["spacing"] = "None";
            items.push_back(std::move(tb_sub));

            col["items"] = std::move(items);
            columns.push_back(std::move(col));
        }

        // Col 3: Branch Badge
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> items;

            glz::json_t tb_br;
            tb_br["type"] = "TextBlock";
            tb_br["text"] = display_branch;
            tb_br["weight"] = "Bolder";
            tb_br["color"] = "Accent";
            tb_br["size"] = "Medium";
            items.push_back(std::move(tb_br));

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

        facts.push_back(glz::json_t::object_t{
            {"title", "Branch:"},
            {"value", branch_fact_val}
        });
        facts.push_back(glz::json_t::object_t{
            {"title", "Working Tree:"},
            {"value", working_tree_fact_val}
        });
        facts.push_back(glz::json_t::object_t{
            {"title", "Upstream Sync:"},
            {"value", upstream_sync_fact_val}
        });
        facts.push_back(glz::json_t::object_t{
            {"title", "Latest Commit:"},
            {"value", latest_commit_fact_val}
        });

        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));
    }

    // 3. AI Summary Container
    {
        glz::json_t summary_container;
        summary_container["type"] = "Container";
        summary_container["style"] = "default";
        summary_container["separator"] = true;

        std::vector<glz::json_t> items;

        glz::json_t tb_hdr;
        tb_hdr["type"] = "TextBlock";
        tb_hdr["text"] = "AI Summary of Working Changes";
        tb_hdr["weight"] = "Bolder";
        tb_hdr["color"] = "Accent";
        items.push_back(std::move(tb_hdr));

        glz::json_t tb_summary;
        tb_summary["type"] = "TextBlock";
        tb_summary["text"] = summary_text;
        tb_summary["wrap"] = true;
        tb_summary["size"] = "Small";
        items.push_back(std::move(tb_summary));

        summary_container["items"] = std::move(items);
        body.push_back(std::move(summary_container));
    }

    // 4. Stage & Commit Container
    {
        glz::json_t input_container;
        input_container["type"] = "Container";
        input_container["separator"] = true;

        std::vector<glz::json_t> items;

        glz::json_t tb_title;
        tb_title["type"] = "TextBlock";
        tb_title["text"] = "Stage & Commit";
        tb_title["weight"] = "Bolder";
        items.push_back(std::move(tb_title));

        glz::json_t input_msg;
        input_msg["type"] = "Input.Text";
        input_msg["id"] = "commit_message";
        input_msg["placeholder"] = "Enter conventional commit message (e.g. feat(git): ...)";
        input_msg["isMultiline"] = false;
        items.push_back(std::move(input_msg));

        glz::json_t input_choice;
        input_choice["type"] = "Input.ChoiceSet";
        input_choice["id"] = "branch_name";
        input_choice["label"] = "Switch Branch";
        input_choice["value"] = display_branch;

        std::vector<glz::json_t> choices;
        std::vector<std::string> const& b_list = !branches.empty() ? branches : std::vector<std::string>{display_branch};
        for (const auto& b : b_list) {
            choices.push_back(glz::json_t::object_t{
                {"title", b},
                {"value", b}
            });
        }
        input_choice["choices"] = std::move(choices);
        items.push_back(std::move(input_choice));

        input_container["items"] = std::move(items);
        body.push_back(std::move(input_container));
    }

    card["body"] = std::move(body);

    // 5. Actions
    std::vector<glz::json_t> actions;
    {
        glz::json_t act_commit;
        act_commit["type"] = "Action.Execute";
        act_commit["title"] = "Commit";
        act_commit["verb"] = "git_commit";
        actions.push_back(std::move(act_commit));

        glz::json_t act_pull;
        act_pull["type"] = "Action.Execute";
        act_pull["title"] = "Pull";
        act_pull["verb"] = "git_pull";
        actions.push_back(std::move(act_pull));

        glz::json_t act_push;
        act_push["type"] = "Action.Execute";
        act_push["title"] = "Push";
        act_push["verb"] = "git_push";
        actions.push_back(std::move(act_push));

        glz::json_t act_summary;
        act_summary["type"] = "Action.Execute";
        act_summary["title"] = "AI Summary";
        act_summary["verb"] = "git_ai_summary";
        actions.push_back(std::move(act_summary));
    }
    card["actions"] = std::move(actions);

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

void git::handle_action(std::string_view action_json) {
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

        last_action = verb;

        if (verb == "git_commit") {
            std::string msg;
            if (data.is_object()) {
                if (data.contains("commit_message") && data["commit_message"].holds<std::string>()) {
                    msg = data["commit_message"].get<std::string>();
                } else if (data.contains("message") && data["message"].holds<std::string>()) {
                    msg = data["message"].get<std::string>();
                }
            }
            if (!msg.empty()) {
                std::lock_guard<std::mutex> const lock(state_mutex);
                last_commit_message = msg;
            }
            if (!selected_repo.empty() && git_model && !msg.empty()) {
                if (git_model->hasStagedChanges(selected_repo)) {
                    std::string const result = git_model->gitCommit(selected_repo, msg);
                    prepend_action_result("Commit", result);
                    refresh_git_state();
                }
            }
        } else if (verb == "git_pull") {
            if (!selected_repo.empty() && git_model) {
                std::string const result = git_model->gitPull(selected_repo);
                prepend_action_result("Pull", result);
                refresh_git_state();
            }
        } else if (verb == "git_push") {
            if (!selected_repo.empty() && git_model) {
                std::string const result = git_model->gitPush(selected_repo);
                prepend_action_result("Push", result);
                refresh_git_state();
            }
        } else if (verb == "git_checkout") {
            std::string target_branch;
            if (data.is_object()) {
                if (data.contains("branch_name") && data["branch_name"].holds<std::string>()) {
                    target_branch = data["branch_name"].get<std::string>();
                } else if (data.contains("branch") && data["branch"].holds<std::string>()) {
                    target_branch = data["branch"].get<std::string>();
                }
            }
            if (!target_branch.empty()) {
                last_checked_out_branch = target_branch;
                current_branch = target_branch;
                if (!selected_repo.empty()) {
                    std::string git_path = CONFIG_SERVICE()->get_git_path();
                    std::string const cmd = std::format("{} checkout {}", git_path, target_branch);
                    std::string const result = rouen::models::GitProcessHelper::executeCommandInDirectory(selected_repo, cmd);
                    prepend_action_result("Checkout", result);
                    refresh_git_state();
                }
            }
        } else if (verb == "git_stage_file") {
            std::string file_path;
            if (data.is_object()) {
                if (data.contains("file_path") && data["file_path"].holds<std::string>()) {
                    file_path = data["file_path"].get<std::string>();
                } else if (data.contains("path") && data["path"].holds<std::string>()) {
                    file_path = data["path"].get<std::string>();
                }
            }
            if (!file_path.empty()) {
                last_staged_file = file_path;
                if (!selected_repo.empty()) {
                    std::string git_path = CONFIG_SERVICE()->get_git_path();
                    std::string const cmd = std::format("{} add {}", git_path, file_path);
                    std::string const result = rouen::models::GitProcessHelper::executeCommandInDirectory(selected_repo, cmd);
                    prepend_action_result("Stage File", result);
                    refresh_git_state();
                }
            }
        } else if (verb == "git_ai_summary") {
            if (!selected_repo.empty()) {
                generate_ai_summary();
            }
        } else if (verb == "toggle_adaptive_view") {
            adaptive_view_mode = !adaptive_view_mode;
        }
    } catch (const std::exception& e) {
        std::cerr << "[git] Action error: " << e.what() << std::endl;
    }
}

void git::render_index() {
    const auto& repo_paths = git_model->getRepoPaths();

    for (const auto& rpath : repo_paths) {
        rouen::models::GitRepoStatus const status = git_model->getRepoStatus(rpath);
        ImColor const dotColor = getStatusColor(status);
        
        ImGui::BeginGroup();
        
        float const dotRadius = 4.0f;
        ImVec2 const cursorPos = ImGui::GetCursorPos();
        ImVec2 const dotPos(cursorPos.x + dotRadius + 4.0f, cursorPos.y + ImGui::GetTextLineHeight() / 2.0f);
        ImVec2 const windowPos = ImGui::GetWindowPos();
        ImVec2 const absoluteDotPos(windowPos.x + dotPos.x, windowPos.y + dotPos.y);
        ImGui::GetWindowDrawList()->AddCircleFilled(
            absoluteDotPos, 
            dotRadius, 
            dotColor, 
            10);
        
        ImGui::SetCursorPosX(cursorPos.x + 2 * dotRadius + 8.0f);
        
        const char *repo_path_cstr = rpath.c_str();
        if (rpath.size() > 80) {
            repo_path_cstr += rpath.size() - 80;
        }
        if (ImGui::Selectable(repo_path_cstr, false, 0, ImVec2(0, 0))) {
            if (ImGui::GetIO().KeyCtrl) {
                "create_card"_sfn(std::format("dir:{}", rpath));
            } else {
                select(rpath);
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

    std::lock_guard<std::mutex> const lock(state_mutex);
    repo_status = std::format("{} result:\n{}\n\n{}", action_name, output, repo_status);
}

std::string git::generate_ai_commit_message(const std::string& staged_context, const std::string& target_repo_path) {
    return rouen::helpers::ConventionalCommitGenerator::generate_commit_message(staged_context, "", target_repo_path);
}

void git::open_conventional_commit_dialog() {
    if (ai_request_pending) return;
    if (selected_repo.empty()) return;

    ai_request_pending = true;
    show_commit_dialog_ = true;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        ai_status_cue = "Generating AI Conventional Commit message...";
    }

    std::string const repo = selected_repo;

    std::thread([this, repo]() {
        std::string status_output = git_model->getGitStatus(repo);
        std::string staged_context = git_model->getCachedDiff(repo);
        if (staged_context.empty() || staged_context.find("---DIFF---") == std::string::npos) {
            std::string const git_path = CONFIG_SERVICE()->get_git_path();
            staged_context = rouen::models::GitProcessHelper::executeCommandInDirectory(
                repo,
                git_path + " diff --cached"
            );
            if (staged_context.empty()) {
                staged_context = rouen::models::GitProcessHelper::executeCommandInDirectory(
                    repo,
                    git_path + " diff"
                );
            }
        }

        std::string msg = rouen::helpers::ConventionalCommitGenerator::generate_commit_message(
            staged_context, status_output, repo
        );

        if (msg.empty()) {
            msg = "chore: update repository files";
        }

        {
            std::lock_guard<std::mutex> const lock(state_mutex);
            std::strncpy(commit_message_buf_, msg.c_str(), sizeof(commit_message_buf_) - 1);
            commit_message_buf_[sizeof(commit_message_buf_) - 1] = '\0';
        }

        ai_request_pending = false;
    }).detach();
}

void git::commit_with_reviewed_message() {
    std::string msg;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        msg = commit_message_buf_;
    }

    if (msg.empty() || selected_repo.empty()) {
        return;
    }

    std::string const repo = selected_repo;
    git_model->gitAddAll(repo);
    std::string result = git_model->gitCommit(repo, msg);
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        last_commit_message = msg;
        show_commit_dialog_ = false;
    }
    prepend_action_result("Conventional Commit", result);
}

void git::render_conventional_commit_modal() {
    if (!show_commit_dialog_) return;

    ImGui::Separator();
    ImGui::TextColored(colors[0], ICON_MD_SMART_TOY " AI Conventional Commit Review");

    if (ai_request_pending) {
        ImGui::TextColored(colors[4], "Synthesizing Conventional Commit message... |");
        return;
    }

    auto parsed = rouen::helpers::ConventionalCommitGenerator::parse_conventional_commit(commit_message_buf_);
    ImGui::Text("Type: ");
    ImGui::SameLine();
    ImGui::TextColored(parsed.is_valid ? colors[3] : colors[4], "%s", 
        parsed.type.empty() ? "(none)" : parsed.type.c_str());
    if (!parsed.scope.empty()) {
        ImGui::SameLine();
        ImGui::Text("Scope: (%s)", parsed.scope.c_str());
    }
    if (parsed.is_breaking) {
        ImGui::SameLine();
        ImGui::TextColored(colors[2], "[BREAKING CHANGE]");
    }

    ImGui::InputTextMultiline("##GitCommitMsg", commit_message_buf_, sizeof(commit_message_buf_), ImVec2(-1, 90));

    if (ImGui::Button(ICON_MD_SAVE " Stage & Commit")) {
        commit_with_reviewed_message();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_MD_CONTENT_COPY " Copy")) {
        ImGui::SetClipboardText(commit_message_buf_);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_MD_REFRESH " Regenerate")) {
        open_conventional_commit_dialog();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_MD_CANCEL " Cancel")) {
        show_commit_dialog_ = false;
    }
}

void git::generate_ai_summary() {
    if (ai_request_pending) return;
    if (selected_repo.empty()) return;

    ai_request_pending = true;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        ai_status_cue = "Generating AI repository summary";
    }

    std::string const repo = selected_repo;

    std::thread([this, repo]() {
        std::string status_output = git_model->getGitStatus(repo);
        std::string diff_context = git_model->getCachedDiff(repo);

        if (diff_context.empty() || diff_context.find("---DIFF---") == std::string::npos) {
            std::string const git_path = CONFIG_SERVICE()->get_git_path();
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

        std::string const prompt = std::format(
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

        {
            std::lock_guard<std::mutex> const lock(state_mutex);
            last_ai_summary = summary_result;
            last_ai_summary_time = std::chrono::system_clock::now();
            last_ai_summary_repo = repo;
            ai_status_cue.clear();
        }
        prepend_action_result("AI Summary", "Summary generated successfully.");
        ai_request_pending = false;
    }).detach();
}

void git::commit_all_with_ai_message() {
    if (ai_request_pending) return;
    if (selected_repo.empty()) return;

    ai_request_pending = true;
    {
        std::lock_guard<std::mutex> const lock(state_mutex);
        ai_status_cue = "Generating AI commit message & committing...";
        last_commit_message.clear();
    }

    std::string const repo = selected_repo;

    std::thread([this, repo]() {
        std::string const add_result = git_model->gitAddAll(repo);
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
            std::lock_guard<std::mutex> const lock(state_mutex);
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
    size_t const github_pos = remote_url.find("github.com");
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
        std::lock_guard<std::mutex> const lock(state_mutex);
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
            std::string const github_url = std::format("https://github.com/{}", repo_name);
            rouen::platform::open_file(github_url);
        }
        
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_BUILD " CI/CD Status")) {
            std::string const actions_url = std::format("https://github.com/{}/actions", repo_name);
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
                start = params.find(':', start);
                if (start != std::string::npos) {
                    start = params.find('"', start);
                    if (start != std::string::npos) {
                        start++;
                        auto end = params.find('"', start);
                        if (end != std::string::npos) {
                            requested_repo = params.substr(start, end - start);
                        }
                    }
                }
            }
        }
        
        std::string result = R"({"success":true,"repositories":[)";
        bool first = true;
        const auto& repos = git_model->getRepos();
        
        if (!requested_repo.empty()) {
            auto repo_it = repos.find(requested_repo);
            if (repo_it != repos.end()) {
                git_model->getGitStatus(requested_repo);
                auto updated_repos = git_model->getRepos();
                auto updated_it = updated_repos.find(requested_repo);
                if (updated_it != updated_repos.end()) {
                    result += std::format(R"({{"path":"{}","status":"{}","ahead":{}}})", requested_repo, git_status_to_string(updated_it->second), git_model->isBranchAhead(requested_repo) ? "true" : "false");
                }
            } else {
                return std::format(R"({{"success":false,"error":"Repository not found: {}"}})", requested_repo);
            }
        } else {
            for (const auto& [path, status] : repos) {
                git_model->getGitStatus(path);
            }
            const auto& updated_repos = git_model->getRepos();
            for (const auto& [path, status] : updated_repos) {
                if (!first) result += ",";
                result += std::format(R"({{"path":"{}","status":"{}","ahead":{}}})", path, git_status_to_string(status), git_model->isBranchAhead(path) ? "true" : "false");
                first = false;
            }
        }
        
        result += "]}";
        return result;
    } catch (const std::exception& e) {
        return std::format(R"({{"success":false,"error":"Error getting repository status: {}"}})", e.what());
    }
}

std::string git::get_repositories_needing_push_json() const {
    try {
        std::string result = R"({"success":true,"repositories":[)";
        bool first = true;
        const auto& repos = git_model->getRepos();
        
        for (const auto& [path, status] : repos) {
            if (git_model->isBranchAhead(path)) {
                if (!first) result += ",";
                result += std::format(R"({{"path":"{}","status":"{}"}})", path, git_status_to_string(status));
                first = false;
            }
        }
        
        result += "]}";
        return result;
    } catch (const std::exception& e) {
        return std::format(R"({{"success":false,"error":"Error getting repositories needing push: {}"}})", e.what());
    }
}

std::string git::get_modified_repositories_json() const {
    try {
        std::string result = R"({"success":true,"repositories":[)";
        bool first = true;
        const auto& repos = git_model->getRepos();
        
        for (const auto& [path, status] : repos) {
            if (status != rouen::models::GitRepoStatus::Clean && 
                status != rouen::models::GitRepoStatus::Unknown) {
                if (!first) result += ",";
                result += std::format(R"({{"path":"{}","status":"{}","ahead":{}}})", path, git_status_to_string(status), git_model->isBranchAhead(path) ? "true" : "false");
                first = false;
            }
        }
        
        result += "]}";
        return result;
    } catch (const std::exception& e) {
        return std::format(R"({{"success":false,"error":"Error getting modified repositories: {}"}})", e.what());
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
