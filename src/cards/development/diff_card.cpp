#include "diff_card.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "../../helpers/config_service.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../models/git_process_helper.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {

diff_card::diff_card(std::string_view uri) {
    name("Diff & Staging Buffer");
    width = 850.0f;
    colors[0] = ImVec4(0.2f, 0.45f, 0.7f, 1.0f); // Primary blue
    handle_uri(uri);
}

std::string diff_card::get_uri() const {
    return "diff";
}

bool diff_card::matches_uri(std::string_view uri) const {
    return uri == "diff" || uri.starts_with("diff:") || uri.starts_with("diff/");
}

void diff_card::handle_uri(std::string_view uri) {
    uri_param_ = std::string(uri);

    if (uri_param_.starts_with("diff:")) {
        std::string target = uri_param_.substr(5);
        if (target == "staged") {
            load_staged_edit(0);
            return;
        }
        if (!target.empty()) {
            load_file_vs_head(target);
            return;
        }
    }

    // Default: Check if staged edits exist
    auto& editor = rouen::helpers::CodeEditorService::instance();
    if (editor.has_staged_edits()) {
        load_staged_edit(0);
        return;
    }

    // Check active file in editor
    try {
        auto get_active = registrar::get<std::function<std::string()>>("editor_get_active_file");
        if (get_active && *get_active) {
            std::string active_path = (*get_active)();
            if (!active_path.empty() && std::filesystem::exists(active_path)) {
                load_file_vs_head(active_path);
                return;
            }
        }
    } catch (...) {}

    // Fallback: check git modified files in current working directory
    refresh_sources();
    if (!git_modified_files_.empty()) {
        load_file_vs_head(git_modified_files_[0]);
    }
}

std::vector<card::mcp_function> diff_card::get_mcp_functions() const {
    return {};
}

void diff_card::load_staged_edit(size_t index) {
    auto& editor = rouen::helpers::CodeEditorService::instance();
    auto staged_list = editor.get_staged_edits();
    if (staged_list.empty()) {
        diff_ = rouen::helpers::DiffFile{};
        file_path_.clear();
        file_name_ = "No Staged Edits";
        window_title = "Diff & Staging Buffer";
        source_mode_ = DiffSource::StagedAI;
        return;
    }

    if (index >= staged_list.size()) index = 0;
    selected_staged_idx_ = static_cast<int>(index);

    const auto& item = staged_list[index];
    current_staged_id_ = item.id;
    file_path_ = item.file_path;
    file_name_ = std::filesystem::path(file_path_).filename().string();
    base_content_ = item.original_content;
    proposed_content_ = item.proposed_content;
    diff_ = item.diff;
    source_mode_ = DiffSource::StagedAI;

    window_title = std::format("Diff: {} (+{} -{}) [Staged AI]",
        file_name_, diff_.total_additions, diff_.total_deletions);
}

void diff_card::load_file_vs_head(const std::string& full_path) {
    std::string resolved = ProcessHelper::expandTilde(full_path);
    if (!std::filesystem::exists(resolved)) return;


    file_path_ = resolved;
    file_name_ = std::filesystem::path(resolved).filename().string();
    source_mode_ = DiffSource::GitHead;

    // Read current disk content
    {
        std::ifstream f(resolved, std::ios::binary);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            proposed_content_ = ss.str();
        } else {
            proposed_content_.clear();
        }
    }

    // Locate git repository root
    std::filesystem::path cur = std::filesystem::path(resolved).parent_path();
    repo_root_.clear();
    while (!cur.empty() && cur != cur.root_path()) {
        if (std::filesystem::exists(cur / ".git")) {
            repo_root_ = cur.string();
            break;
        }
        cur = cur.parent_path();
    }

    base_content_.clear();
    if (!repo_root_.empty()) {
        std::string rel_path = std::filesystem::relative(resolved, repo_root_).string();
        std::string git_path = CONFIG_SERVICE()->get_git_path();
        std::string cmd = std::format("{} show HEAD:\"{}\"", git_path, rel_path);
        base_content_ = rouen::models::GitProcessHelper::executeCommandInDirectory(repo_root_, cmd);
        // If git show failed (e.g. fatal: path not in HEAD), treat base as empty (new file)
        if (base_content_.starts_with("fatal:")) {
            base_content_.clear();
        }
    }

    recompute_diff();
}

