#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
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

        bool const font_pushed = (font && font != ImGui::GetFont());
        if (font_pushed) ImGui::PushFont(font);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(tok.text.data(), tok.text.data() + tok.text.size());
        ImGui::PopStyleColor();
        if (font_pushed) ImGui::PopFont();

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
// In-Memory Primitive Representation & AST Caching for Markdown
// ---------------------------------------------------------------------------

struct heading_primitive {
    int level{1};
    std::vector<adaptive_cards::text_span> spans;
    std::string plain_text;
};

struct paragraph_primitive {
    std::vector<adaptive_cards::text_span> spans;
};

struct list_item_primitive {
    bool is_ordered{false};
    std::string marker;
    int indent_level{0};
    std::vector<adaptive_cards::text_span> spans;
};

struct blockquote_primitive {
    int indent_level{1};
    std::vector<adaptive_cards::text_span> spans;
};

struct code_block_primitive {
    std::string language;
    std::vector<std::string> lines;
};

struct table_primitive {
    int table_columns{0};
    std::string table_id;
    std::vector<float> column_weights;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

struct separator_primitive {};

struct spacing_primitive {};

using markdown_primitive = std::variant<
    heading_primitive,
    paragraph_primitive,
    list_item_primitive,
    blockquote_primitive,
    code_block_primitive,
    table_primitive,
    separator_primitive,
    spacing_primitive
>;

struct markdown_document {
    std::vector<markdown_primitive> primitives;
    int line_count{0};
    int word_count{0};
    size_t byte_size{0};
    uint64_t content_hash{0};

    [[nodiscard]] bool empty() const noexcept {
        return primitives.empty();
    }

    void clear() {
        primitives.clear();
        line_count = 0;
        word_count = 0;
        byte_size = 0;
        content_hash = 0;
    }
};

[[nodiscard]] inline markdown_document parse_markdown_document(std::string_view markdown_text) {
    using namespace adaptive_cards;
    markdown_document doc;
    doc.byte_size = markdown_text.size();

    if (markdown_text.empty()) {
        return doc;
    }

    std::istringstream stream{std::string(markdown_text)};
    std::string line;
    std::string leftover_line;
    bool has_leftover_line = false;
    bool in_code_block = false;
    std::string code_language;
    std::vector<std::string> code_lines;

    std::string pending_header_line;
    bool has_pending_header = false;

    // Count words in full text
    bool in_word = false;
    for (char c : markdown_text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            doc.word_count++;
        }
    }

