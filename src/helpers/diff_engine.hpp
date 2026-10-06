#pragma once

#include <algorithm>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rouen::helpers {

enum class DiffLineType {
    Context,   // ' ' Unchanged
    Addition,  // '+' Added in new
    Deletion   // '-' Deleted from old
};

struct DiffLine {
    DiffLineType type{DiffLineType::Context};
    std::string text;
    int old_line_num{-1}; // 1-based line number in old file (-1 if addition)
    int new_line_num{-1}; // 1-based line number in new file (-1 if deletion)
};

enum class ChunkStatus {
    Pending,   // Awaiting developer decision
    Accepted,  // Approved to be applied
    Discarded  // Rejected / revert to original
};

struct DiffChunk {
    int id{0};
    int old_start_line{1};
    int old_line_count{0};
    int new_start_line{1};
    int new_line_count{0};
    std::vector<DiffLine> lines;
    ChunkStatus status{ChunkStatus::Pending};

    [[nodiscard]] int additions_count() const {
        int count = 0;
        for (const auto& l : lines) {
            if (l.type == DiffLineType::Addition) ++count;
        }
        return count;
    }

    [[nodiscard]] int deletions_count() const {
        int count = 0;
        for (const auto& l : lines) {
            if (l.type == DiffLineType::Deletion) ++count;
        }
        return count;
    }

    [[nodiscard]] std::string get_header() const {
        return std::format("@@ -{},{} +{},{} @@",
            old_start_line, old_line_count,
            new_start_line, new_line_count);
    }
};

struct DiffFile {
    std::string old_path;
    std::string new_path;
    std::string old_content;
    std::string new_content;
    std::vector<DiffChunk> chunks;
    bool has_changes{false};
    int total_additions{0};
    int total_deletions{0};

    void accept_chunk(int chunk_id) {
        for (auto& chunk : chunks) {
            if (chunk.id == chunk_id) {
                chunk.status = ChunkStatus::Accepted;
                break;
            }
        }
    }

    void discard_chunk(int chunk_id) {
        for (auto& chunk : chunks) {
            if (chunk.id == chunk_id) {
                chunk.status = ChunkStatus::Discarded;
                break;
            }
        }
    }

    void reset_chunk(int chunk_id) {
        for (auto& chunk : chunks) {
            if (chunk.id == chunk_id) {
                chunk.status = ChunkStatus::Pending;
                break;
            }
        }
    }

    void accept_all() {
        for (auto& chunk : chunks) {
            chunk.status = ChunkStatus::Accepted;
        }
    }

    void discard_all() {
        for (auto& chunk : chunks) {
            chunk.status = ChunkStatus::Discarded;
        }
    }

    void reset_all() {
        for (auto& chunk : chunks) {
            chunk.status = ChunkStatus::Pending;
        }
    }

    [[nodiscard]] int pending_count() const {
        int c = 0;
        for (const auto& ch : chunks) if (ch.status == ChunkStatus::Pending) ++c;
        return c;
    }

    [[nodiscard]] int accepted_count() const {
        int c = 0;
        for (const auto& ch : chunks) if (ch.status == ChunkStatus::Accepted) ++c;
        return c;
    }

    [[nodiscard]] int discarded_count() const {
        int c = 0;
        for (const auto& ch : chunks) if (ch.status == ChunkStatus::Discarded) ++c;
        return c;
    }
};

class DiffEngine {
public:
    /**
     * Splits a text string into a vector of lines, stripping trailing CR characters.
     */
    static std::vector<std::string> split_lines(const std::string& text) {
        std::vector<std::string> lines;
        if (text.empty()) return lines;

        std::string::size_type start = 0;
        while (start < text.size()) {
            auto end = text.find('\n', start);
            if (end == std::string::npos) {
                std::string line = text.substr(start);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(std::move(line));
                break;
            }
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            start = end + 1;
        }
        return lines;
    }

    /**
     * Computes a structured line-by-line diff between two strings using Myers algorithm
     * and groups contiguous changes into chunks with context lines.
     */
    static DiffFile compute_diff(
        const std::string& old_text,
        const std::string& new_text,
        const std::string& old_path = "a",
        const std::string& new_path = "b",
        int context_lines = 3
    ) {
        DiffFile result;
        result.old_path = old_path;
        result.new_path = new_path;
        result.old_content = old_text;
        result.new_content = new_text;

        if (old_text == new_text) {
            result.has_changes = false;
            return result;
        }

        std::vector<std::string> a = split_lines(old_text);
        std::vector<std::string> b = split_lines(new_text);

        std::vector<DiffLine> raw_edits = compute_myers_diff(a, b);

        for (const auto& l : raw_edits) {
            if (l.type == DiffLineType::Addition) {
                result.total_additions++;
            } else if (l.type == DiffLineType::Deletion) {
                result.total_deletions++;
            }
        }

        if (result.total_additions == 0 && result.total_deletions == 0) {
            result.has_changes = false;
            return result;
        }

        result.has_changes = true;
        result.chunks = group_into_chunks(raw_edits, context_lines);
        return result;
    }

