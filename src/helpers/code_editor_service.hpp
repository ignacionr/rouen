#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <format>
#include <mutex>
#include <optional>

#include "glaze_include.hpp"
#include "process_helper.hpp"
#include "syntax_checker.hpp"
#include "code_indexer.hpp"
#include "diff_engine.hpp"
#include "../registrar.hpp"
#include <chrono>
#include <deque>

namespace rouen::helpers {

struct EditHistoryEntry {
    std::string id;
    std::string file_path;
    std::string description;
    std::string before_content;
    std::string after_content;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    int additions{0};
    int deletions{0};
    bool syntax_passed{true};

    struct glaze {
        using T = EditHistoryEntry;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "file_path", &T::file_path,
            "description", &T::description,
            "additions", &T::additions,
            "deletions", &T::deletions,
            "syntax_passed", &T::syntax_passed
        );
    };
};

struct StagedEdit {
    std::string id;
    std::string file_path;
    std::string description;
    std::string original_content;
    std::string proposed_content;
    DiffFile diff;
    std::string workspace_dir;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct ReadFileResult {
    std::string file_path;
    int total_lines{0};
    int start_line{1};
    int end_line{0};
    std::string content;
    bool success{true};
    std::string error_message;

    struct glaze {
        using T = ReadFileResult;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "total_lines", &T::total_lines,
            "start_line", &T::start_line,
            "end_line", &T::end_line,
            "content", &T::content,
            "success", &T::success,
            "error_message", &T::error_message
        );
    };
};

struct WriteFileResult {
    std::string file_path;
    bool success{true};
    int bytes_written{0};
    std::string message;
    bool syntax_check_passed{true};
    size_t error_count{0};
    size_t warning_count{0};
    std::vector<Diagnostic> diagnostics;

    struct glaze {
        using T = WriteFileResult;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "success", &T::success,
            "bytes_written", &T::bytes_written,
            "message", &T::message,
            "syntax_check_passed", &T::syntax_check_passed,
            "error_count", &T::error_count,
            "warning_count", &T::warning_count,
            "diagnostics", &T::diagnostics
        );
    };
};

struct ApplyPatchResult {
    std::string file_path;
    bool success{true};
    int replacement_count{0};
    std::string message;
    bool syntax_check_passed{true};
    size_t error_count{0};
    size_t warning_count{0};
    std::vector<Diagnostic> diagnostics;

    struct glaze {
        using T = ApplyPatchResult;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "success", &T::success,
            "replacement_count", &T::replacement_count,
            "message", &T::message,
            "syntax_check_passed", &T::syntax_check_passed,
            "error_count", &T::error_count,
            "warning_count", &T::warning_count,
            "diagnostics", &T::diagnostics
        );
    };
};

class CodeEditorService {
public:
    static CodeEditorService& instance() {
        static CodeEditorService s_instance;
        return s_instance;
    }

    [[nodiscard]] std::string get_default_workspace() const {
        const char* env_ws = std::getenv("ROUEN_WORKSPACE");
        if (!env_ws) env_ws = std::getenv("DEVELOPMENT_SRC_DIR");
        if (!env_ws) env_ws = std::getenv("GIT_SCAN_ROOT");
        if (env_ws && *env_ws && std::filesystem::exists(env_ws)) {
            return env_ws;
        }
        const char* home = std::getenv("HOME");
        if (home && *home) {
            std::filesystem::path const hp(home);
            std::vector<std::filesystem::path> const candidates = {
                hp / "src" / "rouen",
                hp / "Development" / "rouen",
                hp / "Projects" / "rouen"
            };
            for (const auto& c : candidates) {
                if (std::filesystem::exists(c)) {
                    return c.string();
                }
            }
        }
        try {
            auto cwd = std::filesystem::current_path();
            if (cwd != "/" && cwd != "C:\\" && cwd != "c:\\") {
                return cwd.string();
            }
        } catch (...) {}
        return "";
    }

