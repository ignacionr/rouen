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
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../fonts.hpp"
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
    if (diff_.chunks.empty()) {
        current_hunk_idx_ = 0;
    } else if (current_hunk_idx_ >= diff_.chunks.size()) {
        current_hunk_idx_ = diff_.chunks.size() - 1;
    }
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
    rouen::ui::imgui_ui_context_impl ui;
    return render(ui);
}

bool diff_card::render(rouen::ui::ui_context& ui) {
    if (status_message_timer_ > 0.0f) {
        status_message_timer_ -= ImGui::GetIO().DeltaTime;
        if (status_message_timer_ <= 0.0f) {
            status_message_.clear();
        }
    }

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

std::string diff_card::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::vector<glz::json_t> body;

    // 1. Header Container
    glz::json_t header;
    header["type"] = "Container";
    header["style"] = "emphasis";

    glz::json_t col_set;
    col_set["type"] = "ColumnSet";
    std::vector<glz::json_t> columns;

    // Column 1: Emoji icon ⚖️ (\u2696\ufe0f -> UTF-8 "\xE2\x9A\x96\xEF\xB8\x8F")
    glz::json_t icon_col;
    icon_col["type"] = "Column";
    icon_col["width"] = "auto";
    std::vector<glz::json_t> icon_items;
    glz::json_t icon_tb;
    icon_tb["type"] = "TextBlock";
    icon_tb["text"] = "\xE2\x9A\x96\xEF\xB8\x8F";
    icon_tb["size"] = "Large";
    icon_items.push_back(std::move(icon_tb));
    icon_col["items"] = std::move(icon_items);
    columns.push_back(std::move(icon_col));

    // Column 2: Title & Path subtitle
    glz::json_t title_col;
    title_col["type"] = "Column";
    title_col["width"] = "stretch";
    std::vector<glz::json_t> title_items;
    glz::json_t title_tb;
    title_tb["type"] = "TextBlock";
    title_tb["text"] = "Unified Diff Inspector";
    title_tb["weight"] = "Bolder";
    title_tb["size"] = "Large";
    title_items.push_back(std::move(title_tb));

    glz::json_t path_tb;
    path_tb["type"] = "TextBlock";
    path_tb["text"] = file_path_.empty() ? "No file selected" : file_path_;
    path_tb["isSubtle"] = true;
    path_tb["size"] = "Small";
    path_tb["spacing"] = "None";
    title_items.push_back(std::move(path_tb));

    title_col["items"] = std::move(title_items);
    columns.push_back(std::move(title_col));

    // Column 3: Stats badge "+X / -Y lines"
    glz::json_t badge_col;
    badge_col["type"] = "Column";
    badge_col["width"] = "auto";
    std::vector<glz::json_t> badge_items;
    glz::json_t badge_tb;
    badge_tb["type"] = "TextBlock";
    badge_tb["text"] = std::format("+{} / -{} lines", diff_.total_additions, diff_.total_deletions);
    badge_tb["weight"] = "Bolder";
    badge_tb["color"] = "Good";
    badge_tb["size"] = "Small";
    badge_items.push_back(std::move(badge_tb));
    badge_col["items"] = std::move(badge_items);
    columns.push_back(std::move(badge_col));

    col_set["columns"] = std::move(columns);
    std::vector<glz::json_t> header_items;
    header_items.push_back(std::move(col_set));
    header["items"] = std::move(header_items);
    body.push_back(std::move(header));

    // 2. FactSet Container
    glz::json_t fact_set;
    fact_set["type"] = "FactSet";
    std::vector<glz::json_t> facts;

    glz::json_t fact_file;
    fact_file["title"] = "Target File:";
    fact_file["value"] = file_path_.empty() ? "None" : file_path_;
    facts.push_back(std::move(fact_file));

    glz::json_t fact_mode;
    fact_mode["title"] = "View Mode:";
    fact_mode["value"] = (view_mode_ == DiffViewMode::SideBySide) ? "Side-by-Side" : "Unified";
    facts.push_back(std::move(fact_mode));

    glz::json_t fact_src;
    fact_src["title"] = "Diff Source:";
    std::string src_name = (source_mode_ == DiffSource::StagedAI) ? "Staged AI" :
                           (source_mode_ == DiffSource::GitHead) ? "Git vs HEAD" : "Custom File";
    fact_src["value"] = src_name;
    facts.push_back(std::move(fact_src));

    glz::json_t fact_hunk;
    fact_hunk["title"] = "Hunks Count:";
    fact_hunk["value"] = std::to_string(diff_.chunks.size());
    facts.push_back(std::move(fact_hunk));

    glz::json_t fact_stats;
    fact_stats["title"] = "Line Stats:";
    fact_stats["value"] = std::format("+{} / -{} lines", diff_.total_additions, diff_.total_deletions);
    facts.push_back(std::move(fact_stats));

    fact_set["facts"] = std::move(facts);
    body.push_back(std::move(fact_set));

    // 3. File ChoiceSet Container
    glz::json_t file_container;
    file_container["type"] = "Container";
    std::vector<glz::json_t> file_container_items;

    glz::json_t choice_set;
    choice_set["type"] = "Input.ChoiceSet";
    choice_set["id"] = "file_selector";
    
    std::vector<glz::json_t> choices;
    if (source_mode_ == DiffSource::StagedAI) {
        auto staged_list = rouen::helpers::CodeEditorService::instance().get_staged_edits();
        for (const auto& item : staged_list) {
            std::string title = std::format("{} (+{}/-{})", item.file_path, item.diff.total_additions, item.diff.total_deletions);
            choices.push_back(glz::json_t::object_t{{"title", title}, {"value", item.file_path}});
        }
    } else if (source_mode_ == DiffSource::GitHead && !git_modified_files_.empty()) {
        for (const auto& f : git_modified_files_) {
            choices.push_back(glz::json_t::object_t{{"title", f}, {"value", f}});
        }
    }

    if (choices.empty()) {
        if (!file_path_.empty()) {
            std::string title = std::format("{} (+{}/-{})", file_path_, diff_.total_additions, diff_.total_deletions);
            choices.push_back(glz::json_t::object_t{{"title", title}, {"value", file_path_}});
        } else {
            choices.push_back(glz::json_t::object_t{{"title", "No modified files"}, {"value", "none"}});
        }
    }

    choice_set["label"] = std::format("Changed Files ({})", choices.size());
    choice_set["value"] = file_path_.empty() ? (choices.empty() ? "" : choices[0]["value"].get<std::string>()) : file_path_;
    choice_set["choices"] = std::move(choices);

    file_container_items.push_back(std::move(choice_set));
    file_container["items"] = std::move(file_container_items);
    body.push_back(std::move(file_container));

    // 4. Hunk Content Container
    glz::json_t hunk_container;
    hunk_container["type"] = "Container";
    hunk_container["style"] = "accent";
    hunk_container["separator"] = true;
    std::vector<glz::json_t> hunk_items;

    if (diff_.chunks.empty()) {
        glz::json_t empty_tb;
        empty_tb["type"] = "TextBlock";
        empty_tb["text"] = "File is clean. No differences found between base and proposed content.";
        empty_tb["fontType"] = "Monospace";
        empty_tb["size"] = "Small";
        hunk_items.push_back(std::move(empty_tb));
    } else {
        size_t h_idx = current_hunk_idx_ < diff_.chunks.size() ? current_hunk_idx_ : 0;
        const auto& chunk = diff_.chunks[h_idx];

        glz::json_t hunk_title;
        hunk_title["type"] = "TextBlock";
        hunk_title["text"] = std::format("Hunk {}/{}: {}", h_idx + 1, diff_.chunks.size(), chunk.get_header());
        hunk_title["weight"] = "Bolder";
        hunk_title["fontType"] = "Monospace";
        hunk_title["size"] = "Small";
        hunk_items.push_back(std::move(hunk_title));

        std::string hunk_text = chunk.get_header() + "\n";
        for (const auto& line : chunk.lines) {
            if (line.type == rouen::helpers::DiffLineType::Addition) {
                hunk_text += "+ ";
            } else if (line.type == rouen::helpers::DiffLineType::Deletion) {
                hunk_text += "- ";
            } else {
                hunk_text += "  ";
            }
            hunk_text += line.text;
            hunk_text += "\n";
        }
        if (!hunk_text.empty() && hunk_text.back() == '\n') {
            hunk_text.pop_back();
        }

        glz::json_t hunk_diff_tb;
        hunk_diff_tb["type"] = "TextBlock";
        hunk_diff_tb["text"] = hunk_text;
        hunk_diff_tb["fontType"] = "Monospace";
        hunk_diff_tb["wrap"] = true;
        hunk_diff_tb["size"] = "Small";
        hunk_items.push_back(std::move(hunk_diff_tb));
    }

    hunk_container["items"] = std::move(hunk_items);
    body.push_back(std::move(hunk_container));

    card["body"] = std::move(body);

    // 5. Actions
    std::vector<glz::json_t> actions;
    glz::json_t act_next;
    act_next["type"] = "Action.Execute";
    act_next["title"] = "Next Hunk";
    act_next["verb"] = "next_hunk";
    actions.push_back(std::move(act_next));

    glz::json_t act_stage;
    act_stage["type"] = "Action.Execute";
    act_stage["title"] = "Stage Hunk";
    act_stage["verb"] = "stage_hunk";
    actions.push_back(std::move(act_stage));

    glz::json_t act_discard;
    act_discard["type"] = "Action.Execute";
    act_discard["title"] = "Discard Hunk";
    act_discard["verb"] = "discard_hunk";
    actions.push_back(std::move(act_discard));

    glz::json_t act_toggle;
    act_toggle["type"] = "Action.Execute";
    act_toggle["title"] = "Toggle Side-by-Side";
    act_toggle["verb"] = "toggle_view_mode";
    actions.push_back(std::move(act_toggle));

    card["actions"] = std::move(actions);

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

void diff_card::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        std::string verb;
        glz::json_t data{};
        glz::json_t payload{};

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

        if (verb == "select_file") {
            std::string path;
            if (data.contains("file_path") && data["file_path"].holds<std::string>()) {
                path = data["file_path"].get<std::string>();
            } else if (data.contains("file_selector") && data["file_selector"].holds<std::string>()) {
                path = data["file_selector"].get<std::string>();
            } else if (data.contains("file") && data["file"].holds<std::string>()) {
                path = data["file"].get<std::string>();
            }
            if (!path.empty()) {
                std::string resolved = path;
                if (!repo_root_.empty() && std::filesystem::path(path).is_relative()) {
                    auto candidate = std::filesystem::path(repo_root_) / path;
                    if (std::filesystem::exists(candidate)) {
                        resolved = candidate.string();
                    }
                }
                if (std::filesystem::exists(resolved)) {
                    load_file_vs_head(resolved);
                } else {
                    auto staged = rouen::helpers::CodeEditorService::instance().get_staged_edits();
                    bool found_staged = false;
                    for (size_t i = 0; i < staged.size(); ++i) {
                        if (staged[i].file_path == path || staged[i].id == path) {
                            load_staged_edit(i);
                            found_staged = true;
                            break;
                        }
                    }
                    if (!found_staged) {
                        file_path_ = path;
                        file_name_ = std::filesystem::path(path).filename().string();
                    }
                }
                current_hunk_idx_ = 0;
            }
        } else if (verb == "stage_hunk") {
            int hunk_idx = -1;
            if (data.contains("hunk_idx")) {
                if (data["hunk_idx"].holds<double>()) {
                    hunk_idx = static_cast<int>(data["hunk_idx"].get<double>());
                } else if (data["hunk_idx"].holds<std::string>()) {
                    try {
                        hunk_idx = std::stoi(data["hunk_idx"].get<std::string>());
                    } catch (...) {}
                }
            }
            if (hunk_idx < 0) {
                hunk_idx = static_cast<int>(current_hunk_idx_);
            }
            if (!diff_.chunks.empty()) {
                if (hunk_idx >= 0 && static_cast<size_t>(hunk_idx) < diff_.chunks.size()) {
                    accept_chunk(diff_.chunks[static_cast<size_t>(hunk_idx)].id);
                }
            }
        } else if (verb == "discard_hunk") {
            int hunk_idx = -1;
            if (data.contains("hunk_idx")) {
                if (data["hunk_idx"].holds<double>()) {
                    hunk_idx = static_cast<int>(data["hunk_idx"].get<double>());
                } else if (data["hunk_idx"].holds<std::string>()) {
                    try {
                        hunk_idx = std::stoi(data["hunk_idx"].get<std::string>());
                    } catch (...) {}
                }
            }
            if (hunk_idx < 0) {
                hunk_idx = static_cast<int>(current_hunk_idx_);
            }
            if (!diff_.chunks.empty()) {
                if (hunk_idx >= 0 && static_cast<size_t>(hunk_idx) < diff_.chunks.size()) {
                    discard_chunk(diff_.chunks[static_cast<size_t>(hunk_idx)].id);
                }
            }
        } else if (verb == "next_hunk") {
            if (!diff_.chunks.empty()) {
                current_hunk_idx_ = (current_hunk_idx_ + 1) % diff_.chunks.size();
            } else {
                current_hunk_idx_++;
            }
        } else if (verb == "prev_hunk") {
            if (!diff_.chunks.empty()) {
                if (current_hunk_idx_ == 0) {
                    current_hunk_idx_ = diff_.chunks.size() - 1;
                } else {
                    current_hunk_idx_--;
                }
            } else if (current_hunk_idx_ > 0) {
                current_hunk_idx_--;
            }
        } else if (verb == "toggle_view_mode") {
            view_mode_ = (view_mode_ == DiffViewMode::SideBySide) ? DiffViewMode::Unified : DiffViewMode::SideBySide;
        } else if (verb == "toggle_adaptive_view") {
            adaptive_view_mode = !adaptive_view_mode;
        }

        last_adaptive_parse_time_ = {};
    } catch (const std::exception& e) {
        std::cerr << "[diff] Action error: " << e.what() << std::endl;
    } catch (...) {
    }
}

} // namespace rouen::cards