    /**
     * Selectively applies chunks to the original content.
     * Only chunks with status == Accepted (or optionally Pending if treat_pending_as_accepted is true)
     * are applied. Discarded chunks are retained in their original state.
     * Chunks are applied in reverse order to eliminate coordinate drift.
     */
    static std::string apply_selected_chunks(
        const std::string& old_content,
        const DiffFile& diff,
        bool treat_pending_as_accepted = false
    ) {
        if (!diff.has_changes || diff.chunks.empty()) {
            return old_content;
        }

        bool has_crlf = (old_content.find("\r\n") != std::string::npos);
        bool ends_with_newline = (!old_content.empty() && old_content.back() == '\n');

        std::vector<std::string> lines = split_lines(old_content);

        // Sort chunks by old_start_line descending
        std::vector<DiffChunk> sorted_chunks = diff.chunks;
        std::sort(sorted_chunks.begin(), sorted_chunks.end(), [](const DiffChunk& c1, const DiffChunk& c2) {
            return c1.old_start_line > c2.old_start_line;
        });

        for (const auto& chunk : sorted_chunks) {
            bool should_apply = (chunk.status == ChunkStatus::Accepted) ||
                                (chunk.status == ChunkStatus::Pending && treat_pending_as_accepted);
            if (!should_apply) {
                continue; // Discarded or skipped chunk: keep original content
            }

            int start_idx = chunk.old_start_line - 1;
            int count = chunk.old_line_count;

            if (start_idx < 0) start_idx = 0;
            if (start_idx > static_cast<int>(lines.size())) {
                start_idx = static_cast<int>(lines.size());
            }

            int erase_count = count;
            if (start_idx + erase_count > static_cast<int>(lines.size())) {
                erase_count = static_cast<int>(lines.size()) - start_idx;
            }

            // Gather the replacement lines from the chunk
            std::vector<std::string> repl_lines;
            for (const auto& l : chunk.lines) {
                if (l.type == DiffLineType::Context || l.type == DiffLineType::Addition) {
                    repl_lines.push_back(l.text);
                }
            }

            // Erase old lines and insert new lines
            if (erase_count > 0) {
                lines.erase(lines.begin() + start_idx, lines.begin() + start_idx + erase_count);
            }
            lines.insert(lines.begin() + start_idx, repl_lines.begin(), repl_lines.end());
        }

        // Reassemble text
        std::string eol = has_crlf ? "\r\n" : "\n";
        std::ostringstream out;
        for (size_t i = 0; i < lines.size(); ++i) {
            out << lines[i];
            if (i + 1 < lines.size() || ends_with_newline) {
                out << eol;
            }
        }
        return out.str();
    }

    /**
     * Formats a DiffFile into standard unified diff format.
     */
    static std::string to_unified_diff(const DiffFile& diff) {
        if (!diff.has_changes) return "";

        std::ostringstream ss;
        ss << "--- " << (diff.old_path.empty() ? "a" : diff.old_path) << "\n";
        ss << "+++ " << (diff.new_path.empty() ? "b" : diff.new_path) << "\n";

        for (const auto& chunk : diff.chunks) {
            ss << chunk.get_header() << "\n";
            for (const auto& l : chunk.lines) {
                char prefix = ' ';
                if (l.type == DiffLineType::Addition) prefix = '+';
                else if (l.type == DiffLineType::Deletion) prefix = '-';
                ss << prefix << l.text << "\n";
            }
        }
        return ss.str();
    }

