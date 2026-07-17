#pragma once

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

// ---------------------------------------------------------------------------
// render_markdown_block
//
// Renders a full Markdown document with both block-level and inline support:
//
//   Block:  # H1  ## H2  ### H3  --- separator  - * bullets  1. numbered
//           > blockquote  ``` ... ``` code fence  (empty line = spacing)
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
    bool in_code_block = false;

    while (std::getline(stream, line)) {
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
}

} // namespace rouen::helpers