    while (true) {
        if (has_leftover_line) {
            line = leftover_line;
            has_leftover_line = false;
        } else {
            if (!std::getline(stream, line)) {
                break;
            }
        }
        doc.line_count++;

        // ── Code fence toggle ──────────────────────────────────────────────
        if (line.starts_with("```")) {
            if (in_code_block) {
                doc.primitives.push_back(code_block_primitive{
                    .language = std::move(code_language),
                    .lines = std::move(code_lines)
                });
                code_language.clear();
                code_lines.clear();
                in_code_block = false;
            } else {
                in_code_block = true;
                code_language = line.size() > 3 ? line.substr(3) : "";
                code_lines.clear();
            }
            continue;
        }

        if (in_code_block) {
            code_lines.push_back(line);
            continue;
        }

        // ── Table State Machine ───────────────────────────────────────────
        if (has_pending_header) {
            if (is_table_delimiter(line)) {
                auto header_cells = split_table_row(pending_header_line);
                int table_columns = static_cast<int>(header_cells.size());

                std::vector<std::vector<std::string>> table_rows;
                std::string next_line;
                while (std::getline(stream, next_line)) {
                    doc.line_count++;
                    std::string_view trimmed_next = next_line;
                    while (!trimmed_next.empty() && std::isspace(static_cast<unsigned char>(trimmed_next.front()))) {
                        trimmed_next.remove_prefix(1);
                    }
                    if (trimmed_next.starts_with('|')) {
                        if (is_table_delimiter(next_line)) {
                            continue;
                        }
                        table_rows.push_back(split_table_row(next_line));
                    } else {
                        leftover_line = next_line;
                        has_leftover_line = true;
                        break;
                    }
                }

                std::vector<size_t> max_lens(static_cast<size_t>(table_columns), 0);
                for (size_t col = 0; col < static_cast<size_t>(table_columns); ++col) {
                    if (col < header_cells.size()) {
                        max_lens[col] = std::max(max_lens[col], header_cells[col].length());
                    }
                }
                for (const auto& row : table_rows) {
                    for (int col = 0; col < table_columns; ++col) {
                        if (static_cast<size_t>(col) < row.size()) {
                            max_lens[static_cast<size_t>(col)] = std::max(max_lens[static_cast<size_t>(col)], row[static_cast<size_t>(col)].length());
                        }
                    }
                }

                std::vector<float> weights(static_cast<size_t>(table_columns));
                for (int col = 0; col < table_columns; ++col) {
                    float max_len = static_cast<float>(max_lens[static_cast<size_t>(col)]);
                    weights[static_cast<size_t>(col)] = std::clamp(std::sqrt(max_len), 1.0f, 10.0f);
                }

                static int table_id_counter = 0;
                std::string table_id = "markdown_table_" + std::to_string(++table_id_counter);

                doc.primitives.push_back(table_primitive{
                    .table_columns = table_columns,
                    .table_id = std::move(table_id),
                    .column_weights = std::move(weights),
                    .headers = std::move(header_cells),
                    .rows = std::move(table_rows)
                });

                has_pending_header = false;
                pending_header_line.clear();
                continue;
            } else {
                doc.primitives.push_back(paragraph_primitive{
                    .spans = parse_inline_markdown(pending_header_line)
                });
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
            doc.primitives.push_back(separator_primitive{});
            continue;
        }

        // ── Headings ──────────────────────────────────────────────────────
        if (line.starts_with("# ")) {
            doc.primitives.push_back(heading_primitive{
                .level = 1,
                .spans = parse_inline_markdown(std::string_view{line}.substr(2)),
                .plain_text = strip_markdown(line.substr(2))
            });
            continue;
        }
        if (line.starts_with("## ")) {
            doc.primitives.push_back(heading_primitive{
                .level = 2,
                .spans = {},
                .plain_text = strip_markdown(line.substr(3))
            });
            continue;
        }
        if (line.starts_with("### ")) {
            doc.primitives.push_back(heading_primitive{
                .level = 3,
                .spans = parse_inline_markdown(std::string_view{line}.substr(4)),
                .plain_text = strip_markdown(line.substr(4))
            });
            continue;
        }
        if (line.starts_with("#### ")) {
            doc.primitives.push_back(heading_primitive{
                .level = 4,
                .spans = parse_inline_markdown(std::string_view{line}.substr(5)),
                .plain_text = strip_markdown(line.substr(5))
            });
            continue;
        }
        if (line.starts_with("##### ")) {
            doc.primitives.push_back(heading_primitive{
                .level = 5,
                .spans = parse_inline_markdown(std::string_view{line}.substr(6)),
                .plain_text = strip_markdown(line.substr(6))
            });
            continue;
        }
        if (line.starts_with("###### ")) {
            doc.primitives.push_back(heading_primitive{
                .level = 6,
                .spans = parse_inline_markdown(std::string_view{line}.substr(7)),
                .plain_text = strip_markdown(line.substr(7))
            });
            continue;
        }

        // ── Blockquote ────────────────────────────────────────────────────
        if (line.starts_with("> ")) {
            doc.primitives.push_back(blockquote_primitive{
                .indent_level = 1,
                .spans = parse_inline_markdown(std::string_view{line}.substr(2))
            });
            continue;
        }

        // ── Unordered bullet list ─────────────────────────────────────────
        if (line.starts_with("- ") || line.starts_with("* ")) {
            doc.primitives.push_back(list_item_primitive{
                .is_ordered = false,
                .marker = "-",
                .indent_level = 0,
                .spans = parse_inline_markdown(std::string_view{line}.substr(2))
            });
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
                    doc.primitives.push_back(list_item_primitive{
                        .is_ordered = true,
                        .marker = line.substr(0, dot + 1),
                        .indent_level = 0,
                        .spans = parse_inline_markdown(std::string_view{line}.substr(dot + 2))
                    });
                    continue;
                }
            }
        }

        // ── Empty line → vertical spacing ────────────────────────────────
        if (line.empty()) {
            doc.primitives.push_back(spacing_primitive{});
            continue;
        }

        // ── Regular paragraph ─────────────────────────────────────────────
        doc.primitives.push_back(paragraph_primitive{
            .spans = parse_inline_markdown(line)
        });
    }

    if (in_code_block) {
        doc.primitives.push_back(code_block_primitive{
            .language = std::move(code_language),
            .lines = std::move(code_lines)
        });
    }

    if (has_pending_header) {
        doc.primitives.push_back(paragraph_primitive{
            .spans = parse_inline_markdown(pending_header_line)
        });
    }

    return doc;
}

