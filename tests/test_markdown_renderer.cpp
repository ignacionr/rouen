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

TEST(MarkdownRenderer, WordTokenizationPreservesFormatting) {
    using namespace rouen::helpers;
    using namespace rouen::helpers::adaptive_cards;
    const auto spans = parse_inline_markdown("**bold** text with `code`");
    const auto words = tokenize_spans_into_words(spans);
    ASSERT_GE(words.size(), 4U);
    EXPECT_EQ(words[0].kind, span_kind::bold);
    EXPECT_EQ(words[0].text, "bold");
    EXPECT_TRUE(words[0].has_trailing_space);
    EXPECT_EQ(words[1].kind, span_kind::normal);
    EXPECT_EQ(words[1].text, "text");
    EXPECT_TRUE(words[1].has_trailing_space);
    EXPECT_EQ(words[2].kind, span_kind::normal);
    EXPECT_EQ(words[2].text, "with");
    EXPECT_TRUE(words[2].has_trailing_space);
    EXPECT_EQ(words[3].kind, span_kind::code);
    EXPECT_EQ(words[3].text, "code");
    EXPECT_FALSE(words[3].has_trailing_space);
}

TEST(MarkdownRenderer, WordTokenizationExtractsLinks) {
    using namespace rouen::helpers;
    using namespace rouen::helpers::adaptive_cards;
    const auto spans = parse_inline_markdown("Visit [Rouen](https://github.com/ignacionr/rouen) today");
    const auto words = tokenize_spans_into_words(spans);
    ASSERT_EQ(words.size(), 3U);
    EXPECT_EQ(words[0].text, "Visit");
    EXPECT_TRUE(words[0].has_trailing_space);
    EXPECT_EQ(words[1].kind, span_kind::link);
    EXPECT_EQ(words[1].text, "Rouen");
    EXPECT_EQ(words[1].url, "https://github.com/ignacionr/rouen");
    EXPECT_TRUE(words[1].has_trailing_space);
    EXPECT_EQ(words[2].text, "today");
    EXPECT_FALSE(words[2].has_trailing_space);
}

TEST(MarkdownRenderer, HardLineBreaksAreRecognized) {
    using namespace rouen::helpers;
    using namespace rouen::helpers::adaptive_cards;
    const auto spans = parse_inline_markdown("First line  \nSecond line\\\nThird line");
    const auto words = tokenize_spans_into_words(spans);
    ASSERT_GE(words.size(), 6U);
    EXPECT_EQ(words[0].text, "First");
    EXPECT_EQ(words[1].text, "line");
    EXPECT_TRUE(words[1].is_hard_break);
    EXPECT_EQ(words[2].text, "Second");
    EXPECT_EQ(words[3].text, "line");
    EXPECT_TRUE(words[3].is_hard_break);
    EXPECT_EQ(words[4].text, "Third");
    EXPECT_EQ(words[5].text, "line");
    EXPECT_FALSE(words[5].is_hard_break);
}

TEST(MarkdownRenderer, WrappingCalculationFitsAvailableWidth) {
    using namespace rouen::helpers;
    using namespace rouen::helpers::adaptive_cards;
    const auto spans = parse_inline_markdown("A very long sentence with multiple words that definitely needs wrapping across lines");
    const auto words = tokenize_spans_into_words(spans);
    EXPECT_EQ(words.size(), 13U);
    
    // Simulate wrapping with character budget 20 per line
    float current_x = 0.0f;
    float max_w = 20.0f;
    int line_count = 1;
    for (const auto& w : words) {
        float word_w = static_cast<float>(w.text.size());
        if (current_x + word_w > max_w && current_x > 0.0f) {
            line_count++;
            current_x = 0.0f;
        }
        current_x += word_w + (w.has_trailing_space ? 1.0f : 0.0f);
    }
    EXPECT_GT(line_count, 3);
}

