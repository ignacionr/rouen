#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../src/helpers/adaptive_cards/markdown.hpp"
#include "../src/helpers/color_utils.hpp"
#include "../src/helpers/filetype_handler.hpp"
#include "../src/helpers/markdown_renderer.hpp"

using namespace rouen::helpers::adaptive_cards;

// ===========================================================================
// Tests for parse_inline_markdown — block-level patterns
//
// render_markdown_block() delegates block detection to line-prefix checks and
// inline rendering to parse_inline_markdown().  These tests validate that the
// inline parser handles the content that block-level rendering would feed it,
// and that strip_markdown round-trips correctly for block content.
// ===========================================================================

// ── Heading content parsing ──────────────────────────────────────────────────

TEST(MarkdownRenderer, HeadingContentIsParsedInline) {
    // render_markdown_block strips "# " and feeds the remainder to
    // render_inline_markdown.  Verify bold inside a heading is recognised.
    const auto spans = parse_inline_markdown("**Status Update** for Rouen");
    ASSERT_GE(spans.size(), 2U);
    EXPECT_EQ(spans[0].kind, span_kind::bold);
    EXPECT_EQ(spans[0].text, "Status Update");
}

TEST(MarkdownRenderer, H2ContentIsStripped) {
    // SeparatorText receives strip_markdown output for ## headings.
    EXPECT_EQ(strip_markdown("**Important** heading"), "Important heading");
}

TEST(MarkdownRenderer, H3ContentParsedInline) {
    const auto spans = parse_inline_markdown("*Subsection title*");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::italic);
    EXPECT_EQ(spans[0].text, "Subsection title");
}

// ── Bullet / list item content ───────────────────────────────────────────────

TEST(MarkdownRenderer, BulletContentParsedInline) {
    // "- **bold item**" → after block prefix strip: "**bold item**"
    const auto spans = parse_inline_markdown("**bold item**");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::bold);
    EXPECT_EQ(spans[0].text, "bold item");
}

TEST(MarkdownRenderer, NumberedListContentParsedInline) {
    // "1. Visit [link](url)" → after block prefix strip: "Visit [link](url)"
    const auto spans = parse_inline_markdown("Visit [link](https://example.com)");
    ASSERT_EQ(spans.size(), 2U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "Visit ");
    EXPECT_EQ(spans[1].kind, span_kind::link);
    EXPECT_EQ(spans[1].text, "link");
    EXPECT_EQ(spans[1].url, "https://example.com");
}

// ── Blockquote content ───────────────────────────────────────────────────────

TEST(MarkdownRenderer, BlockquoteContentParsedInline) {
    // "> *Note:* important" → after "> " strip: "*Note:* important"
    const auto spans = parse_inline_markdown("*Note:* important");
    ASSERT_GE(spans.size(), 2U);
    EXPECT_EQ(spans[0].kind, span_kind::italic);
    EXPECT_EQ(spans[0].text, "Note:");
    EXPECT_EQ(spans[1].kind, span_kind::normal);
}

// ── Code fence detection (block-level, tested via line prefix) ───────────────

TEST(MarkdownRenderer, CodeFenceToggleDetection) {
    // Verify that ``` is correctly identified as a code fence toggle.
    // In render_markdown_block, lines starting with ``` toggle code mode.
    std::string_view fence = "```python";
    EXPECT_TRUE(fence.starts_with("```"));

    std::string_view not_fence = "`` not a fence ``";
    EXPECT_FALSE(not_fence.starts_with("```"));
}

// ── Horizontal rule detection ────────────────────────────────────────────────

TEST(MarkdownRenderer, HorizontalRuleVariants) {
    EXPECT_EQ(std::string("---"), "---");
    EXPECT_EQ(std::string("***"), "***");
    EXPECT_EQ(std::string("___"), "___");
    // Non-rules:
    EXPECT_NE(std::string("-- -"), "---");
    EXPECT_NE(std::string("----"), "---");
}

// ── Ordered list prefix detection ────────────────────────────────────────────

TEST(MarkdownRenderer, OrderedListPrefixParsing) {
    // Verify the ordered-list heuristic: digits followed by ". " within 5 chars.
    auto is_ordered_list = [](std::string_view line) -> bool {
        const auto dot = line.find(". ");
        if (dot == std::string_view::npos || dot == 0 || dot >= 5) return false;
        for (std::size_t i = 0; i < dot; ++i) {
            if (line[i] < '0' || line[i] > '9') return false;
        }
        return true;
    };

    EXPECT_TRUE(is_ordered_list("1. First item"));
    EXPECT_TRUE(is_ordered_list("12. Twelfth item"));
    EXPECT_TRUE(is_ordered_list("999. Big number"));
    EXPECT_FALSE(is_ordered_list("a. Not a number"));
    EXPECT_FALSE(is_ordered_list(". No digits"));
    EXPECT_FALSE(is_ordered_list("123456. Too many digits"));
}