void diff_card::load_arbitrary_diff(const std::string& old_str, const std::string& new_str, const std::string& label) {
    file_path_ = label;
    file_name_ = label;
    base_content_ = old_str;
    proposed_content_ = new_str;
    source_mode_ = DiffSource::CustomFile;
    recompute_diff();
}

void diff_card::recompute_diff() {
    diff_ = rouen::helpers::DiffEngine::compute_diff(base_content_, proposed_content_, file_name_, file_name_);
    window_title = std::format("Diff: {} (+{} -{})",
        file_name_, diff_.total_additions, diff_.total_deletions);
}

void diff_card::refresh_sources() {
    git_modified_files_.clear();
    std::string git_path = CONFIG_SERVICE()->get_git_path();
    std::string output = rouen::models::GitProcessHelper::executeCommandInDirectory(".", git_path + " status --porcelain");
    if (!output.empty()) {
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.size() > 3) {
                std::string rel = line.substr(3);
                // Strip quotes if any
                if (rel.front() == '"' && rel.back() == '"') {
                    rel = rel.substr(1, rel.size() - 2);
                }
                std::string full = std::filesystem::absolute(rel).string();
                if (std::filesystem::exists(full)) {
                    git_modified_files_.push_back(full);
                }
            }
        }
    }
}

void diff_card::accept_chunk(int chunk_id) {
    diff_.accept_chunk(chunk_id);
}

void diff_card::discard_chunk(int chunk_id) {
    diff_.discard_chunk(chunk_id);
}

void diff_card::reset_chunk(int chunk_id) {
    diff_.reset_chunk(chunk_id);
}

void diff_card::accept_all() {
    diff_.accept_all();
}

void diff_card::discard_all() {
    diff_.discard_all();
}

void diff_card::reset_all() {
    diff_.reset_all();
}

bool diff_card::apply_to_disk() {
    auto& editor = rouen::helpers::CodeEditorService::instance();

    if (source_mode_ == DiffSource::StagedAI) {
        if (current_staged_id_.empty()) return false;
        bool ok = editor.apply_staged_edit(current_staged_id_, true);
        if (ok) {
            status_message_ = ICON_MD_CHECK " Applied accepted chunks to disk. Automated syntax check passed.";
            status_message_is_error_ = false;
            status_message_timer_ = 5.0f;
            load_staged_edit(0);
            return true;
        } else {
            status_message_ = ICON_MD_ERROR " Failed to apply staged edit to disk.";
            status_message_is_error_ = true;
            status_message_timer_ = 6.0f;
            return false;
        }
    } else {
        // Git HEAD / Disk file mode
        if (file_path_.empty() || !std::filesystem::exists(file_path_)) return false;

        // In GitHead mode: base is HEAD, proposed is Disk.
        // Chunks with status Accepted will be kept. Chunks with status Discarded will revert to base (HEAD)!
        std::string final_content = rouen::helpers::DiffEngine::apply_selected_chunks(base_content_, diff_, false);

        auto write_res = editor.write_file(file_path_, final_content, true);
        if (write_res.success) {
            if (write_res.syntax_check_passed) {
                status_message_ = std::format(ICON_MD_CHECK " Changes applied to disk. Syntax check passed with 0 errors.");
                status_message_is_error_ = false;
            } else {
                status_message_ = std::format(ICON_MD_WARNING " Applied to disk, but compiler reported {} error(s).", write_res.error_count);
                status_message_is_error_ = true;
            }
            status_message_timer_ = 5.0f;
            load_file_vs_head(file_path_);
            return true;
        } else {
            status_message_ = ICON_MD_ERROR " Failed writing changes to disk.";
            status_message_is_error_ = true;
            status_message_timer_ = 6.0f;
            return false;
        }
    }
}

bool diff_card::render() {
    if (status_message_timer_ > 0.0f) {
        status_message_timer_ -= ImGui::GetIO().DeltaTime;
        if (status_message_timer_ <= 0.0f) {
            status_message_.clear();
        }
    }

    return render_window([this]() {
        render_toolbar();

        if (show_history_drawer_) {
            render_history_drawer();
            ImGui::Separator();
        }

        if (!diff_.has_changes) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::Text(ICON_MD_CHECK " File is clean. No differences found between base and proposed content.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            return;
        }

        if (view_mode_ == DiffViewMode::SideBySide) {
            render_side_by_side();
        } else {
            render_unified();
        }
    });
}

