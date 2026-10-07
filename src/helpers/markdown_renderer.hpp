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
    std::function<void(const std::string& alt, const std::string& url)> render_image_cb{};
};

// ---------------------------------------------------------------------------
// Word token and flow layout engine for inline Markdown
// ---------------------------------------------------------------------------
struct word_token {
    adaptive_cards::span_kind kind{adaptive_cards::span_kind::normal};
    std::string_view text;
    std::string_view url;
    bool has_trailing_space{false};
    bool is_hard_break{false};
};

[[nodiscard]] inline std::vector<word_token> tokenize_spans_into_words(
    const std::vector<adaptive_cards::text_span>& spans
) {
    std::vector<word_token> tokens;
    
    for (const auto& span : spans) {
        if (span.kind == adaptive_cards::span_kind::image) {
            tokens.push_back({
                .kind = span.kind,
                .text = span.text,
                .url = span.url,
                .has_trailing_space = false,
                .is_hard_break = false
            });
            continue;
        }

        std::string_view sv = span.text;

        // If span starts with whitespace and tokens exist, record trailing space on previous token
        if (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
            if (!tokens.empty()) {
                tokens.back().has_trailing_space = true;
            }
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
                if (sv.front() == '\n' && !tokens.empty()) {
                    tokens.back().is_hard_break = true;
                }
                sv.remove_prefix(1);
            }
        }

        while (!sv.empty()) {
            std::size_t word_end = 0;
            while (word_end < sv.size() && !std::isspace(static_cast<unsigned char>(sv[word_end]))) {
                if (sv[word_end] == '\\' && word_end + 1 < sv.size() && sv[word_end + 1] == '\n') {
                    break;
                }
                ++word_end;
            }

            if (word_end > 0) {
                std::string_view word = sv.substr(0, word_end);
                sv.remove_prefix(word_end);

                bool has_space = false;
                bool is_hard = false;

                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
                    if (sv.front() == '\n') {
                        is_hard = true;
                    }
                    has_space = true;
                    sv.remove_prefix(1);
                }

                if (!sv.empty() && sv.front() == '\\' && sv.size() > 1 && sv[1] == '\n') {
                    is_hard = true;
                    has_space = true;
                    sv.remove_prefix(2);
                }

                tokens.push_back({
                    .kind = span.kind,
                    .text = word,
                    .url = span.url,
                    .has_trailing_space = has_space,
                    .is_hard_break = is_hard
                });
            } else if (!sv.empty() && sv.front() == '\\' && sv.size() > 1 && sv[1] == '\n') {
                if (!tokens.empty()) {
                    tokens.back().is_hard_break = true;
                }
                sv.remove_prefix(2);
            } else {
                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
                    if (sv.front() == '\n' && !tokens.empty()) {
                        tokens.back().is_hard_break = true;
                    }
                    sv.remove_prefix(1);
                }
            }
        }
    }
    return tokens;
}