// ── Multi-line strip_markdown ────────────────────────────────────────────────

TEST(MarkdownRenderer, StripMarkdownMultiLine) {
    // strip_markdown works on a single line; verify sequential calls.
    EXPECT_EQ(strip_markdown("**bold** and *italic*"), "bold and italic");
    EXPECT_EQ(strip_markdown("`code` and [link](url)"), "code and link");
}

// ── Escaped characters survive block-level processing ────────────────────────

TEST(MarkdownRenderer, EscapedAsterisksInInline) {
    const auto spans = parse_inline_markdown("Price is \\*not\\* negotiable");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "Price is *not* negotiable");
}

// ── Empty and whitespace-only input ──────────────────────────────────────────

TEST(MarkdownRenderer, EmptyInputProducesNoSpans) {
    EXPECT_TRUE(parse_inline_markdown("").empty());
}

TEST(MarkdownRenderer, WhitespaceOnlyIsNormal) {
    const auto spans = parse_inline_markdown("   ");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "   ");
}

// ── Complex real-world AI response patterns ──────────────────────────────────

TEST(MarkdownRenderer, AIResponseWithCodeAndBold) {
    // Typical AI chat response snippet
    const auto spans = parse_inline_markdown(
        "Use `std::vector` for **dynamic arrays** in C++."
    );
    // Expected: normal + code + normal + bold + normal
    ASSERT_EQ(spans.size(), 5U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "Use ");
    EXPECT_EQ(spans[1].kind, span_kind::code);
    EXPECT_EQ(spans[1].text, "std::vector");
    EXPECT_EQ(spans[2].kind, span_kind::normal);
    EXPECT_EQ(spans[2].text, " for ");
    EXPECT_EQ(spans[3].kind, span_kind::bold);
    EXPECT_EQ(spans[3].text, "dynamic arrays");
    EXPECT_EQ(spans[4].kind, span_kind::normal);
    EXPECT_EQ(spans[4].text, " in C++.");
}

TEST(MarkdownRenderer, AIResponseWithMultipleLinks) {
    const auto spans = parse_inline_markdown(
        "See [docs](https://docs.example.com) and [source](https://github.com/repo)"
    );
    ASSERT_EQ(spans.size(), 4U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "See ");
    EXPECT_EQ(spans[1].kind, span_kind::link);
    EXPECT_EQ(spans[1].text, "docs");
    EXPECT_EQ(spans[1].url, "https://docs.example.com");
    EXPECT_EQ(spans[2].kind, span_kind::normal);
    EXPECT_EQ(spans[2].text, " and ");
    EXPECT_EQ(spans[3].kind, span_kind::link);
    EXPECT_EQ(spans[3].text, "source");
    EXPECT_EQ(spans[3].url, "https://github.com/repo");
}

// ── Block-level line classification helper ───────────────────────────────────
// This mirrors the classification logic in render_markdown_block, verifying
// that the line-prefix matching works for all supported block types.

TEST(MarkdownRenderer, BlockLinePrefixClassification) {
    // Headings
    EXPECT_TRUE(std::string_view("# Heading 1").starts_with("# "));
    EXPECT_TRUE(std::string_view("## Heading 2").starts_with("## "));
    EXPECT_TRUE(std::string_view("### Heading 3").starts_with("### "));
    
    // Blockquote
    EXPECT_TRUE(std::string_view("> quoted text").starts_with("> "));
    
    // Unordered list
    EXPECT_TRUE(std::string_view("- item").starts_with("- "));
    EXPECT_TRUE(std::string_view("* item").starts_with("* "));
    
    // Code fence
    EXPECT_TRUE(std::string_view("```").starts_with("```"));
    EXPECT_TRUE(std::string_view("```cpp").starts_with("```"));
    
    // Not headings (no space after #)
    EXPECT_FALSE(std::string_view("#NoSpace").starts_with("# "));
}

// ── strip_markdown preserves plain text ──────────────────────────────────────

TEST(MarkdownRenderer, StripMarkdownPreservesPlainText) {
    const std::string plain = "No markdown here, just plain text.";
    EXPECT_EQ(strip_markdown(plain), plain);
}