void diff_card::render_toolbar() {
    float avail_w = ImGui::GetContentRegionAvail().x;

    // Row 1: Source & Mode selectors
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // Source buttons
    auto& editor = rouen::helpers::CodeEditorService::instance();
    auto staged_list = editor.get_staged_edits();
    std::string staged_btn = std::format(ICON_MD_SMART_TOY " Staged AI ({})", staged_list.size());

    bool is_staged = (source_mode_ == DiffSource::StagedAI);
    if (is_staged) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::Button(staged_btn.c_str())) {
        load_staged_edit(0);
    }
    if (is_staged) ImGui::PopStyleColor();

    ImGui::SameLine();
    bool is_git = (source_mode_ == DiffSource::GitHead);
    if (is_git) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::Button(ICON_MD_CALL_SPLIT " Git vs HEAD")) {
        refresh_sources();
        if (!git_modified_files_.empty()) {
            load_file_vs_head(git_modified_files_[0]);
        } else if (!file_path_.empty()) {
            load_file_vs_head(file_path_);
        }
    }
    if (is_git) ImGui::PopStyleColor();

    // Mode Toggle (Right aligned)
    ImGui::SameLine(avail_w - 180.0f);
    if (view_mode_ == DiffViewMode::SideBySide) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::SmallButton(ICON_MD_COMPARE " Split")) {
        view_mode_ = DiffViewMode::SideBySide;
    }
    if (view_mode_ == DiffViewMode::SideBySide) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (view_mode_ == DiffViewMode::Unified) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::SmallButton(ICON_MD_VIEW_STREAM " Unified")) {
        view_mode_ = DiffViewMode::Unified;
    }
    if (view_mode_ == DiffViewMode::Unified) ImGui::PopStyleColor();

    // History Toggle
    ImGui::SameLine();
    auto history = editor.get_history();
    std::string hist_label = std::format(ICON_MD_HISTORY " History ({})", history.size());
    if (show_history_drawer_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.35f, 0.45f, 1.0f));
    if (ImGui::SmallButton(hist_label.c_str())) {
        show_history_drawer_ = !show_history_drawer_;
    }
    if (show_history_drawer_) ImGui::PopStyleColor();

    // Row 2: File Selector & Stats
    ImGui::Spacing();
    if (source_mode_ == DiffSource::StagedAI && staged_list.size() > 1) {
        std::string combo_preview = std::format("[{}] {}", selected_staged_idx_ + 1, file_name_);
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::BeginCombo("##staged_combo", combo_preview.c_str())) {
            for (size_t i = 0; i < staged_list.size(); ++i) {
                bool is_sel = (selected_staged_idx_ == static_cast<int>(i));
                std::string item_name = std::format("[{}] {}", i + 1, std::filesystem::path(staged_list[i].file_path).filename().string());
                if (ImGui::Selectable(item_name.c_str(), is_sel)) {
                    load_staged_edit(i);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    } else if (source_mode_ == DiffSource::GitHead && git_modified_files_.size() > 1) {
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::BeginCombo("##git_combo", file_name_.c_str())) {
            for (size_t i = 0; i < git_modified_files_.size(); ++i) {
                bool is_sel = (selected_git_file_idx_ == static_cast<int>(i));
                std::string fname = std::filesystem::path(git_modified_files_[i]).filename().string();
                if (ImGui::Selectable(fname.c_str(), is_sel)) {
                    selected_git_file_idx_ = static_cast<int>(i);
                    load_file_vs_head(git_modified_files_[i]);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Target: %s", file_name_.c_str());
        ImGui::SameLine();
    }

    // Stats badge
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "+%d", diff_.total_additions);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "-%d", diff_.total_deletions);
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu hunks)", diff_.chunks.size());

    // Row 3: Bulk Actions & Apply to Disk
    ImGui::SameLine(avail_w - 320.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.35f, 0.2f, 0.8f));
    if (ImGui::SmallButton(ICON_MD_CHECK " Accept All")) {
        accept_all();
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.18f, 0.18f, 0.8f));
    if (ImGui::SmallButton(ICON_MD_CLOSE " Discard All")) {
        discard_all();
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.6f, 0.35f, 1.0f));
    if (ImGui::SmallButton(ICON_MD_SAVE " Apply to Disk")) {
        apply_to_disk();
    }
    ImGui::PopStyleColor(2);

    ImGui::PopStyleVar();

    // Flash notification message
    if (!status_message_.empty()) {
        ImVec4 col = status_message_is_error_ ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
        ImGui::TextColored(col, "%s", status_message_.c_str());
    }

    ImGui::Separator();
}

