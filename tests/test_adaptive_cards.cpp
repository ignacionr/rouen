#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>

#include "../src/helpers/adaptive_cards/markdown.hpp"
#include "../src/helpers/adaptive_cards/parser.hpp"
#include "../src/helpers/adaptive_cards/renderer.hpp"
#include "../src/helpers/adaptive_cards/templater.hpp"

using namespace rouen::helpers::adaptive_cards;

TEST(AdaptiveCardsRound1, ParsesMinimalAdaptiveCard) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "TextBlock",
      "id": "title",
      "text": "Hello ${name}"
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.type, "AdaptiveCard");
    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].type, "TextBlock");
    EXPECT_EQ(parsed.body[0].id, "title");
    EXPECT_EQ(parsed.body[0].text, "Hello ${name}");
}

TEST(AdaptiveCardsRound1, BindsFlatStringVariables) {
    element greeting{};
    greeting.type = "TextBlock";
    greeting.id = "greeting";
    greeting.text = "Hello ${name}";

    card_document card{};
    card.type = "AdaptiveCard";
    card.body = {greeting};

    templater binder{};
    context values{};
    auto parse_err = glz::read_json(values, R"JSON({"name":"Rouen"})JSON");
    ASSERT_FALSE(parse_err);
    const auto bound = binder.bind(card, values);

    ASSERT_EQ(bound.body.size(), 1U);
    EXPECT_EQ(bound.body[0].text, "Hello Rouen");
}

TEST(AdaptiveCardsRound2, ParsesContainersColumnsAndFactSets) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Container",
      "items": [
        { "type": "TextBlock", "text": "${user.profile.name}", "size": "Large", "weight": "Bolder", "color": "Accent" },
        {
          "type": "ColumnSet",
          "columns": [
            { "type": "Column", "items": [ { "type": "TextBlock", "text": "Left" } ] },
            { "type": "Column", "items": [ { "type": "TextBlock", "text": "Right" } ] }
          ]
        },
        {
          "type": "FactSet",
          "facts": [
            { "title": "Status", "value": "${user.status}" },
            { "title": "Team", "value": "${user.team.name}" }
          ]
        }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 1U);
    ASSERT_EQ(parsed.body[0].type, "Container");
    ASSERT_EQ(parsed.body[0].items.size(), 3U);
    EXPECT_EQ(parsed.body[0].items[1].type, "ColumnSet");
    EXPECT_EQ(parsed.body[0].items[2].type, "FactSet");
    ASSERT_EQ(parsed.body[0].items[2].facts.size(), 2U);
}

TEST(AdaptiveCardsRound2, BindsNestedDotNotationAndCollectsLines) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Container",
      "items": [
        { "type": "TextBlock", "text": "Hello ${user.profile.name}" },
        { "type": "FactSet", "facts": [ { "title": "Status", "value": "${user.status}" } ] }
      ]
    }
  ]
}
)JSON";

    const std::string ctx_json = R"JSON(
{
  "user": {
    "profile": { "name": "Rouen" },
    "status": "online"
  }
}
)JSON";

    parser card_parser{};
    templater binder{};
    const auto parsed = card_parser.parse(card_json);

    context values{};
    auto parse_err = glz::read_json(values, ctx_json);
    ASSERT_FALSE(parse_err);
    const auto bound = binder.bind(parsed, values);

    const auto lines = renderer::collect_lines(bound);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Hello Rouen");
    EXPECT_EQ(lines[1], "Status: online");
}

TEST(AdaptiveCardsRound2, RejectsUnsupportedElementTypes) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Image",
      "id": "unsupported",
      "text": "ignored"
    }
  ]
}
)JSON";

    parser card_parser{};
    EXPECT_THROW(
        {
            static_cast<void>(card_parser.parse(card_json));
        },
        std::runtime_error
    );
}

TEST(AdaptiveCardsRound3, ParsesInputsAndOpenUrlActions) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "Input.Text", "id": "query", "title": "Query", "placeholder": "owner/repo" },
    { "type": "Input.Toggle", "id": "enabled", "title": "Enabled", "value": "true" }
  ],
  "actions": [
    { "type": "Action.OpenUrl", "title": "Open Repo", "url": "https://github.com/${repo_owner}/${repo_name}" }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 2U);
    EXPECT_EQ(parsed.body[0].type, "Input.Text");
    EXPECT_EQ(parsed.body[1].type, "Input.Toggle");
    ASSERT_EQ(parsed.actions.size(), 1U);
    EXPECT_EQ(parsed.actions[0].type, "Action.OpenUrl");
}