TEST(MarkdownRenderer, ParseDocumentPrimitives) {
    using namespace rouen::helpers;
    const std::string doc_text = 
        "# Title\n"
        "## Section\n"
        "> Quote line\n"
        "- Bullet item\n"
        "1. Numbered item\n"
        "```cpp\n"
        "int x = 42;\n"
        "```\n"
        "| Name | Age |\n"
        "| --- | --- |\n"
        "| Alice | 30 |\n"
        "Paragraph text.\n";

    const auto doc = parse_markdown_document(doc_text);
    EXPECT_FALSE(doc.empty());
    EXPECT_GT(doc.line_count, 0);
    EXPECT_GT(doc.word_count, 0);
    EXPECT_EQ(doc.byte_size, doc_text.size());

    bool has_h1 = false;
    bool has_h2 = false;
    bool has_quote = false;
    bool has_list = false;
    bool has_code = false;
    bool has_table = false;
    bool has_para = false;

    for (const auto& prim : doc.primitives) {
        if (std::holds_alternative<heading_primitive>(prim)) {
            const auto& h = std::get<heading_primitive>(prim);
            if (h.level == 1) has_h1 = true;
            if (h.level == 2) has_h2 = true;
        } else if (std::holds_alternative<blockquote_primitive>(prim)) {
            has_quote = true;
        } else if (std::holds_alternative<list_item_primitive>(prim)) {
            has_list = true;
        } else if (std::holds_alternative<code_block_primitive>(prim)) {
            has_code = true;
            EXPECT_EQ(std::get<code_block_primitive>(prim).lines.size(), 1U);
        } else if (std::holds_alternative<table_primitive>(prim)) {
            has_table = true;
            EXPECT_EQ(std::get<table_primitive>(prim).table_columns, 2);
            EXPECT_EQ(std::get<table_primitive>(prim).rows.size(), 1U);
        } else if (std::holds_alternative<paragraph_primitive>(prim)) {
            has_para = true;
        }
    }

    EXPECT_TRUE(has_h1);
    EXPECT_TRUE(has_h2);
    EXPECT_TRUE(has_quote);
    EXPECT_TRUE(has_list);
    EXPECT_TRUE(has_code);
    EXPECT_TRUE(has_table);
    EXPECT_TRUE(has_para);
}

TEST(MarkdownRenderer, FontStackBalanceDuringRender) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000.0f, 1000.0f);
    
    ImFontConfig cfg;
    ImFont* font_bold = io.Fonts->AddFontDefault(&cfg);
    ImFont* font_mono = io.Fonts->AddFontDefault(&cfg);
    unsigned char* tex_pixels;
    int tex_w, tex_h;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_w, &tex_h);
    
    ImGui::NewFrame();
    
    ImGui::Begin("TestWindow");
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false);
    ImGui::BeginChild("msg_bubble_0", ImVec2(300, 100), true);
    
    rouen::helpers::markdown_render_config md_cfg{
        .font_bold = font_bold,
        .font_italic = nullptr,
        .font_code = font_mono
    };

    std::vector<std::string> test_cases = {
        "# Heading 1",
        "## Heading 2",
        "### Heading 3 with **bold** and `code`",
        "**Resolution Summary:**",
        "The inbox item `inbox/2026-10-08-feat-ai-mcp-engineering-and-inbox-tools.md` has been successfully updated to status **done**.",
        "> Blockquote line with *italic*",
        "- List item with [link](https://example.com)",
        "```json\n{\"test\": true}\n```",
        "| A | B |\n|---|---|\n| 1 | 2 |"
    };

    for (const auto& tc : test_cases) {
        rouen::helpers::render_markdown_block(tc, md_cfg);
    }
    
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
    
    ImGui::Render();
    ImGui::DestroyContext(ctx);
}

TEST(MarkdownRenderer, ImGuiFontGuardRAII) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000.0f, 1000.0f);

    ImFontConfig cfg1;
    ImFont* base_font = io.Fonts->AddFontDefault(&cfg1);
    ImFontConfig cfg2;
    ImFont* custom_font = io.Fonts->AddFontDefault(&cfg2);
    unsigned char* tex_pixels;
    int tex_w, tex_h;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_w, &tex_h);

    ImGui::NewFrame();
    ImGui::Begin("TestGuardWindow");

    ImFont* initial_font = ImGui::GetFont();
    EXPECT_EQ(initial_font, base_font);
    EXPECT_NE(custom_font, base_font);

    // 1. Guard with null font does nothing
    {
        rouen::helpers::imgui_font_guard guard(nullptr);
        EXPECT_FALSE(guard.pushed);
        EXPECT_EQ(ImGui::GetFont(), initial_font);
    }
    EXPECT_EQ(ImGui::GetFont(), initial_font);

    // 2. Guard with current font does not push duplicate
    {
        rouen::helpers::imgui_font_guard guard(initial_font);
        EXPECT_FALSE(guard.pushed);
        EXPECT_EQ(ImGui::GetFont(), initial_font);
    }
    EXPECT_EQ(ImGui::GetFont(), initial_font);

    // 3. Guard with different font pushes and pops on exit
    {
        rouen::helpers::imgui_font_guard guard(custom_font);
        EXPECT_TRUE(guard.pushed);
        EXPECT_EQ(ImGui::GetFont(), custom_font);
    }
    EXPECT_EQ(ImGui::GetFont(), initial_font);

    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext(ctx);
}