    [[nodiscard]] std::string resolve_path(const std::string& path, const std::string& workspace_dir = "") const {
        std::string expanded = ProcessHelper::expandTilde(path);
        std::filesystem::path p(expanded);
        if (p.is_absolute() && std::filesystem::exists(p)) {
            return expanded;
        }
        if (!workspace_dir.empty()) {
            std::filesystem::path ws(ProcessHelper::expandTilde(workspace_dir));
            auto candidate = ws / expanded;
            if (std::filesystem::exists(candidate)) {
                return candidate.string();
            }
        }
        if (std::filesystem::exists(p)) {
            return expanded;
        }
        if (!p.is_absolute()) {
            std::string def_ws = get_default_workspace();
            if (!def_ws.empty()) {
                auto candidate = std::filesystem::path(def_ws) / expanded;
                if (std::filesystem::exists(candidate)) {
                    return candidate.string();
                }
            }
        }
        return expanded;
    }

    /**
     * Read a line-bounded chunk of a file with optional 1-based line numbers.
     */
    ReadFileResult read_file(
        const std::string& path,
        int start_line = 1,
        int end_line = -1,
        bool show_line_numbers = true,
        const std::string& workspace_dir = ""
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        ReadFileResult result;
        std::string resolved = resolve_path(path, workspace_dir);
        result.file_path = resolved;

        if (!std::filesystem::exists(resolved)) {
            result.success = false;
            result.error_message = "File does not exist: " + resolved;
            return result;
        }

        std::ifstream file(resolved, std::ios::binary);
        if (!file.is_open()) {
            result.success = false;
            result.error_message = "Failed to open file: " + resolved;
            return result;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }

        result.total_lines = static_cast<int>(lines.size());
        if (result.total_lines == 0) {
            result.start_line = 1;
            result.end_line = 0;
            result.content = "";
            return result;
        }

        int s = std::max(1, start_line);
        int e = (end_line <= 0 || end_line > result.total_lines) ? result.total_lines : end_line;
        if (s > e) {
            s = e;
        }

        // Context limit protection: at most 2000 lines per call
        if (e - s + 1 > 2000) {
            e = s + 1999;
        }

        result.start_line = s;
        result.end_line = e;

        std::ostringstream out;
        for (int i = s; i <= e; ++i) {
            if (show_line_numbers) {
                out << i << ": " << lines[static_cast<size_t>(i - 1)] << "\n";
            } else {
                out << lines[static_cast<size_t>(i - 1)] << "\n";
            }
        }
        result.content = out.str();
        return result;
    }

    /**
     * Write or overwrite a full file with automated syntax verification and index update.
     */
    WriteFileResult write_file(
        const std::string& path,
        const std::string& content,
        bool overwrite = false,
        const std::string& workspace_dir = ""
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        WriteFileResult result;
        std::string resolved = resolve_path(path, workspace_dir);
        if (!std::filesystem::path(resolved).is_absolute() && !std::filesystem::exists(resolved)) {
            std::string def_ws = !workspace_dir.empty() ? workspace_dir : get_default_workspace();
            if (!def_ws.empty()) {
                resolved = (std::filesystem::path(def_ws) / resolved).string();
            }
        }
        result.file_path = resolved;

        std::filesystem::path p(resolved);
        if (std::filesystem::exists(p) && !overwrite) {
            result.success = false;
            result.message = "File already exists. Specify 'overwrite': true or use 'code_apply_patch' for surgical edits.";
            return result;
        }

        std::error_code ec;
        auto parent_dir = p.parent_path();
        if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
            std::filesystem::create_directories(parent_dir, ec);
            if (ec) {
                result.success = false;
                result.message = "Failed to create directory: " + parent_dir.string() + " (" + ec.message() + ")";
                return result;
            }
        }

        std::string before_content;
        if (std::filesystem::exists(resolved)) {
            std::ifstream in(resolved, std::ios::binary);
            if (in.is_open()) {
                std::ostringstream ss;
                ss << in.rdbuf();
                before_content = ss.str();
            }
        }

        {
            std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                result.success = false;
                result.message = "Failed to open file for writing: " + resolved;
                return result;
            }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        result.bytes_written = static_cast<int>(content.size());
        notify_editor_reload(resolved);

        // Update code indexer
        CodeIndexer::instance().index_file(resolved, true);

        // Run automated syntax check for self-correction feedback
        auto syntax_res = SyntaxChecker::instance().check_file(resolved, workspace_dir);
        result.error_count = syntax_res.error_count;
        result.warning_count = syntax_res.warning_count;
        result.diagnostics = syntax_res.diagnostics;
        result.syntax_check_passed = syntax_res.success;

        // Record in in-memory undo buffer
        record_history_internal(resolved, before_content, content, "code_write_file", syntax_res.success);

