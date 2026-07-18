#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "imgui_include.hpp"
#include "adaptive_cards/markdown.hpp"

namespace rouen::helpers {

// Font pointers for Markdown rendering.
// All fields optional: null = use the current (default) ImGui font.
// Populate from rouen::fonts::get_font() at the call site so this header
// has no dependency on fonts.hpp (keeping the test binary link-clean).
struct markdown_render_config {
    ImFont* font_bold{nullptr};
    ImFont* font_italic{nullptr};
    ImFont* font_code{nullptr};
};

// ---------------------------------------------------------------------------
// render_inline_markdown
//
// Parses and renders a single line of inline Markdown using PushFont/PopFont
// for bold, italic, and code spans. Links show a tooltip and fire open_url_cb
// on click (if provided). base_color is applied to all non-code, non-link spans.
// ---------------------------------------------------------------------------
inline void render_inline_markdown(
    std::string_view text,
    const ImVec4& base_color,
    const markdown_render_config& config,
    const std::function<void(const std::string&)>& open_url_cb = {}
) {
    using namespace adaptive_cards;
    const auto spans = parse_inline_markdown(text);

    // Fast path: single plain-text span → standard wrapping text.
    if (spans.size() == 1 && spans[0].kind == span_kind::normal) {
        ImGui::PushStyleColor(ImGuiCol_Text, base_color);
        ImGui::TextWrapped("%s", spans[0].text.c_str());
        ImGui::PopStyleColor();
        return;
    }

    bool first = true;
    for (const auto& span : spans) {
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        first = false;

        switch (span.kind) {
        case span_kind::normal:
            ImGui::PushStyleColor(ImGuiCol_Text, base_color);
            ImGui::TextUnformatted(span.text.c_str());
            ImGui::PopStyleColor();
            break;
        case span_kind::bold:
            if (config.font_bold) ImGui::PushFont(config.font_bold);
            ImGui::PushStyleColor(ImGuiCol_Text, base_color);
            ImGui::TextUnformatted(span.text.c_str());
            ImGui::PopStyleColor();
            if (config.font_bold) ImGui::PopFont();
            break;
        case span_kind::italic:
            if (config.font_italic) ImGui::PushFont(config.font_italic);
            ImGui::PushStyleColor(ImGuiCol_Text, base_color);
            ImGui::TextUnformatted(span.text.c_str());
            ImGui::PopStyleColor();
            if (config.font_italic) ImGui::PopFont();
            break;
        case span_kind::code: {
            if (config.font_code) ImGui::PushFont(config.font_code);
            constexpr ImVec4 code_color{0.50f, 0.90f, 0.70f, 1.0f};
            ImGui::PushStyleColor(ImGuiCol_Text, code_color);
            ImGui::TextUnformatted(span.text.c_str());
            ImGui::PopStyleColor();
            if (config.font_code) ImGui::PopFont();
            break;
        }
        case span_kind::link: {
            constexpr ImVec4 link_color{0.35f, 0.65f, 1.0f, 1.0f};
            ImGui::PushStyleColor(ImGuiCol_Text, link_color);
            ImGui::TextUnformatted(span.text.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", span.url.c_str());
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (ImGui::IsItemClicked() && !span.url.empty() && open_url_cb) {
                open_url_cb(span.url);
            }
            break;
        }
        }
    }
}

// Helpers for parsing markdown tables
inline std::vector<std::string> split_table_row(std::string_view line) {
    std::vector<std::string> cells;
    std::string_view s = line;
    
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    if (!s.empty() && s.front() == '|') s.remove_prefix(1);
    
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (!s.empty() && s.back() == '|') s.remove_suffix(1);

    std::string current;
    for (char c : s) {
        if (c == '|') {
            std::string_view cell_view = current;
            while (!cell_view.empty() && std::isspace(static_cast<unsigned char>(cell_view.front()))) cell_view.remove_prefix(1);
            while (!cell_view.empty() && std::isspace(static_cast<unsigned char>(cell_view.back()))) cell_view.remove_suffix(1);
            cells.emplace_back(cell_view);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    std::string_view cell_view = current;
    while (!cell_view.empty() && std::isspace(static_cast<unsigned char>(cell_view.front()))) cell_view.remove_prefix(1);
    while (!cell_view.empty() && std::isspace(static_cast<unsigned char>(cell_view.back()))) cell_view.remove_suffix(1);
    cells.emplace_back(cell_view);
    
    return cells;
}

inline bool is_table_delimiter(std::string_view line) {
    std::string_view s = line;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    if (s.empty() || s.front() != '|') return false;
    s.remove_prefix(1);
    
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty() || s.back() != '|') return false;
    s.remove_suffix(1);

    if (s.empty()) return false;
    for (char c : s) {
        if (c != '-' && c != ':' && c != '|' && c != ' ') {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// render_markdown_block
//
// Renders a full Markdown document with both block-level and inline support:
//
//   Block:  # H1  ## H2  ### H3  --- separator  - * bullets  1. numbered
//           > blockquote  ``` ... ``` code fence  (empty line = spacing)
//           | ... | tables
//   Inline: **bold**  *italic*  `code`  [link](url)
//
// Headings use font_bold (if available) and visual hierarchy via colors and
// ImGui::SeparatorText / ImGui::Separator. Code fences use font_code.
// ---------------------------------------------------------------------------
inline void render_markdown_block(
    std::string_view markdown_text,
    const markdown_render_config& config,
    const std::function<void(const std::string&)>& open_url_cb = {}
) {
    using namespace adaptive_cards;

    const ImVec4 default_color  = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    constexpr ImVec4 h1_color   = {0.85f, 0.70f, 0.30f, 1.0f};
    constexpr ImVec4 dim_color  = {0.60f, 0.60f, 0.60f, 1.0f};
    constexpr ImVec4 code_color = {0.50f, 0.90f, 0.70f, 1.0f};

    std::istringstream stream{std::string(markdown_text)};
    std::string line;
    std::string leftover_line;
    bool has_leftover_line = false;
    bool in_code_block = false;
    
    std::string pending_header_line;
    bool has_pending_header = false;

    while (true) {
        if (has_leftover_line) {
            line = leftover_line;
            has_leftover_line = false;
        } else {
            if (!std::getline(stream, line)) {
                break;
            }
        }

        // ── Code fence toggle ──────────────────────────────────────────────
        if (line.starts_with("```")) {
            in_code_block = !in_code_block;
            continue;
        }

        if (in_code_block) {
            if (config.font_code) ImGui::PushFont(config.font_code);
            ImGui::PushStyleColor(ImGuiCol_Text, code_color);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
            if (config.font_code) ImGui::PopFont();
            continue;
        }

        // ── Table State Machine ───────────────────────────────────────────
        if (has_pending_header) {
            if (is_table_delimiter(line)) {
                auto header_cells = split_table_row(pending_header_line);
                int table_columns = static_cast<int>(header_cells.size());
                
                // Read ahead to collect all data rows belonging to this table
                std::vector<std::vector<std::string>> table_rows;
                table_rows.push_back(header_cells);
                
                std::string next_line;
                while (std::getline(stream, next_line)) {
                    std::string_view trimmed_next = next_line;
                    while (!trimmed_next.empty() && std::isspace(static_cast<unsigned char>(trimmed_next.front()))) {
                        trimmed_next.remove_prefix(1);
                    }
                    if (trimmed_next.starts_with('|')) {
                        if (is_table_delimiter(next_line)) {
                            continue; // skip duplicate delimiter lines
                        }
                        table_rows.push_back(split_table_row(next_line));
                    } else {
                        leftover_line = next_line;
                        has_leftover_line = true;
                        break;
                    }
                }
                
                // Calculate max text length per column to compute proportions
                std::vector<size_t> max_lens(static_cast<size_t>(table_columns), 0);
                for (const auto& row : table_rows) {
                    for (int col = 0; col < table_columns; ++col) {
                        if (static_cast<size_t>(col) < row.size()) {
                            max_lens[static_cast<size_t>(col)] = std::max(max_lens[static_cast<size_t>(col)], row[static_cast<size_t>(col)].length());
                        }
                    }
                }
                
                // Generate unique table ID
                static int table_id_counter = 0;
                std::string table_id = "markdown_table_" + std::to_string(++table_id_counter);
                
                if (ImGui::BeginTable(table_id.c_str(), table_columns, 
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                    
                    // Set up columns with dynamic stretch weights based on content length
                    for (int col = 0; col < table_columns; ++col) {
                        float max_len = static_cast<float>(max_lens[static_cast<size_t>(col)]);
                        float weight = std::clamp(std::sqrt(max_len), 1.0f, 10.0f);
                        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, weight);
                    }
                    
                    // Render headers
                    ImGui::TableNextRow();
                    for (int col = 0; col < table_columns; ++col) {
                        ImGui::TableSetColumnIndex(col);
                        if (config.font_bold) ImGui::PushFont(config.font_bold);
                        render_inline_markdown(table_rows[0][static_cast<size_t>(col)], default_color, config, open_url_cb);
                        if (config.font_bold) ImGui::PopFont();
                    }
                    
                    // Render rows
                    for (size_t r = 1; r < table_rows.size(); ++r) {
                        ImGui::TableNextRow();
                        for (int col = 0; col < table_columns; ++col) {
                            ImGui::TableSetColumnIndex(col);
                            if (static_cast<size_t>(col) < table_rows[r].size()) {
                                render_inline_markdown(table_rows[r][static_cast<size_t>(col)], default_color, config, open_url_cb);
                            }
                        }
                    }
                    
                    ImGui::EndTable();
                }
                
                has_pending_header = false;
                pending_header_line.clear();
                continue;
            } else {
                // Not a table! Flush the pending header first as a paragraph
                render_inline_markdown(pending_header_line, default_color, config, open_url_cb);
                has_pending_header = false;
                pending_header_line.clear();
            }
        }

        std::string_view trimmed_line = line;
        while (!trimmed_line.empty() && std::isspace(static_cast<unsigned char>(trimmed_line.front()))) {
            trimmed_line.remove_prefix(1);
        }
        
        if (trimmed_line.starts_with('|')) {
            pending_header_line = line;
            has_pending_header = true;
            continue;
        }

        // ── Horizontal rule ───────────────────────────────────────────────
        if (line == "---" || line == "***" || line == "___") {
            ImGui::Separator();
            continue;
        }

        // ── Headings ──────────────────────────────────────────────────────
        if (line.starts_with("# ")) {
            if (config.font_bold) ImGui::PushFont(config.font_bold);
            render_inline_markdown(std::string_view{line}.substr(2), h1_color, config, open_url_cb);
            if (config.font_bold) ImGui::PopFont();
            ImGui::Separator();
            continue;
        }
        if (line.starts_with("## ")) {
            // SeparatorText renders best with plain text; strip inline markers.
            ImGui::SeparatorText(strip_markdown(line.substr(3)).c_str());
            continue;
        }
        if (line.starts_with("### ")) {
            if (config.font_bold) ImGui::PushFont(config.font_bold);
            render_inline_markdown(std::string_view{line}.substr(4), default_color, config, open_url_cb);
            if (config.font_bold) ImGui::PopFont();
            continue;
        }

        // ── Blockquote ────────────────────────────────────────────────────
        if (line.starts_with("> ")) {
            ImGui::Indent();
            render_inline_markdown(std::string_view{line}.substr(2), dim_color, config, open_url_cb);
            ImGui::Unindent();
            continue;
        }

        // ── Unordered bullet list ─────────────────────────────────────────
        if (line.starts_with("- ") || line.starts_with("* ")) {
            ImGui::Bullet();
            ImGui::SameLine();
            render_inline_markdown(std::string_view{line}.substr(2), default_color, config, open_url_cb);
            continue;
        }

        // ── Ordered (numbered) list: "1. text", "12. text", etc. ─────────
        {
            const std::size_t dot = line.find(". ");
            if (dot != std::string::npos && dot > 0 && dot < 5) {
                bool all_digits = true;
                for (std::size_t i = 0; i < dot; ++i) {
                    if (line[i] < '0' || line[i] > '9') { all_digits = false; break; }
                }
                if (all_digits) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    render_inline_markdown(std::string_view{line}.substr(dot + 2), default_color, config, open_url_cb);
                    continue;
                }
            }
        }

        // ── Empty line → vertical spacing ────────────────────────────────
        if (line.empty()) {
            ImGui::Spacing();
            continue;
        }

        // ── Regular paragraph ─────────────────────────────────────────────
        render_inline_markdown(line, default_color, config, open_url_cb);
    }
    
    // Clean up any remaining open blocks at EOF
    if (has_pending_header) {
        render_inline_markdown(pending_header_line, default_color, config, open_url_cb);
    }
}

} // namespace rouen::helpers