    /**
     * Parses standard unified diff text into a DiffFile structure.
     */
    static DiffFile parse_unified_diff(
        const std::string& diff_text,
        const std::string& fallback_old_path = "",
        const std::string& fallback_new_path = ""
    ) {
        DiffFile result;
        result.old_path = fallback_old_path;
        result.new_path = fallback_new_path;

        auto lines = split_lines(diff_text);
        DiffChunk current_chunk;
        bool in_chunk = false;
        int chunk_counter = 0;

        int cur_old_line = 1;
        int cur_new_line = 1;

        for (const auto& line : lines) {
            if (line.starts_with("--- ")) {
                result.old_path = line.substr(4);
                if (result.old_path.starts_with("a/")) result.old_path = result.old_path.substr(2);
                continue;
            }
            if (line.starts_with("+++ ")) {
                result.new_path = line.substr(4);
                if (result.new_path.starts_with("b/")) result.new_path = result.new_path.substr(2);
                continue;
            }
            if (line.starts_with("@@")) {
                if (in_chunk) {
                    result.chunks.push_back(std::move(current_chunk));
                    current_chunk = DiffChunk{};
                }
                in_chunk = true;
                current_chunk.id = ++chunk_counter;

                // Parse @@ -start,count +start,count @@
                parse_hunk_header(line, current_chunk.old_start_line, current_chunk.old_line_count,
                                   current_chunk.new_start_line, current_chunk.new_line_count);
                cur_old_line = current_chunk.old_start_line;
                cur_new_line = current_chunk.new_start_line;
                continue;
            }

            if (in_chunk && !line.empty()) {
                char c = line[0];
                std::string body = line.substr(1);
                DiffLine dl;
                dl.text = body;

                if (c == '+') {
                    dl.type = DiffLineType::Addition;
                    dl.new_line_num = cur_new_line++;
                    result.total_additions++;
                } else if (c == '-') {
                    dl.type = DiffLineType::Deletion;
                    dl.old_line_num = cur_old_line++;
                    result.total_deletions++;
                } else {
                    dl.type = DiffLineType::Context;
                    dl.old_line_num = cur_old_line++;
                    dl.new_line_num = cur_new_line++;
                }
                current_chunk.lines.push_back(std::move(dl));
            }
        }

        if (in_chunk && !current_chunk.lines.empty()) {
            result.chunks.push_back(std::move(current_chunk));
        }

        result.has_changes = !result.chunks.empty() && (result.total_additions > 0 || result.total_deletions > 0);
        return result;
    }

private:
    static void parse_hunk_header(const std::string& header, int& old_start, int& old_count, int& new_start, int& new_count) {
        old_start = 1; old_count = 1;
        new_start = 1; new_count = 1;

        auto first_at = header.find("@@");
        auto second_at = header.find("@@", first_at + 2);
        if (first_at == std::string::npos || second_at == std::string::npos) return;

        std::string range_part = header.substr(first_at + 2, second_at - (first_at + 2));
        std::istringstream iss(range_part);
        std::string minus_part, plus_part;
        iss >> minus_part >> plus_part;

        if (minus_part.starts_with('-')) {
            std::string sub = minus_part.substr(1);
            auto comma = sub.find(',');
            if (comma != std::string::npos) {
                old_start = std::stoi(sub.substr(0, comma));
                old_count = std::stoi(sub.substr(comma + 1));
            } else if (!sub.empty()) {
                old_start = std::stoi(sub);
                old_count = 1;
            }
        }

        if (plus_part.starts_with('+')) {
            std::string sub = plus_part.substr(1);
            auto comma = sub.find(',');
            if (comma != std::string::npos) {
                new_start = std::stoi(sub.substr(0, comma));
                new_count = std::stoi(sub.substr(comma + 1));
            } else if (!sub.empty()) {
                new_start = std::stoi(sub);
                new_count = 1;
            }
        }
    }

    static std::vector<DiffLine> compute_myers_diff(
        const std::vector<std::string>& a,
        const std::vector<std::string>& b
    ) {
        int n = static_cast<int>(a.size());
        int m = static_cast<int>(b.size());

        if (n == 0 && m == 0) return {};

        if (n == 0) {
            std::vector<DiffLine> edits;
            edits.reserve(static_cast<size_t>(m));
            for (int j = 0; j < m; ++j) {
                edits.push_back({DiffLineType::Addition, b[static_cast<size_t>(j)], -1, j + 1});
            }
            return edits;
        }

        if (m == 0) {
            std::vector<DiffLine> edits;
            edits.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                edits.push_back({DiffLineType::Deletion, a[static_cast<size_t>(i)], i + 1, -1});
            }
            return edits;
        }

        int max_d = n + m;
        int v_offset = max_d;
        std::vector<int> v(static_cast<size_t>(2 * max_d + 1), 0);
        std::vector<std::vector<int>> trace;
        int final_d = 0;

        for (int d = 0; d <= max_d; ++d) {
            for (int k = -d; k <= d; k += 2) {
                int x;
                size_t idx_down = static_cast<size_t>(k + 1 + v_offset);
                size_t idx_right = static_cast<size_t>(k - 1 + v_offset);

                if (k == -d || (k != d && v[idx_right] < v[idx_down])) {
                    x = v[idx_down]; // Down (insertion)
                } else {
                    x = v[idx_right] + 1; // Right (deletion)
                }
                int y = x - k;

                while (x < n && y < m && a[static_cast<size_t>(x)] == b[static_cast<size_t>(y)]) {
                    x++;
                    y++;
                }

                v[static_cast<size_t>(k + v_offset)] = x;

                if (x >= n && y >= m) {
                    trace.push_back(v);
                    final_d = d;
                    goto backtrack;
                }
            }
            trace.push_back(v);
        }