void diff_card::render_side_by_side() {
    float avail_w = ImGui::GetContentRegionAvail().x;
    float col_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // Header labels
    ImGui::TextColored(ImVec4(0.7f, 0.75f, 0.85f, 1.0f), "Base / HEAD (%s)", file_name_.c_str());
    ImGui::SameLine(col_w + ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextColored(ImVec4(0.7f, 0.75f, 0.85f, 1.0f), "Proposed / Working Tree");
    ImGui::Separator();

    ImGui::BeginChild("side_by_side_diff_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& chunk : diff_.chunks) {
        // Chunk Header Bar
        ImGui::PushID(chunk.id);
        ImVec2 bar_p0 = ImGui::GetCursorScreenPos();
        ImVec2 bar_p1 = ImVec2(bar_p0.x + avail_w, bar_p0.y + 26.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(bar_p0, bar_p1, IM_COL32(30, 38, 52, 220), 4.0f);

        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Hunk #%d: %s", chunk.id, chunk.get_header().c_str());

        // Status badge
        ImGui::SameLine();
        if (chunk.status == rouen::helpers::ChunkStatus::Accepted) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "[ACCEPTED " ICON_MD_CHECK "]");
        } else if (chunk.status == rouen::helpers::ChunkStatus::Discarded) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[DISCARDED " ICON_MD_CLOSE "]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[PENDING]");
        }

        // Per-chunk controls (right aligned)
        ImGui::SameLine(avail_w - 180.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 0.8f));
        if (ImGui::SmallButton(ICON_MD_CHECK " Accept")) {
            accept_chunk(chunk.id);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, 0.8f));
        if (ImGui::SmallButton(ICON_MD_CLOSE " Discard")) {
            discard_chunk(chunk.id);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_REFRESH " Reset")) {
            reset_chunk(chunk.id);
        }

        ImGui::Spacing();

        // Render aligned lines for left and right columns
        for (const auto& line : chunk.lines) {
            float line_start_y = ImGui::GetCursorPosY();

            // Left Pane (Base)
            ImGui::SetCursorPosX(10.0f);
            if (line.type == rouen::helpers::DiffLineType::Deletion) {
                ImVec2 p_min = ImGui::GetCursorScreenPos();
                ImVec2 p_max = ImVec2(p_min.x + col_w - 10.0f, p_min.y + ImGui::GetTextLineHeightWithSpacing());
                ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(110, 30, 30, 85));

                ImGui::TextDisabled("%4d ", line.old_line_num);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "- %s", line.text.c_str());
            } else if (line.type == rouen::helpers::DiffLineType::Context) {
                ImGui::TextDisabled("%4d ", line.old_line_num);
                ImGui::SameLine();
                ImGui::TextUnformatted(line.text.c_str());
            } else {
                // Addition: blank placeholder on left
                ImGui::Dummy(ImVec2(col_w - 10.0f, ImGui::GetTextLineHeight()));
            }

            // Right Pane (Proposed)
            ImGui::SetCursorPosY(line_start_y);
            ImGui::SetCursorPosX(col_w + ImGui::GetStyle().ItemSpacing.x);

            if (line.type == rouen::helpers::DiffLineType::Addition) {
                ImVec2 p_min = ImGui::GetCursorScreenPos();
                ImVec2 p_max = ImVec2(p_min.x + col_w - 10.0f, p_min.y + ImGui::GetTextLineHeightWithSpacing());
                
                ImU32 bg_col = (chunk.status == rouen::helpers::ChunkStatus::Discarded)
                    ? IM_COL32(40, 40, 40, 70)
                    : IM_COL32(30, 110, 45, 85);
                ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, bg_col);

                ImGui::TextDisabled("%4d ", line.new_line_num);
                ImGui::SameLine();

                if (chunk.status == rouen::helpers::ChunkStatus::Discarded) {
                    ImGui::TextDisabled("+ %s (discarded)", line.text.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "+ %s", line.text.c_str());
                }
            } else if (line.type == rouen::helpers::DiffLineType::Context) {
                ImGui::TextDisabled("%4d ", line.new_line_num);
                ImGui::SameLine();
                ImGui::TextUnformatted(line.text.c_str());
            } else {
                // Deletion: blank placeholder on right
                ImGui::Dummy(ImVec2(col_w - 10.0f, ImGui::GetTextLineHeight()));
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void diff_card::render_unified() {
    float avail_w = ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("unified_diff_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& chunk : diff_.chunks) {
        ImGui::PushID(chunk.id);

        // Chunk Header Bar
        ImVec2 bar_p0 = ImGui::GetCursorScreenPos();
        ImVec2 bar_p1 = ImVec2(bar_p0.x + avail_w, bar_p0.y + 26.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(bar_p0, bar_p1, IM_COL32(30, 38, 52, 220), 4.0f);

        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Hunk #%d: %s", chunk.id, chunk.get_header().c_str());

        // Status badge
        ImGui::SameLine();
        if (chunk.status == rouen::helpers::ChunkStatus::Accepted) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "[ACCEPTED " ICON_MD_CHECK "]");
        } else if (chunk.status == rouen::helpers::ChunkStatus::Discarded) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[DISCARDED " ICON_MD_CLOSE "]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[PENDING]");
        }

        // Action buttons
        ImGui::SameLine(avail_w - 180.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 0.8f));
        if (ImGui::SmallButton(ICON_MD_CHECK " Accept")) {
            accept_chunk(chunk.id);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, 0.8f));
        if (ImGui::SmallButton(ICON_MD_CLOSE " Discard")) {
            discard_chunk(chunk.id);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_REFRESH " Reset")) {
            reset_chunk(chunk.id);
        }

        ImGui::Spacing();

        // Render unified rows
        for (const auto& line : chunk.lines) {
            ImVec2 p_min = ImGui::GetCursorScreenPos();
            ImVec2 p_max = ImVec2(p_min.x + avail_w, p_min.y + ImGui::GetTextLineHeightWithSpacing());

            if (line.type == rouen::helpers::DiffLineType::Addition) {
                ImU32 bg_col = (chunk.status == rouen::helpers::ChunkStatus::Discarded)
                    ? IM_COL32(40, 40, 40, 70)
                    : IM_COL32(30, 110, 45, 85);
                ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, bg_col);

                ImGui::TextDisabled("     %4d + ", line.new_line_num);
                ImGui::SameLine();
                if (chunk.status == rouen::helpers::ChunkStatus::Discarded) {
                    ImGui::TextDisabled("%s (discarded)", line.text.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "%s", line.text.c_str());
                }
            } else if (line.type == rouen::helpers::DiffLineType::Deletion) {
                ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(110, 30, 30, 85));

                ImGui::TextDisabled("%4d      - ", line.old_line_num);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", line.text.c_str());
            } else {
                ImGui::TextDisabled("%4d %4d   ", line.old_line_num, line.new_line_num);
                ImGui::SameLine();
                ImGui::TextUnformatted(line.text.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void diff_card::render_history_drawer() {
    auto& editor = rouen::helpers::CodeEditorService::instance();
    auto history = editor.get_history();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 0.95f));
    ImGui::BeginChild("history_drawer", ImVec2(0, 150.0f), true);

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "In-Memory Edit History Stack");
    ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);

    bool can_u = editor.can_undo();
    if (!can_u) ImGui::BeginDisabled();
    if (ImGui::SmallButton("↶ Undo Last")) {
        editor.undo();
        load_file_vs_head(file_path_);
    }
    if (!can_u) ImGui::EndDisabled();

    ImGui::SameLine();
    bool can_r = editor.can_redo();
    if (!can_r) ImGui::BeginDisabled();
    if (ImGui::SmallButton("↷ Redo")) {
        editor.redo();
        load_file_vs_head(file_path_);
    }
    if (!can_r) ImGui::EndDisabled();

    ImGui::Separator();

    if (history.empty()) {
        ImGui::TextDisabled("No edits recorded yet in this session.");
    } else {
        for (const auto& entry : history) {
            ImGui::PushID(entry.id.c_str());

            std::string fname = std::filesystem::path(entry.file_path).filename().string();
            ImGui::TextDisabled("•");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s", fname.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", entry.description.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "+%d", entry.additions);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "-%d", entry.deletions);

            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.2f, 0.2f, 0.8f));
            if (ImGui::SmallButton("↺ Rollback")) {
                editor.rollback_to(entry.id);
                load_file_vs_head(file_path_);
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace rouen::cards