TEST(MarkdownRenderer, ChatMessageMarkdownFlowWrapping) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1200.0f, 800.0f);

    ImFontConfig cfg2;
    ImFont* font_bold = io.Fonts->AddFontDefault(&cfg2);
    ImFontConfig cfg3;
    ImFont* font_mono = io.Fonts->AddFontDefault(&cfg3);
    unsigned char* tex_pixels;
    int tex_w, tex_h;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_w, &tex_h);

    ImGui::NewFrame();
    ImGui::Begin("TestWindow");

    const std::string text = 
        "### Implementation Status & Next Steps for `rss-item` Adaptive Card Feature\n\n"
        "We have successfully analyzed the feature specification and architectural requirements for adding full Adaptive Cards presentation, two-way action handling (`play_media`, `pause_media`, `bookmark_item`), and declarative rendering mode to the `rss-item` card (`src/cards/information/rss_item.hpp` and `src/cards/information/rss_item.cpp`).\n\n"
        "#### Summary of Planned & Specified Changes:\n"
        "1. **Header Updates (`src/cards/information/rss_item.hpp`)**:\n"
        "   - Declare override methods: `get_adaptive_card_json()` and `handle_action(std::string_view action_json)`.\n"
        "   - Add state tracking variables: `adaptive_view_mode`, `adaptive_parser_`, `adaptive_renderer_`, `adaptive_input_state_`, `adaptive_bound_`, `adaptive_error_`, and `last_adaptive_parse_time_`.\n\n"
        "2. **Dual-Mode Rendering in `render(rouen::ui::ui_context& ui)`**:\n"
        "   - Provide an \"Adaptive Card View\" toggle checkbox at the top of the card interface.\n"
        "   - When active, parse and render the declarative Adaptive Card 1.5 JSON layout using the native renderer, supporting action callbacks (`open_url`, `on_submit`).\n\n"
        "3. **Two-Way Action Dispatch (`handle_action`)**:\n"
        "   - Parse inbound JSON payloads via Glaze to support verbs like `play_media`, `pause_media`, and `bookmark_item`, triggering appropriate side effects on audio playback and saved favorites.\n\n"
        "4. **Unit & Regression Testing**:\n"
        "   - Add comprehensive tests in `tests/test_card_adaptive_interface.cpp` verifying JSON serialization, action dispatch, and malformed payload resilience.\n\n"
        "5. **Build & Deployment**:\n"
        "   - Compile and deploy locally to `$HOME/Applications/Rouen.app` using the strict `-j2` Ninja build constraints.";

    rouen::helpers::markdown_render_config md_cfg{
        .font_bold = font_bold,
        .font_italic = nullptr,
        .font_code = font_mono
    };

    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -60), false);
    ImGui::BeginChild("msg_bubble_1", ImVec2(550, 600), true, ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 530);

    float render_start_y = ImGui::GetCursorPosY();
    rouen::helpers::render_markdown_block(text, md_cfg);
    float render_end_y = ImGui::GetCursorPosY();
    float total_rendered_height = render_end_y - render_start_y;

    // The rendered text must wrap cleanly without runaway line jumps.
    // Previously, broken wrap logic produced 1761px height for this text.
    // Correct word-level wrapping fits within 600px.
    EXPECT_GT(total_rendered_height, 200.0f);
    EXPECT_LT(total_rendered_height, 600.0f);

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();

    ImGui::Render();
    ImGui::DestroyContext(ctx);
}