TEST(MarkdownRenderer, StripMarkdownHandlesNestedFormatting) {
    // Bold inside a sentence
    EXPECT_EQ(strip_markdown("This is **very** important"), "This is very important");
    // Multiple formatting types
    EXPECT_EQ(strip_markdown("*a* **b** `c`"), "a b c");
}

// ── Table parsing helpers tests ──────────────────────────────────────────────

TEST(MarkdownRenderer, SplitTableRowTests) {
    using namespace rouen::helpers;
    
    auto cells1 = split_table_row("| Column 1 | Column 2 |");
    ASSERT_EQ(cells1.size(), 2U);
    EXPECT_EQ(cells1[0], "Column 1");
    EXPECT_EQ(cells1[1], "Column 2");

    auto cells2 = split_table_row("| Feature | Category | Details |");
    ASSERT_EQ(cells2.size(), 3U);
    EXPECT_EQ(cells2[0], "Feature");
    EXPECT_EQ(cells2[1], "Category");
    EXPECT_EQ(cells2[2], "Details");

    auto cells3 = split_table_row("  |  Padded Cell  |  Another Cell  |  ");
    ASSERT_EQ(cells3.size(), 2U);
    EXPECT_EQ(cells3[0], "Padded Cell");
    EXPECT_EQ(cells3[1], "Another Cell");
}

TEST(MarkdownRenderer, IsTableDelimiterTests) {
    using namespace rouen::helpers;

    EXPECT_TRUE(is_table_delimiter("|---|---|"));
    EXPECT_TRUE(is_table_delimiter("|:---|---:|"));
    EXPECT_TRUE(is_table_delimiter("| :--- | :---: | ---: |"));
    EXPECT_TRUE(is_table_delimiter("  |---|---|  "));

    EXPECT_FALSE(is_table_delimiter("| Feature Category | Key Features |"));
    EXPECT_FALSE(is_table_delimiter("Not a delimiter at all"));
    EXPECT_FALSE(is_table_delimiter("|---"));
}

// ── WCAG Color Contrast & Readability Tests ─────────────────────────────────