TEST(AdaptiveCardsRound3, BindsActionUrlsWithExpressions) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "Input.Text", "id": "query", "title": "Repo", "value": "${repo_owner}/${repo_name}" }
  ],
  "actions": [
    { "type": "Action.OpenUrl", "title": "Open Repo", "url": "https://github.com/${repo_owner}/${repo_name}" }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON(
{
  "repo_owner": "ignacionr",
  "repo_name": "rouen"
}
)JSON";

    parser card_parser{};
    templater binder{};

    context values{};
    auto parse_err = glz::read_json(values, ctx_json);
    ASSERT_FALSE(parse_err);

    const auto parsed = card_parser.parse(card_json);
    const auto bound = binder.bind(parsed, values);

    ASSERT_EQ(bound.body.size(), 1U);
    EXPECT_EQ(bound.body[0].value, "ignacionr/rouen");
    const auto urls = renderer::collect_action_urls(bound);
    ASSERT_EQ(urls.size(), 1U);
    EXPECT_EQ(urls[0], "https://github.com/ignacionr/rouen");
}

TEST(AdaptiveCardsRound4, ParsesRepeatAndAdvancedActions) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Container",
      "$data": "${notifications}",
      "items": [
        { "type": "TextBlock", "text": "${title}" }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.ShowCard",
      "title": "Details",
      "card": {
        "type": "AdaptiveCard",
        "body": [
          { "type": "TextBlock", "text": "Details panel" }
        ]
      }
    },
    { "type": "Action.Submit", "title": "Submit" }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);
    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].data, "${notifications}");
    ASSERT_EQ(parsed.actions.size(), 2U);
    EXPECT_EQ(parsed.actions[0].type, "Action.ShowCard");
    EXPECT_EQ(parsed.actions[1].type, "Action.Submit");
    ASSERT_EQ(parsed.actions[0].card.body.size(), 1U);
}

TEST(AdaptiveCardsRound4, ExpandsDataRepeatsOverArrays) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Container",
      "$data": "${notifications}",
      "items": [
        { "type": "TextBlock", "text": "${title} (${severity})" }
      ]
    }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON(
{
  "notifications": [
    { "title": "Build Failed", "severity": "critical" },
    { "title": "Review Requested", "severity": "info" }
  ]
}
)JSON";

    parser card_parser{};
    templater binder{};
    context values{};
    auto parse_err = glz::read_json(values, ctx_json);
    ASSERT_FALSE(parse_err);

    const auto parsed = card_parser.parse(card_json);
    const auto bound = binder.bind(parsed, values);
    ASSERT_EQ(bound.body.size(), 2U);
    const auto lines = renderer::collect_lines(bound);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Build Failed (critical)");
    EXPECT_EQ(lines[1], "Review Requested (info)");
}

TEST(AdaptiveCardsRound4, BuildsSubmitPayloadFromInputState) {
    renderer::input_state state{};
    state.text_values["comment"] = "Looks good";
    state.toggle_values["acknowledged"] = true;

    const std::string payload = renderer::build_submit_payload(state);
    glz::json_t parsed{};
    auto err = glz::read_json(parsed, payload);
    ASSERT_FALSE(err);
    ASSERT_TRUE(parsed.contains("text"));
    ASSERT_TRUE(parsed.contains("toggle"));
    EXPECT_EQ(parsed["text"]["comment"].get<std::string>(), "Looks good");
    EXPECT_TRUE(parsed["toggle"]["acknowledged"].get<bool>());
}

TEST(AdaptiveCardsRound4, RejectsUnsupportedActionType) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "Hi" }
  ],
  "actions": [
    { "type": "Action.Execute", "title": "Unsupported" }
  ]
}
)JSON";
    parser card_parser{};
    EXPECT_THROW(
        {
            static_cast<void>(card_parser.parse(card_json));
        },
        std::runtime_error
    );
}

TEST(AdaptiveCardsRound3, CollectsInputLinesFromCard) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "Input.Text", "id": "repo", "title": "Repo" },
    { "type": "Input.Toggle", "id": "enabled", "title": "Enabled" }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);
    const auto lines = renderer::collect_lines(parsed);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Input.Text: repo");
    EXPECT_EQ(lines[1], "Input.Toggle: enabled");
}