inline void render_markdown_document(
    const markdown_document& doc,
    const markdown_render_config& config,
    const std::function<void(const std::string&)>& open_url_cb = {}
) {
    const ImVec4 default_color  = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    constexpr ImVec4 h1_color   = {0.85f, 0.70f, 0.30f, 1.0f};
    constexpr ImVec4 dim_color  = {0.60f, 0.60f, 0.60f, 1.0f};
    constexpr ImVec4 code_color = {0.50f, 0.90f, 0.70f, 1.0f};

    for (const auto& prim : doc.primitives) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, heading_primitive>) {
                if (item.level == 1) {
                    if (config.font_bold) ImGui::PushFont(config.font_bold);
                    render_flowing_markdown(item.spans, h1_color, config, -1.0f, open_url_cb);
                    if (config.font_bold) ImGui::PopFont();
                    ImGui::Separator();
                } else if (item.level == 2) {
                    ImGui::SeparatorText(item.plain_text.c_str());
                } else if (item.level >= 3) {
                    if (config.font_bold) ImGui::PushFont(config.font_bold);
                    render_flowing_markdown(item.spans, default_color, config, -1.0f, open_url_cb);
                    if (config.font_bold) ImGui::PopFont();
                }
            } else if constexpr (std::is_same_v<T, paragraph_primitive>) {
                render_flowing_markdown(item.spans, default_color, config, -1.0f, open_url_cb);
            } else if constexpr (std::is_same_v<T, list_item_primitive>) {
                ImGui::Bullet();
                ImGui::SameLine();
                const float text_col_x = ImGui::GetCursorPosX();
                render_flowing_markdown(item.spans, default_color, config, text_col_x, open_url_cb);
            } else if constexpr (std::is_same_v<T, blockquote_primitive>) {
                ImGui::Indent();
                const float indent_x = ImGui::GetCursorPosX();
                render_flowing_markdown(item.spans, dim_color, config, indent_x, open_url_cb);
                ImGui::Unindent();
            } else if constexpr (std::is_same_v<T, code_block_primitive>) {
                if (config.font_code) ImGui::PushFont(config.font_code);
                ImGui::PushStyleColor(ImGuiCol_Text, code_color);
                for (const auto& code_line : item.lines) {
                    ImGui::TextWrapped("%s", code_line.c_str());
                }
                ImGui::PopStyleColor();
                if (config.font_code) ImGui::PopFont();
            } else if constexpr (std::is_same_v<T, table_primitive>) {
                if (ImGui::BeginTable(item.table_id.c_str(), item.table_columns, 
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                    for (int col = 0; col < item.table_columns; ++col) {
                        float weight = col < static_cast<int>(item.column_weights.size()) ? item.column_weights[static_cast<size_t>(col)] : 1.0f;
                        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, weight);
                    }
                    ImGui::TableNextRow();
                    for (int col = 0; col < item.table_columns; ++col) {
                        ImGui::TableSetColumnIndex(col);
                        if (config.font_bold) ImGui::PushFont(config.font_bold);
                        if (static_cast<size_t>(col) < item.headers.size()) {
                            render_inline_markdown(item.headers[static_cast<size_t>(col)], default_color, config, open_url_cb);
                        }
                        if (config.font_bold) ImGui::PopFont();
                    }
                    for (const auto& row : item.rows) {
                        ImGui::TableNextRow();
                        for (int col = 0; col < item.table_columns; ++col) {
                            ImGui::TableSetColumnIndex(col);
                            if (static_cast<size_t>(col) < row.size()) {
                                render_inline_markdown(row[static_cast<size_t>(col)], default_color, config, open_url_cb);
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            } else if constexpr (std::is_same_v<T, separator_primitive>) {
                ImGui::Separator();
            } else if constexpr (std::is_same_v<T, spacing_primitive>) {
                ImGui::Spacing();
            }
        }, prim);
    }
}

// ---------------------------------------------------------------------------
// render_markdown_block
//
// Backward-compatible wrapper that parses and renders a Markdown document.
// ---------------------------------------------------------------------------
inline void render_markdown_block(
    std::string_view markdown_text,
    const markdown_render_config& config,
    const std::function<void(const std::string&)>& open_url_cb = {}
) {
    const markdown_document doc = parse_markdown_document(markdown_text);
    render_markdown_document(doc, config, open_url_cb);
}

} // namespace rouen::helpers