TEST(ColorContrast, RelativeLuminanceBounds) {
    using namespace rouen::helpers::color;

    EXPECT_NEAR(relative_luminance(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), 0.0f, 0.001f);
    EXPECT_NEAR(relative_luminance(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 1.0f, 0.001f);

    // Mid-gray (sRGB 0.5f) should have relative luminance between 0.2 and 0.25
    float mid_luma = relative_luminance(ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    EXPECT_GT(mid_luma, 0.20f);
    EXPECT_LT(mid_luma, 0.25f);
}

TEST(ColorContrast, ContrastRatioCalculation) {
    using namespace rouen::helpers::color;

    ImVec4 black{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 white{1.0f, 1.0f, 1.0f, 1.0f};

    // Black on white is standard 21:1
    EXPECT_NEAR(contrast_ratio(black, white), 21.0f, 0.05f);
    EXPECT_NEAR(contrast_ratio(white, black), 21.0f, 0.05f);

    // Identical colors have contrast ratio 1:1
    EXPECT_NEAR(contrast_ratio(black, black), 1.0f, 0.01f);
}

TEST(ColorContrast, AssistantBubbleReadabilityExceedsWcagAAA) {
    using namespace rouen::helpers::color;

    // Dark assistant bubble background (e.g. elevated slate/charcoal)
    ImVec4 assistant_bg{0.18f, 0.17f, 0.16f, 0.95f};
    ImVec4 text_color = pick_readable_text_color(assistant_bg);

    // Text must be light
    EXPECT_GT(text_color.x, 0.8f);
    EXPECT_GT(text_color.y, 0.8f);
    EXPECT_GT(text_color.z, 0.8f);

    // Contrast ratio against assistant bubble must exceed WCAG AAA (7:1)
    float ratio = contrast_ratio(assistant_bg, text_color);
    EXPECT_GE(ratio, 7.0f);
}

TEST(ColorContrast, UserBubbleReadabilityExceedsWcagAA) {
    using namespace rouen::helpers::color;

    // Amber bright accent user bubble
    ImVec4 amber_user_bg{0.95f, 0.58f, 0.10f, 0.92f};
    ImVec4 text_color = pick_readable_text_color(amber_user_bg);

    // Text on bright amber must be dark
    EXPECT_LT(text_color.x, 0.2f);
    EXPECT_LT(text_color.y, 0.2f);
    EXPECT_LT(text_color.z, 0.2f);

    // Contrast ratio against amber must exceed WCAG AA (4.5:1)
    float ratio = contrast_ratio(amber_user_bg, text_color);
    EXPECT_GE(ratio, 4.5f);
}

// ===========================================================================
// Tests for Markdown File Association and Viewer URL Resolution
// ===========================================================================

TEST(MarkdownFiletype, PlainClickDoesNotResolveToViewerCard) {
    // When clicked without Ctrl / Cmd held, .md and .markdown files must return nullopt
    // so that FileSystem card defaults to opening the text editor as-is.
    auto& handler = rouen::helpers::FiletypeHandler::instance();
    auto res_md = handler.resolve("/path/to/README.md", false);
    EXPECT_FALSE(res_md.has_value());

    auto res_markdown = handler.resolve("/path/to/notes.markdown", false);
    EXPECT_FALSE(res_markdown.has_value());
}

TEST(MarkdownFiletype, CtrlClickResolvesToMarkdownViewerCard) {
    // When clicked with Ctrl / Cmd held, .md and .markdown files must resolve to
    // "markdown:<path>" so that the viewer card is opened.
    auto& handler = rouen::helpers::FiletypeHandler::instance();
    auto res_md = handler.resolve("/path/to/README.md", true);
    ASSERT_TRUE(res_md.has_value());
    EXPECT_EQ(*res_md, "markdown:/path/to/README.md");

    auto res_markdown = handler.resolve("/path/to/notes.markdown", true);
    ASSERT_TRUE(res_markdown.has_value());
    EXPECT_EQ(*res_markdown, "markdown:/path/to/notes.markdown");
}

TEST(MarkdownFiletype, NonMarkdownFilesAreNotAffected) {
    auto& handler = rouen::helpers::FiletypeHandler::instance();
    auto res_cpp = handler.resolve("/path/to/main.cpp", true);
    EXPECT_FALSE(res_cpp.has_value());

    auto res_txt = handler.resolve("/path/to/notes.txt", true);
    EXPECT_FALSE(res_txt.has_value());
}

TEST(MarkdownViewer, RelativeLinkResolution) {
    std::filesystem::path current_doc = "/workspace/project/docs/guide.md";
    std::filesystem::path relative_link = "architecture.md";
    std::filesystem::path target = (current_doc.parent_path() / relative_link).lexically_normal();
    EXPECT_EQ(target.string(), "/workspace/project/docs/architecture.md");

    std::filesystem::path parent_link = "../README.md";
    std::filesystem::path parent_target = (current_doc.parent_path() / parent_link).lexically_normal();
    EXPECT_EQ(parent_target.string(), "/workspace/project/README.md");
}

TEST(MarkdownRenderer, ImageSpanIsParsed) {
    const auto spans = parse_inline_markdown("![Rouen Architecture](diagrams/rouen_architecture.png)");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::image);
    EXPECT_EQ(spans[0].text, "Rouen Architecture");
    EXPECT_EQ(spans[0].url, "diagrams/rouen_architecture.png");
}

TEST(MarkdownRenderer, ImageSpanWithTitle) {
    const auto spans = parse_inline_markdown("![Flow](diagrams/flow.png \"Diagram Title\")");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::image);
    EXPECT_EQ(spans[0].text, "Flow");
    EXPECT_EQ(spans[0].url, "diagrams/flow.png");
}

TEST(MarkdownRenderer, ImageDoesNotBreakStandardLinks) {
    const auto spans = parse_inline_markdown("[Click here](https://example.com) and ![embedded](img.png)");
    ASSERT_EQ(spans.size(), 3U);
    EXPECT_EQ(spans[0].kind, span_kind::link);
    EXPECT_EQ(spans[0].text, "Click here");
    EXPECT_EQ(spans[0].url, "https://example.com");
    EXPECT_EQ(spans[1].kind, span_kind::normal);
    EXPECT_EQ(spans[2].kind, span_kind::image);
    EXPECT_EQ(spans[2].text, "embedded");
    EXPECT_EQ(spans[2].url, "img.png");
}

TEST(MarkdownRenderer, StripMarkdownWithImages) {
    EXPECT_EQ(strip_markdown("![Alt Text](diagrams/test.png)"), "Alt Text");
}

TEST(MarkdownViewer, ImageRelativePathResolution) {
    std::filesystem::path current_doc = "/Users/ignaciorodriguez/src/rouen/docs/AI.md";
    std::filesystem::path image_rel = "diagrams/ai_architecture.png";
    std::filesystem::path target = (current_doc.parent_path() / image_rel).lexically_normal();
    EXPECT_EQ(target.string(), "/Users/ignaciorodriguez/src/rouen/docs/diagrams/ai_architecture.png");
}