TEST(AdaptiveCardsRound4, BindsShowCardNestedFacts) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [],
  "actions": [
    {
      "type": "Action.ShowCard",
      "title": "Details",
      "card": {
        "type": "AdaptiveCard",
        "body": [
          {
            "type": "FactSet",
            "facts": [
              { "title": "Total", "value": "${summary.total}" },
              { "title": "Critical", "value": "${summary.critical}" }
            ]
          }
        ]
      }
    }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON(
{
  "summary": { "total": 3, "critical": 1 }
}
)JSON";

    parser card_parser{};
    templater binder{};
    context values{};
    auto parse_err = glz::read_json(values, ctx_json);
    ASSERT_FALSE(parse_err);

    const auto parsed = card_parser.parse(card_json);
    const auto bound = binder.bind(parsed, values);

    ASSERT_EQ(bound.actions.size(), 1U);
    ASSERT_EQ(bound.actions[0].card.body.size(), 1U);
    ASSERT_EQ(bound.actions[0].card.body[0].facts.size(), 2U);
    EXPECT_EQ(bound.actions[0].card.body[0].facts[0].value, "3");
    EXPECT_EQ(bound.actions[0].card.body[0].facts[1].value, "1");
}

// ──────────────────────────────────────────────────────────────────────────────
// Round 5 – Basic Markdown Text Support
// ──────────────────────────────────────────────────────────────────────────────

TEST(AdaptiveCardsRound5, PlainTextProducesSingleNormalSpan) {
    const auto spans = parse_inline_markdown("Hello world");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "Hello world");
}

TEST(AdaptiveCardsRound5, EmptyStringProducesNoSpans) {
    const auto spans = parse_inline_markdown("");
    EXPECT_TRUE(spans.empty());
}

TEST(AdaptiveCardsRound5, ParsesBoldSpan) {
    const auto spans = parse_inline_markdown("**bold**");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::bold);
    EXPECT_EQ(spans[0].text, "bold");
}

TEST(AdaptiveCardsRound5, ParsesItalicAsterisk) {
    const auto spans = parse_inline_markdown("*italic*");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::italic);
    EXPECT_EQ(spans[0].text, "italic");
}

TEST(AdaptiveCardsRound5, ParsesItalicUnderscore) {
    const auto spans = parse_inline_markdown("_italic_");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::italic);
    EXPECT_EQ(spans[0].text, "italic");
}

TEST(AdaptiveCardsRound5, ParsesCodeSpan) {
    const auto spans = parse_inline_markdown("`inline code`");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::code);
    EXPECT_EQ(spans[0].text, "inline code");
}

TEST(AdaptiveCardsRound5, ParsesLinkSpan) {
    const auto spans = parse_inline_markdown("[Rouen](https://github.com/ignacionr/rouen)");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::link);
    EXPECT_EQ(spans[0].text, "Rouen");
    EXPECT_EQ(spans[0].url, "https://github.com/ignacionr/rouen");
}

TEST(AdaptiveCardsRound5, ParsesMixedSpans) {
    // "Hello **world**!" → normal + bold + normal
    const auto spans = parse_inline_markdown("Hello **world**!");
    ASSERT_EQ(spans.size(), 3U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "Hello ");
    EXPECT_EQ(spans[1].kind, span_kind::bold);
    EXPECT_EQ(spans[1].text, "world");
    EXPECT_EQ(spans[2].kind, span_kind::normal);
    EXPECT_EQ(spans[2].text, "!");
}

TEST(AdaptiveCardsRound5, ParsesAllKindsInSequence) {
    // "a **b** *c* `d`" → normal + bold + normal + italic + normal + code
    const auto spans = parse_inline_markdown("a **b** *c* `d`");
    ASSERT_EQ(spans.size(), 6U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);  EXPECT_EQ(spans[0].text, "a ");
    EXPECT_EQ(spans[1].kind, span_kind::bold);    EXPECT_EQ(spans[1].text, "b");
    EXPECT_EQ(spans[2].kind, span_kind::normal);  EXPECT_EQ(spans[2].text, " ");
    EXPECT_EQ(spans[3].kind, span_kind::italic);  EXPECT_EQ(spans[3].text, "c");
    EXPECT_EQ(spans[4].kind, span_kind::normal);  EXPECT_EQ(spans[4].text, " ");
    EXPECT_EQ(spans[5].kind, span_kind::code);    EXPECT_EQ(spans[5].text, "d");
}

TEST(AdaptiveCardsRound5, ParsesAllKindsNoGaps) {
    const auto spans = parse_inline_markdown("**b***i*`c`[l](u)");
    ASSERT_EQ(spans.size(), 4U);
    EXPECT_EQ(spans[0].kind, span_kind::bold);    EXPECT_EQ(spans[0].text, "b");
    EXPECT_EQ(spans[1].kind, span_kind::italic);  EXPECT_EQ(spans[1].text, "i");
    EXPECT_EQ(spans[2].kind, span_kind::code);    EXPECT_EQ(spans[2].text, "c");
    EXPECT_EQ(spans[3].kind, span_kind::link);    EXPECT_EQ(spans[3].text, "l");
    EXPECT_EQ(spans[3].url, "u");
}