        if (syntax_res.success) {
            result.message = std::format("File written successfully ({} bytes). Syntax check passed with 0 errors.", result.bytes_written);
        } else {
            result.message = std::format(
                "File written, but compiler reported {} syntax error(s):\n",
                syntax_res.error_count
            );
            for (const auto& d : syntax_res.diagnostics) {
                if (d.severity == "error") {
                    result.message += std::format("  Line {}: {}\n", d.line, d.message);
                }
            }
            result.message += "Please review diagnostics and repair with code_apply_patch.";
        }

        return result;
    }

    /**
     * Surgically replace a target chunk in a file with exact match validation
     * and automated compiler diagnostic feedback (self-correction loop).
     */
    ApplyPatchResult apply_patch(
        const std::string& path,
        const std::string& target_content,
        const std::string& replacement_content,
        int start_line = 1,
        int end_line = -1,
        bool allow_multiple = false,
        const std::string& workspace_dir = ""
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        ApplyPatchResult result;
        std::string resolved = resolve_path(path, workspace_dir);
        result.file_path = resolved;

        if (target_content.empty()) {
            result.success = false;
            result.message = "target_content cannot be empty.";
            return result;
        }

        if (!std::filesystem::exists(resolved)) {
            result.success = false;
            result.message = "File does not exist: " + resolved;
            return result;
        }

        std::string file_content;
        {
            std::ifstream file(resolved, std::ios::binary);
            if (!file.is_open()) {
                result.success = false;
                result.message = "Failed to open file: " + resolved;
                return result;
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            file_content = ss.str();
        }

        // Normalize target and file content line endings if needed for matching
        std::string target = target_content;
        bool file_has_crlf = (file_content.find("\r\n") != std::string::npos);
        bool target_has_crlf = (target.find("\r\n") != std::string::npos);

        if (file_has_crlf && !target_has_crlf) {
            target = to_crlf(target);
        } else if (!file_has_crlf && target_has_crlf) {
            target = to_lf(target);
        }

        // Determine line bounds in character offsets
        size_t search_start = 0;
        size_t search_end = file_content.size();

        if (start_line > 1 || end_line > 0) {
            int current_line = 1;
            size_t pos = 0;
            while (pos < file_content.size()) {
                if (current_line == start_line && search_start == 0 && start_line > 1) {
                    search_start = pos;
                }
                if (end_line > 0 && current_line == end_line + 1) {
                    search_end = pos;
                    break;
                }
                if (file_content[pos] == '\n') {
                    current_line++;
                }
                pos++;
            }
            if (search_start >= file_content.size() || search_start > search_end) {
                result.success = false;
                result.message = std::format("Specified start_line ({}) is beyond the end of the file.", start_line);
                return result;
            }
        }

        std::string search_region = file_content.substr(search_start, search_end - search_start);

        // Find occurrences within the designated search region
        std::vector<size_t> match_offsets;
        size_t match_pos = search_region.find(target, 0);
        while (match_pos != std::string::npos) {
            match_offsets.push_back(search_start + match_pos);
            match_pos = search_region.find(target, match_pos + target.size());
        }

        if (match_offsets.empty()) {
            result.success = false;
            result.message = "Target content was not found in " + resolved;
            if (start_line > 1 || end_line > 0) {
                result.message += std::format(" within lines {}-{}", start_line, end_line > 0 ? std::to_string(end_line) : "end");
            }
            result.message += ". Check indentation, line endings, and exact whitespace.";
            return result;
        }

        if (match_offsets.size() > 1 && !allow_multiple) {
            result.success = false;
            result.message = std::format(
                "Found {} occurrences of target content in {}. Please narrow start_line/end_line or provide more unique surrounding lines.",
                match_offsets.size(), resolved
            );
            return result;
        }

        // Adjust replacement line endings to match file conventions
        std::string replacement = replacement_content;
        if (file_has_crlf && replacement.find("\r\n") == std::string::npos) {
            replacement = to_crlf(replacement);
        } else if (!file_has_crlf && replacement.find("\r\n") != std::string::npos) {
            replacement = to_lf(replacement);
        }

        // Perform replacement
        std::string new_content;
        if (allow_multiple) {
            size_t last_idx = 0;
            for (size_t offset : match_offsets) {
                new_content.append(file_content, last_idx, offset - last_idx);
                new_content.append(replacement);
                last_idx = offset + target.size();
            }
            new_content.append(file_content, last_idx, file_content.size() - last_idx);
            result.replacement_count = static_cast<int>(match_offsets.size());
        } else {
            size_t offset = match_offsets[0];
            new_content.reserve(file_content.size() - target.size() + replacement.size());
            new_content.append(file_content, 0, offset);
            new_content.append(replacement);
            new_content.append(file_content, offset + target.size(), file_content.size() - (offset + target.size()));
            result.replacement_count = 1;
        }

        // Write modified content back to file
        {
            std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                result.success = false;
                result.message = "Failed to write modified content to: " + resolved;
                return result;
            }
            out.write(new_content.data(), static_cast<std::streamsize>(new_content.size()));
        }

        notify_editor_reload(resolved);

        // Update code indexer
        CodeIndexer::instance().index_file(resolved, true);

        // Run automated syntax check for self-correction feedback loop
        auto syntax_res = SyntaxChecker::instance().check_file(resolved, workspace_dir);
        result.error_count = syntax_res.error_count;
        result.warning_count = syntax_res.warning_count;
        result.diagnostics = syntax_res.diagnostics;
        result.syntax_check_passed = syntax_res.success;

        // Record in in-memory undo buffer
        record_history_internal(resolved, file_content, new_content, "code_apply_patch", syntax_res.success);

        if (syntax_res.success) {
            result.message = std::format(
                "Patch applied successfully ({} replacement{}). Syntax check passed with 0 errors.",
                result.replacement_count,
                result.replacement_count == 1 ? "" : "s"
            );
        } else {
            result.message = std::format(
                "Patch applied ({} replacement{}), but compiler reported {} syntax error(s):\n",
                result.replacement_count,
                result.replacement_count == 1 ? "" : "s",
                syntax_res.error_count
            );
            for (const auto& d : syntax_res.diagnostics) {
                if (d.severity == "error") {
                    result.message += std::format("  Line {}: {}\n", d.line, d.message);
                }
            }
            result.message += "Self-correction required: please review the diagnostics above and apply a fix.";
        }

        return result;
    }

    /**
     * Undo the most recent edit across any file. Restores previous content to disk,
     * updates the code indexer, runs syntax checks, and notifies the editor.
     */
    bool undo() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (undo_stack_.empty()) return false;

        EditHistoryEntry entry = std::move(undo_stack_.back());
        undo_stack_.pop_back();

        std::ofstream out(entry.file_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(entry.before_content.data(), static_cast<std::streamsize>(entry.before_content.size()));
        out.close();

        notify_editor_reload(entry.file_path);
        CodeIndexer::instance().index_file(entry.file_path, true);
        SyntaxChecker::instance().check_file(entry.file_path);

        redo_stack_.push_back(std::move(entry));
        return true;
    }

    /**
     * Redo the most recently undone edit.
     */
    bool redo() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (redo_stack_.empty()) return false;

        EditHistoryEntry entry = std::move(redo_stack_.back());
        redo_stack_.pop_back();

        std::ofstream out(entry.file_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(entry.after_content.data(), static_cast<std::streamsize>(entry.after_content.size()));
        out.close();

        notify_editor_reload(entry.file_path);
        CodeIndexer::instance().index_file(entry.file_path, true);
        SyntaxChecker::instance().check_file(entry.file_path);

        undo_stack_.push_back(std::move(entry));
        return true;
    }

    [[nodiscard]] bool can_undo() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !undo_stack_.empty();
    }

    [[nodiscard]] bool can_redo() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !redo_stack_.empty();
    }

    [[nodiscard]] std::vector<EditHistoryEntry> get_history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<EditHistoryEntry>(undo_stack_.rbegin(), undo_stack_.rend());
    }

    bool rollback_to(const std::string& edit_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(undo_stack_.begin(), undo_stack_.end(),
            [&edit_id](const EditHistoryEntry& e) { return e.id == edit_id; });
        if (it == undo_stack_.end()) return false;

        std::ofstream out(it->file_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(it->before_content.data(), static_cast<std::streamsize>(it->before_content.size()));
        out.close();

        notify_editor_reload(it->file_path);
        CodeIndexer::instance().index_file(it->file_path, true);
        SyntaxChecker::instance().check_file(it->file_path);

        redo_stack_.push_back(*it);
        undo_stack_.erase(it);
        return true;
    }

    void clear_history() {
        std::lock_guard<std::mutex> lock(mutex_);
        undo_stack_.clear();
        redo_stack_.clear();
    }

    /**
     * Staging Buffer: Stage a proposed patch in memory and compute its diff
     * without modifying the file on disk.
     */
    std::string stage_patch(
        const std::string& path,
        const std::string& target_content,
        const std::string& replacement_content,
        int start_line = 1,
        int end_line = -1,
        bool allow_multiple = false,
        const std::string& workspace_dir = "",
        const std::string& description = "AI Staged Patch"
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string resolved = resolve_path(path, workspace_dir);
        if (!std::filesystem::exists(resolved)) return "";

        std::ifstream file(resolved, std::ios::binary);
        if (!file.is_open()) return "";
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string file_content = ss.str();

        std::string target = target_content;
        bool file_has_crlf = (file_content.find("\r\n") != std::string::npos);
        bool target_has_crlf = (target.find("\r\n") != std::string::npos);
        if (file_has_crlf && !target_has_crlf) target = to_crlf(target);
        else if (!file_has_crlf && target_has_crlf) target = to_lf(target);

        size_t search_start = 0;
        size_t search_end = file_content.size();
        if (start_line > 1 || end_line > 0) {
            int current_line = 1;
            size_t pos = 0;
            while (pos < file_content.size()) {
                if (current_line == start_line && search_start == 0 && start_line > 1) search_start = pos;
                if (end_line > 0 && current_line == end_line + 1) { search_end = pos; break; }
                if (file_content[pos] == '\n') current_line++;
                pos++;
            }
        }
        std::string search_region = file_content.substr(search_start, search_end - search_start);
        std::vector<size_t> match_offsets;
        size_t match_pos = search_region.find(target, 0);
        while (match_pos != std::string::npos) {
            match_offsets.push_back(search_start + match_pos);
            match_pos = search_region.find(target, match_pos + target.size());
        }
        if (match_offsets.empty()) return "";
        if (match_offsets.size() > 1 && !allow_multiple) return "";

        std::string replacement = replacement_content;
        if (file_has_crlf && replacement.find("\r\n") == std::string::npos) replacement = to_crlf(replacement);
        else if (!file_has_crlf && replacement.find("\r\n") != std::string::npos) replacement = to_lf(replacement);

        std::string new_content;
        if (allow_multiple) {
            size_t last_idx = 0;
            for (size_t offset : match_offsets) {
                new_content.append(file_content, last_idx, offset - last_idx);
                new_content.append(replacement);
                last_idx = offset + target.size();
            }
            new_content.append(file_content, last_idx, file_content.size() - last_idx);
        } else {
            size_t offset = match_offsets[0];
            new_content.reserve(file_content.size() - target.size() + replacement.size());
            new_content.append(file_content, 0, offset);
            new_content.append(replacement);
            new_content.append(file_content, offset + target.size(), file_content.size() - (offset + target.size()));
        }

        StagedEdit staged;
        staged.id = std::format("stage-{}", ++stage_counter_);
        staged.file_path = resolved;
        staged.description = description;
        staged.original_content = file_content;
        staged.proposed_content = new_content;
        staged.diff = DiffEngine::compute_diff(file_content, new_content, resolved, resolved);
        staged.workspace_dir = workspace_dir;
        staged.timestamp = std::chrono::system_clock::now();

        staged_edits_.push_back(std::move(staged));
        return staged_edits_.back().id;
    }

    /**
     * Staging Buffer: Stage a proposed full file write in memory and compute diff.
     */
    std::string stage_write(
        const std::string& path,
        const std::string& new_content,
        const std::string& workspace_dir = "",
        const std::string& description = "AI Staged Write"
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string resolved = ProcessHelper::expandTilde(path);
        std::string before_content;
        if (std::filesystem::exists(resolved)) {
            std::ifstream file(resolved, std::ios::binary);
            if (file.is_open()) {
                std::ostringstream ss;
                ss << file.rdbuf();
                before_content = ss.str();
            }
        }

        StagedEdit staged;
        staged.id = std::format("stage-{}", ++stage_counter_);
        staged.file_path = resolved;
        staged.description = description;
        staged.original_content = before_content;
        staged.proposed_content = new_content;
        staged.diff = DiffEngine::compute_diff(before_content, new_content, resolved, resolved);
        staged.workspace_dir = workspace_dir;
        staged.timestamp = std::chrono::system_clock::now();

        staged_edits_.push_back(std::move(staged));
        return staged_edits_.back().id;
    }

    [[nodiscard]] std::vector<StagedEdit> get_staged_edits() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return staged_edits_;
    }

    [[nodiscard]] bool has_staged_edits() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !staged_edits_.empty();
    }

    bool apply_staged_edit(const std::string& edit_id, bool selective = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(staged_edits_.begin(), staged_edits_.end(),
            [&edit_id](const StagedEdit& s) { return s.id == edit_id; });
        if (it == staged_edits_.end()) return false;

        std::string final_content;
        if (selective) {
            final_content = DiffEngine::apply_selected_chunks(it->original_content, it->diff);
        } else {
            final_content = it->proposed_content;
        }

        std::ofstream out(it->file_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(final_content.data(), static_cast<std::streamsize>(final_content.size()));
        out.close();

        notify_editor_reload(it->file_path);
        CodeIndexer::instance().index_file(it->file_path, true);
        auto syntax_res = SyntaxChecker::instance().check_file(it->file_path, it->workspace_dir);

        // Record history
        record_history_internal(it->file_path, it->original_content, final_content,
            it->description, syntax_res.success);

        staged_edits_.erase(it);
        return true;
    }

    bool discard_staged_edit(const std::string& edit_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(staged_edits_.begin(), staged_edits_.end(),
            [&edit_id](const StagedEdit& s) { return s.id == edit_id; });
        if (it == staged_edits_.end()) return false;
        staged_edits_.erase(it);
        return true;
    }

    void clear_staged_edits() {
        std::lock_guard<std::mutex> lock(mutex_);
        staged_edits_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<EditHistoryEntry> undo_stack_;
    std::vector<EditHistoryEntry> redo_stack_;
    std::vector<StagedEdit> staged_edits_;
    int history_counter_{0};
    int stage_counter_{0};

    CodeEditorService() {
        register_services();
    }

    void register_services() {
        try {
            registrar::add("code_editor_undo", std::make_shared<std::function<bool()>>([this]() { return undo(); }));
            registrar::add("code_editor_redo", std::make_shared<std::function<bool()>>([this]() { return redo(); }));
            registrar::add("code_editor_can_undo", std::make_shared<std::function<bool()>>([this]() { return can_undo(); }));
            registrar::add("code_editor_can_redo", std::make_shared<std::function<bool()>>([this]() { return can_redo(); }));
        } catch (...) {}
    }

    void record_history_internal(
        const std::string& path,
        const std::string& before,
        const std::string& after,
        const std::string& desc,
        bool syntax_ok
    ) {
        auto diff = DiffEngine::compute_diff(before, after, path, path);

        EditHistoryEntry entry;
        entry.id = std::format("edit-{}", ++history_counter_);
        entry.file_path = path;
        entry.description = desc;
        entry.before_content = before;
        entry.after_content = after;
        entry.timestamp = std::chrono::system_clock::now();
        entry.additions = diff.total_additions;
        entry.deletions = diff.total_deletions;
        entry.syntax_passed = syntax_ok;

        undo_stack_.push_back(std::move(entry));
        if (undo_stack_.size() > 50) {
            undo_stack_.erase(undo_stack_.begin());
        }
        redo_stack_.clear();
    }

    static std::string to_lf(const std::string& str) {
        std::string out;
        out.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '\r' && i + 1 < str.size() && str[i + 1] == '\n') {
                continue;
            }
            out += str[i];
        }
        return out;
    }

    static std::string to_crlf(const std::string& str) {
        std::string out;
        out.reserve(str.size() + str.size() / 20);
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '\n' && (i == 0 || str[i - 1] != '\r')) {
                out += "\r\n";
            } else {
                out += str[i];
            }
        }
        return out;
    }

    void notify_editor_reload(const std::string& path) {
        try {
            auto get_active = registrar::get<std::function<std::string()>>("editor_get_active_file");
            if (get_active && *get_active) {
                std::string active_file = (*get_active)();
                if (!active_file.empty() && active_file == path) {
                    auto edit_fn = registrar::get<std::function<void(std::string const &)>>("edit");
                    if (edit_fn && *edit_fn) {
                        (*edit_fn)(path);
                    }
                }
            }
        } catch (...) {}
    }
};

} // namespace rouen::helpers