inline void render_flowing_markdown(
    const std::vector<adaptive_cards::text_span>& spans,
    const ImVec4& base_color,
    const markdown_render_config& config,
    float indent_x = -1.0f,
    const std::function<void(const std::string&)>& open_url_cb = {}
) {
    if (spans.empty()) return;

    if (spans.size() == 1 && spans[0].kind == adaptive_cards::span_kind::normal && indent_x < 0.0f) {
        ImGui::PushStyleColor(ImGuiCol_Text, base_color);
        ImGui::TextWrapped("%s", spans[0].text.c_str());
        ImGui::PopStyleColor();
        return;
    }

    const float start_pos_x = (indent_x >= 0.0f) ? indent_x : ImGui::GetCursorPosX();
    const float avail_width = ImGui::GetContentRegionAvail().x;
    const float wrap_pos_x  = start_pos_x + avail_width;

    const auto tokens = tokenize_spans_into_words(spans);
    if (tokens.empty()) return;

    bool is_line_start = (indent_x < 0.0f);

    for (const auto& tok : tokens) {
        if (tok.kind == adaptive_cards::span_kind::image) {
            if (config.render_image_cb) {
                config.render_image_cb(std::string(tok.text), std::string(tok.url));
            } else {
                constexpr ImVec4 img_badge_color{0.45f, 0.75f, 0.95f, 1.0f};
                ImGui::PushStyleColor(ImGuiCol_Text, img_badge_color);
                ImGui::Text("[%.*s]", static_cast<int>(tok.text.size()), tok.text.data());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Image: %.*s", static_cast<int>(tok.url.size()), tok.url.data());
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
                if (ImGui::IsItemClicked() && !tok.url.empty() && open_url_cb) {
                    open_url_cb(std::string(tok.url));
                }
            }
            is_line_start = false;
            continue;
        }

        ImFont* font = nullptr;
        ImVec4 color = base_color;

        switch (tok.kind) {
        case adaptive_cards::span_kind::bold:
            font = config.font_bold;
            break;
        case adaptive_cards::span_kind::italic:
            font = config.font_italic;
            break;
        case adaptive_cards::span_kind::code:
            font = config.font_code;
            color = ImVec4{0.50f, 0.90f, 0.70f, 1.0f};
            break;
        case adaptive_cards::span_kind::link:
            color = ImVec4{0.35f, 0.65f, 1.0f, 1.0f};
            break;
        case adaptive_cards::span_kind::normal:
        case adaptive_cards::span_kind::image:
        default:
            break;
        }

        if (!font) font = ImGui::GetFont();

        const float font_size = font->FontSize;
        const float word_w = font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, tok.text.data(), tok.text.data() + tok.text.size()).x;
        const float space_w = font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, " ", nullptr).x;

        const float current_x = ImGui::GetCursorPosX();

        // Check if word causes an overflow past the wrap boundary
        if (!is_line_start && (current_x + word_w > wrap_pos_x)) {
            ImGui::NewLine();
            ImGui::SetCursorPosX(start_pos_x);
            is_line_start = true;
        }

        if (!is_line_start) {
            ImGui::SameLine(0.0f, space_w);
        }

        if (font != ImGui::GetFont()) ImGui::PushFont(font);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(tok.text.data(), tok.text.data() + tok.text.size());
        ImGui::PopStyleColor();
        if (font != ImGui::GetFont()) ImGui::PopFont();

        if (tok.kind == adaptive_cards::span_kind::link) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%.*s", static_cast<int>(tok.url.size()), tok.url.data());
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (ImGui::IsItemClicked() && !tok.url.empty() && open_url_cb) {
                open_url_cb(std::string(tok.url));
            }
        }

        is_line_start = false;

        if (tok.is_hard_break) {
            ImGui::NewLine();
            ImGui::SetCursorPosX(start_pos_x);
            is_line_start = true;
        }
    }
}

// ---------------------------------------------------------------------------
// render_inline_markdown
//
// Parses and renders a single line of inline Markdown with word-level flow
// wrapping across multiple styled spans (bold, italic, code, links, images).
// ---------------------------------------------------------------------------
inline void render_inline_markdown(
    std::string_view text,
    const ImVec4& base_color,
    const markdown_render_config& config,
    const std::function<void(const std::string&)>& open_url_cb = {}
) {
    using namespace adaptive_cards;
    const auto spans = parse_inline_markdown(text);
    render_flowing_markdown(spans, base_color, config, -1.0f, open_url_cb);
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
            ImGui::TextWrapped("%s", line.c_str());
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
            const float indent_x = ImGui::GetCursorPosX();
            const auto spans = adaptive_cards::parse_inline_markdown(std::string_view{line}.substr(2));
            render_flowing_markdown(spans, dim_color, config, indent_x, open_url_cb);
            ImGui::Unindent();
            continue;
        }

        // ── Unordered bullet list ─────────────────────────────────────────
        if (line.starts_with("- ") || line.starts_with("* ")) {
            ImGui::Bullet();
            ImGui::SameLine();
            const float text_col_x = ImGui::GetCursorPosX();
            const auto spans = adaptive_cards::parse_inline_markdown(std::string_view{line}.substr(2));
            render_flowing_markdown(spans, default_color, config, text_col_x, open_url_cb);
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
                    const float text_col_x = ImGui::GetCursorPosX();
                    const auto spans = adaptive_cards::parse_inline_markdown(std::string_view{line}.substr(dot + 2));
                    render_flowing_markdown(spans, default_color, config, text_col_x, open_url_cb);
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