TEST(AdaptiveCardsRound5, EscapedCharacterIsLiteral) {
    const auto spans = parse_inline_markdown("Hello \\*world\\*");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "Hello *world*");
}

TEST(AdaptiveCardsRound5, UnterminatedMarkerIsLiteral) {
    // "*hello" — no closing *, so treated as literal normal text.
    const auto spans = parse_inline_markdown("*hello");
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans[0].kind, span_kind::normal);
    EXPECT_EQ(spans[0].text, "*hello");
}

TEST(AdaptiveCardsRound5, StripMarkdownPlainText) {
    EXPECT_EQ(strip_markdown("Hello world"), "Hello world");
}

TEST(AdaptiveCardsRound5, StripMarkdownRemovesAllMarkers) {
    EXPECT_EQ(strip_markdown("**bold** *italic* `code` [link](url)"), "bold italic code link");
}

TEST(AdaptiveCardsRound5, StripMarkdownEmptyString) {
    EXPECT_EQ(strip_markdown(""), "");
}

TEST(AdaptiveCardsRound5, CollectLinesStripsMarkdown) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "**Status:** critical" },
    { "type": "TextBlock", "text": "Reported by *ignacionr*" }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);
    const auto lines = renderer::collect_lines(parsed);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Status: critical");
    EXPECT_EQ(lines[1], "Reported by ignacionr");
}

TEST(AdaptiveCardsRound5, TemplatingInsideMarkdownSpans) {
    // Binding expressions work transparently inside markdown markers.
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "**${project}** by _${author}_" }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON({"project": "Rouen", "author": "ignacionr"})JSON";

    parser card_parser{};
    templater binder{};
    context values{};
    auto err = glz::read_json(values, ctx_json);
    ASSERT_FALSE(err);

    const auto parsed = card_parser.parse(card_json);
    const auto bound = binder.bind(parsed, values);

    ASSERT_EQ(bound.body.size(), 1U);
    const auto spans = parse_inline_markdown(bound.body[0].text);
    ASSERT_EQ(spans.size(), 3U);
    EXPECT_EQ(spans[0].kind, span_kind::bold);   EXPECT_EQ(spans[0].text, "Rouen");
    EXPECT_EQ(spans[1].kind, span_kind::normal);
    EXPECT_EQ(spans[2].kind, span_kind::italic);  EXPECT_EQ(spans[2].text, "ignacionr");
}

TEST(AdaptiveCardsRound5, FullRound5CardParsesAndBinds) {
    // Smoke-test against the same JSON as the round5 preset sample.
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "**Status Update** for ${project}", "size": "Large" },
    { "type": "TextBlock", "text": "Reported by _${author}_ on `${date}`" },
    { "type": "TextBlock", "text": "**Severity:** ${severity} — *${status}*" },
    { "type": "TextBlock", "text": "See [release notes](${release_url}) for full details." }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON({
  "project": "Rouen",
  "author": "ignacionr",
  "date": "2026-07-17",
  "severity": "low",
  "status": "in progress",
  "release_url": "https://github.com/ignacionr/rouen/releases"
})JSON";

    parser card_parser{};
    templater binder{};
    context values{};
    auto err = glz::read_json(values, ctx_json);
    ASSERT_FALSE(err);

    const auto parsed = card_parser.parse(card_json);
    const auto bound = binder.bind(parsed, values);

    ASSERT_EQ(bound.body.size(), 4U);

    // Row 0: header — large text, stripped by collect_lines.
    const auto lines = renderer::collect_lines(bound);
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_EQ(lines[0], "Status Update for Rouen");

    // Row 1: italic author + code date.
    {
        const auto spans = parse_inline_markdown(bound.body[1].text);
        ASSERT_GE(spans.size(), 2U);
        // Find the italic span (author).
        const auto it = std::ranges::find_if(spans, [](const text_span& s) {
            return s.kind == span_kind::italic;
        });
        ASSERT_NE(it, spans.end());
        EXPECT_EQ(it->text, "ignacionr");
    }

    // Row 3: link points to the resolved URL.
    {
        const auto spans = parse_inline_markdown(bound.body[3].text);
        const auto link_it = std::ranges::find_if(spans, [](const text_span& s) {
            return s.kind == span_kind::link;
        });
        ASSERT_NE(link_it, spans.end());
        EXPECT_EQ(link_it->text, "release notes");
        EXPECT_EQ(link_it->url, "https://github.com/ignacionr/rouen/releases");
    }
}