    backtrack:
        std::vector<DiffLine> edits;
        int x = n;
        int y = m;

        for (int step = final_d; step > 0; --step) {
            int k = x - y;
            const auto& prev_v = trace[static_cast<size_t>(step - 1)];

            int prev_k;
            size_t idx_prev_right = static_cast<size_t>(k - 1 + v_offset);
            size_t idx_prev_down = static_cast<size_t>(k + 1 + v_offset);

            if (k == -step || (k != step && prev_v[idx_prev_right] < prev_v[idx_prev_down])) {
                prev_k = k + 1;
            } else {
                prev_k = k - 1;
            }

            int prev_x = prev_v[static_cast<size_t>(prev_k + v_offset)];
            int prev_y = prev_x - prev_k;

            int step_x = (prev_k == k + 1) ? prev_x : prev_x + 1;
            int step_y = (prev_k == k + 1) ? prev_y + 1 : prev_y;

            // Matches (Context lines)
            while (x > step_x && y > step_y) {
                edits.push_back({DiffLineType::Context, a[static_cast<size_t>(x - 1)], x, y});
                x--;
                y--;
            }

            // Edit step
            if (prev_k == k + 1) {
                // Insertion
                edits.push_back({DiffLineType::Addition, b[static_cast<size_t>(step_y - 1)], -1, step_y});
            } else {
                // Deletion
                edits.push_back({DiffLineType::Deletion, a[static_cast<size_t>(step_x - 1)], step_x, -1});
            }

            x = prev_x;
            y = prev_y;
        }

        // Remaining snake down to (0, 0)
        while (x > 0 && y > 0) {
            edits.push_back({DiffLineType::Context, a[static_cast<size_t>(x - 1)], x, y});
            x--;
            y--;
        }

        std::reverse(edits.begin(), edits.end());
        return edits;
    }

    static std::vector<DiffChunk> group_into_chunks(
        const std::vector<DiffLine>& edits,
        int context_lines
    ) {
        std::vector<DiffChunk> chunks;
        if (edits.empty()) return chunks;

        // Find change indices
        std::vector<size_t> change_indices;
        for (size_t i = 0; i < edits.size(); ++i) {
            if (edits[i].type != DiffLineType::Context) {
                change_indices.push_back(i);
            }
        }

        if (change_indices.empty()) return chunks;

        // Group into ranges
        struct Range { size_t start; size_t end; };
        std::vector<Range> ranges;
        size_t ctx = static_cast<size_t>(std::max(0, context_lines));

        size_t r_start = (change_indices[0] >= ctx) ? (change_indices[0] - ctx) : 0;
        size_t r_end = std::min(edits.size() - 1, change_indices[0] + ctx);

        for (size_t i = 1; i < change_indices.size(); ++i) {
            size_t c_idx = change_indices[i];
            size_t next_start = (c_idx >= ctx) ? (c_idx - ctx) : 0;

            if (next_start <= r_end) {
                // Merge ranges
                r_end = std::min(edits.size() - 1, c_idx + ctx);
            } else {
                ranges.push_back({r_start, r_end});
                r_start = next_start;
                r_end = std::min(edits.size() - 1, c_idx + ctx);
            }
        }
        ranges.push_back({r_start, r_end});

        int chunk_id = 0;
        for (const auto& r : ranges) {
            DiffChunk chunk;
            chunk.id = ++chunk_id;
            chunk.status = ChunkStatus::Pending;

            int first_old = -1;
            int first_new = -1;
            int old_count = 0;
            int new_count = 0;

            for (size_t i = r.start; i <= r.end; ++i) {
                const auto& l = edits[i];
                chunk.lines.push_back(l);

                if (l.type == DiffLineType::Context) {
                    if (first_old == -1 && l.old_line_num > 0) first_old = l.old_line_num;
                    if (first_new == -1 && l.new_line_num > 0) first_new = l.new_line_num;
                    old_count++;
                    new_count++;
                } else if (l.type == DiffLineType::Deletion) {
                    if (first_old == -1 && l.old_line_num > 0) first_old = l.old_line_num;
                    old_count++;
                } else if (l.type == DiffLineType::Addition) {
                    if (first_new == -1 && l.new_line_num > 0) first_new = l.new_line_num;
                    new_count++;
                }
            }

            chunk.old_start_line = (first_old != -1) ? first_old : 1;
            chunk.old_line_count = old_count;
            chunk.new_start_line = (first_new != -1) ? first_new : 1;
            chunk.new_line_count = new_count;

            chunks.push_back(std::move(chunk));
        }

        return chunks;
    }
};

} // namespace rouen::helpers
