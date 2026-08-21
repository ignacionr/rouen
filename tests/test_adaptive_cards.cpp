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
      "type": "Widget.Unknown",
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
    ASSERT_TRUE(parsed.contains("comment"));
    ASSERT_TRUE(parsed.contains("acknowledged"));
    EXPECT_EQ(parsed["comment"].get<std::string>(), "Looks good");
    EXPECT_TRUE(parsed["acknowledged"].get<bool>());
}

TEST(AdaptiveCardsRound4, RejectsUnsupportedActionType) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "Hi" }
  ],
  "actions": [
    { "type": "Action.Unknown", "title": "Unsupported" }
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

TEST(AdaptiveCardsRound5, EscapedUnderscoreInPlainTextAndFormatting) {
    // Escaped underscore in plain text
    {
        const auto spans = parse_inline_markdown("foo\\_bar");
        ASSERT_EQ(spans.size(), 1U);
        EXPECT_EQ(spans[0].kind, span_kind::normal);
        EXPECT_EQ(spans[0].text, "foo_bar");
    }

    // Escaped underscore inside italic underscore span
    {
        const auto spans = parse_inline_markdown("_italic\\_text_");
        ASSERT_EQ(spans.size(), 1U);
        EXPECT_EQ(spans[0].kind, span_kind::italic);
        EXPECT_EQ(spans[0].text, "italic_text");
    }

    // Escaped asterisk inside bold span
    {
        const auto spans = parse_inline_markdown("**bold\\*\\*text**");
        ASSERT_EQ(spans.size(), 1U);
        EXPECT_EQ(spans[0].kind, span_kind::bold);
        EXPECT_EQ(spans[0].text, "bold**text");
    }

    // Escaped brackets and parens in links
    {
        const auto spans = parse_inline_markdown("[link\\]label](https://example.com/path\\)url)");
        ASSERT_EQ(spans.size(), 1U);
        EXPECT_EQ(spans[0].kind, span_kind::link);
        EXPECT_EQ(spans[0].text, "link]label");
        EXPECT_EQ(spans[0].url, "https://example.com/path)url");
    }
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

TEST(AdaptiveCardsNewFeatures, ParsesInputChoiceSet) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Input.ChoiceSet",
      "id": "colorChoice",
      "style": "compact",
      "isMultiSelect": false,
      "value": "red",
      "choices": [
        { "title": "Red", "value": "red" },
        { "title": "Green", "value": "green" },
        { "title": "Blue", "value": "blue" }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].type, "Input.ChoiceSet");
    EXPECT_EQ(parsed.body[0].id, "colorChoice");
    EXPECT_EQ(parsed.body[0].style, "compact");
    EXPECT_FALSE(parsed.body[0].isMultiSelect);
    EXPECT_EQ(parsed.body[0].choices.size(), 3U);
    if (parsed.body[0].choices.size() >= 3U) {
        EXPECT_EQ(parsed.body[0].choices[0].title, "Red");
        EXPECT_EQ(parsed.body[0].choices[0].value, "red");
    }
}

TEST(AdaptiveCardsNewFeatures, ParsesInputNumberDateAndTime) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "Input.Number", "id": "quantity", "min": 1, "max": 10, "value": "5" },
    { "type": "Input.Date", "id": "startDate", "value": "2026-07-22" },
    { "type": "Input.Time", "id": "meetingTime", "value": "14:30" }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 3U);
    EXPECT_EQ(parsed.body[0].type, "Input.Number");
    EXPECT_EQ(parsed.body[0].min, 1.0);
    EXPECT_EQ(parsed.body[0].max, 10.0);
    EXPECT_EQ(parsed.body[1].type, "Input.Date");
    EXPECT_EQ(parsed.body[1].value, "2026-07-22");
    EXPECT_EQ(parsed.body[2].type, "Input.Time");
    EXPECT_EQ(parsed.body[2].value, "14:30");
}

TEST(AdaptiveCardsNewFeatures, ParsesMediaElement) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Media",
      "poster": "https://example.com/poster.jpg",
      "altText": "Sample Video",
      "sources": [
        { "mimeType": "video/mp4", "url": "https://example.com/video.mp4" }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].type, "Media");
    EXPECT_EQ(parsed.body[0].poster, "https://example.com/poster.jpg");
    EXPECT_EQ(parsed.body[0].altText, "Sample Video");
    ASSERT_EQ(parsed.body[0].sources.size(), 1U);
    EXPECT_EQ(parsed.body[0].sources[0].mimeType, "video/mp4");
    EXPECT_EQ(parsed.body[0].sources[0].url, "https://example.com/video.mp4");
}

TEST(AdaptiveCardsNewFeatures, ParsesImageSet) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "ImageSet",
      "imageSize": "medium",
      "images": [
        { "type": "Image", "url": "https://example.com/img1.png" },
        { "type": "Image", "url": "https://example.com/img2.png" }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].type, "ImageSet");
    EXPECT_EQ(parsed.body[0].imageSize, "medium");
    ASSERT_EQ(parsed.body[0].images.size(), 2U);
    EXPECT_EQ(parsed.body[0].images[0].url, "https://example.com/img1.png");
}

TEST(AdaptiveCardsNewFeatures, ParsesRichTextBlockAndTextRuns) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "RichTextBlock",
      "inlines": [
        { "type": "TextRun", "text": "Hello ", "bold": true },
        { "type": "TextRun", "text": "World", "italic": true, "color": "Accent" }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].type, "RichTextBlock");
    ASSERT_EQ(parsed.body[0].inlines.size(), 2U);
    EXPECT_EQ(parsed.body[0].inlines[0].text, "Hello ");
    EXPECT_TRUE(parsed.body[0].inlines[0].bold);
    EXPECT_EQ(parsed.body[0].inlines[1].text, "World");
    EXPECT_TRUE(parsed.body[0].inlines[1].italic);
}

TEST(AdaptiveCardsNewFeatures, ParsesTableStructure) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Table",
      "columns": [
        { "width": 1 },
        { "width": 2 }
      ],
      "rows": [
        {
          "type": "TableRow",
          "cells": [
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "Header 1" } ] },
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "Header 2" } ] }
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
    EXPECT_EQ(parsed.body[0].type, "Table");
    ASSERT_EQ(parsed.body[0].columns.size(), 2U);
    EXPECT_EQ(parsed.body[0].columns[0].width, "1");
    EXPECT_EQ(parsed.body[0].columns[1].width, "2");
    ASSERT_EQ(parsed.body[0].rows.size(), 1U);
    EXPECT_EQ(parsed.body[0].rows[0].cells.size(), 2U);
}

TEST(AdaptiveCardsNewFeatures, ParsesTableWithStretchAndCustomColumnWidths) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Table",
      "columns": [
        { "type": "TableColumnDefinition", "width": "stretch" },
        { "type": "TableColumnDefinition", "width": "auto" },
        { "type": "TableColumnDefinition", "width": "120px" },
        { "width": "stretch" }
      ],
      "rows": [
        {
          "type": "TableRow",
          "cells": [
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "Stretch Col" } ] },
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "Auto Col" } ] },
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "Fixed Col" } ] },
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "Default Stretch" } ] }
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
    EXPECT_EQ(parsed.body[0].type, "Table");
    ASSERT_EQ(parsed.body[0].columns.size(), 4U);
    EXPECT_EQ(parsed.body[0].columns[0].type, "TableColumnDefinition");
    EXPECT_EQ(parsed.body[0].columns[0].width, "stretch");
    EXPECT_EQ(parsed.body[0].columns[1].type, "TableColumnDefinition");
    EXPECT_EQ(parsed.body[0].columns[1].width, "auto");
    EXPECT_EQ(parsed.body[0].columns[2].type, "TableColumnDefinition");
    EXPECT_EQ(parsed.body[0].columns[2].width, "120px");
    EXPECT_EQ(parsed.body[0].columns[3].width, "stretch");
}

TEST(AdaptiveCardsNewFeatures, BindsTableColumnWidthsAndCells) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Table",
      "columns": [
        { "width": "${col1_width}" },
        { "width": "${col2_width}" }
      ],
      "rows": [
        {
          "type": "TableRow",
          "cells": [
            { "type": "TableCell", "items": [ { "type": "TextBlock", "text": "${row1_val}" } ] }
          ]
        }
      ]
    }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON(
{
  "col1_width": "stretch",
  "col2_width": "auto",
  "row1_val": "Cell Value"
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
    ASSERT_EQ(bound.body[0].columns.size(), 2U);
    EXPECT_EQ(bound.body[0].columns[0].width, "stretch");
    EXPECT_EQ(bound.body[0].columns[1].width, "auto");
    ASSERT_EQ(bound.body[0].rows.size(), 1U);
    ASSERT_EQ(bound.body[0].rows[0].cells.size(), 1U);
    ASSERT_EQ(bound.body[0].rows[0].cells[0].items.size(), 1U);
    EXPECT_EQ(bound.body[0].rows[0].cells[0].items[0].text, "Cell Value");
}

TEST(AdaptiveCardsNewFeatures, ParsesActionToggleVisibilityAndExecute) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "actions": [
    {
      "type": "Action.ToggleVisibility",
      "title": "Toggle Details",
      "targetElements": ["detailsPanel"]
    },
    {
      "type": "Action.Execute",
      "title": "Do Action",
      "verb": "doWork"
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.actions.size(), 2U);
    EXPECT_EQ(parsed.actions[0].type, "Action.ToggleVisibility");
    ASSERT_EQ(parsed.actions[0].targetElements.size(), 1U);
    EXPECT_EQ(parsed.actions[0].targetElements[0], "detailsPanel");
    EXPECT_EQ(parsed.actions[1].type, "Action.Execute");
    EXPECT_EQ(parsed.actions[1].verb, "doWork");
}

TEST(AdaptiveCardsNewFeatures, ParsesContainerStyleSpacingAndSeparator) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Container",
      "style": "emphasis",
      "spacing": "large",
      "separator": true,
      "selectAction": {
        "type": "Action.OpenUrl",
        "url": "https://example.com"
      },
      "items": [
        { "type": "TextBlock", "text": "Clickable Styled Container" }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 1U);
    EXPECT_EQ(parsed.body[0].type, "Container");
    EXPECT_EQ(parsed.body[0].style, "emphasis");
    EXPECT_EQ(parsed.body[0].spacing, "large");
    EXPECT_TRUE(parsed.body[0].separator);
    EXPECT_EQ(parsed.body[0].selectAction.type, "Action.OpenUrl");
    EXPECT_EQ(parsed.body[0].selectAction.url, "https://example.com");
}

TEST(AdaptiveCardsNewFeatures, ParsesLowercaseColorNames) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "Red", "color": "attention" },
    { "type": "TextBlock", "text": "Yellow", "color": "warning" },
    { "type": "TextBlock", "text": "Blue", "color": "good" },
    { "type": "TextBlock", "text": "Header", "color": "accent" }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 4U);
    EXPECT_EQ(parsed.body[0].color, "attention");
    EXPECT_EQ(parsed.body[1].color, "warning");
    EXPECT_EQ(parsed.body[2].color, "good");
    EXPECT_EQ(parsed.body[3].color, "accent");
}

// ──────────────────────────────────────────────────────────────────────────────
// ActionSet Support Tests
// ──────────────────────────────────────────────────────────────────────────────

TEST(AdaptiveCardsActionSet, ParsesActionSetWithMultipleActions) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "TextBlock",
      "text": "Inline actions below:"
    },
    {
      "type": "ActionSet",
      "id": "mainActionSet",
      "actions": [
        {
          "type": "Action.OpenUrl",
          "title": "Visit Website",
          "url": "https://adaptivecards.io"
        },
        {
          "type": "Action.Submit",
          "title": "Submit Response",
          "data": { "actionKey": "submit123" }
        },
        {
          "type": "Action.Execute",
          "title": "Execute Verb",
          "verb": "doProcess"
        },
        {
          "type": "Action.ToggleVisibility",
          "title": "Toggle Details",
          "targetElements": ["detailsPanel"]
        },
        {
          "type": "Action.ShowCard",
          "title": "More Options",
          "card": {
            "type": "AdaptiveCard",
            "body": [
              { "type": "TextBlock", "text": "Expanded options panel" }
            ]
          }
        }
      ]
    }
  ]
}
)JSON";

    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.body.size(), 2U);
    EXPECT_EQ(parsed.body[0].type, "TextBlock");
    EXPECT_EQ(parsed.body[1].type, "ActionSet");
    EXPECT_EQ(parsed.body[1].id, "mainActionSet");
    ASSERT_EQ(parsed.body[1].actions.size(), 5U);

    EXPECT_EQ(parsed.body[1].actions[0].type, "Action.OpenUrl");
    EXPECT_EQ(parsed.body[1].actions[0].title, "Visit Website");
    EXPECT_EQ(parsed.body[1].actions[0].url, "https://adaptivecards.io");

    EXPECT_EQ(parsed.body[1].actions[1].type, "Action.Submit");
    EXPECT_EQ(parsed.body[1].actions[1].title, "Submit Response");
    ASSERT_TRUE(parsed.body[1].actions[1].data.is_object());
    EXPECT_EQ(parsed.body[1].actions[1].data["actionKey"].get<std::string>(), "submit123");

    EXPECT_EQ(parsed.body[1].actions[2].type, "Action.Execute");
    EXPECT_EQ(parsed.body[1].actions[2].verb, "doProcess");

    EXPECT_EQ(parsed.body[1].actions[3].type, "Action.ToggleVisibility");
    ASSERT_EQ(parsed.body[1].actions[3].targetElements.size(), 1U);
    EXPECT_EQ(parsed.body[1].actions[3].targetElements[0], "detailsPanel");

    EXPECT_EQ(parsed.body[1].actions[4].type, "Action.ShowCard");
    ASSERT_EQ(parsed.body[1].actions[4].card.body.size(), 1U);
    EXPECT_EQ(parsed.body[1].actions[4].card.body[0].text, "Expanded options panel");
}

TEST(AdaptiveCardsActionSet, ParsesActionSetInsideContainerAndColumns) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "Container",
      "items": [
        {
          "type": "ColumnSet",
          "columns": [
            {
              "type": "Column",
              "items": [
                {
                  "type": "ActionSet",
                  "actions": [
                    { "type": "Action.OpenUrl", "title": "Col 1 Action", "url": "https://col1.com" }
                  ]
                }
              ]
            },
            {
              "type": "Column",
              "items": [
                {
                  "type": "ActionSet",
                  "actions": [
                    { "type": "Action.Submit", "title": "Col 2 Action" }
                  ]
                }
              ]
            }
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
    EXPECT_EQ(parsed.body[0].type, "Container");
    ASSERT_EQ(parsed.body[0].items.size(), 1U);
    EXPECT_EQ(parsed.body[0].items[0].type, "ColumnSet");
    ASSERT_EQ(parsed.body[0].items[0].columns.size(), 2U);
    ASSERT_EQ(parsed.body[0].items[0].columns[0].items.size(), 1U);
    EXPECT_EQ(parsed.body[0].items[0].columns[0].items[0].type, "ActionSet");
    EXPECT_EQ(parsed.body[0].items[0].columns[0].items[0].actions[0].url, "https://col1.com");
    EXPECT_EQ(parsed.body[0].items[0].columns[1].items[0].actions[0].type, "Action.Submit");
}

TEST(AdaptiveCardsActionSet, RejectsInvalidActionInsideActionSet) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "ActionSet",
      "actions": [
        { "type": "Action.InvalidActionName", "title": "Oops" }
      ]
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

TEST(AdaptiveCardsActionSet, BindsActionSetVariables) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "ActionSet",
      "actions": [
        {
          "type": "Action.OpenUrl",
          "title": "Open ${user.name}",
          "url": "https://github.com/${user.username}"
        },
        {
          "type": "Action.ShowCard",
          "title": "Show ${user.role}",
          "card": {
            "type": "AdaptiveCard",
            "body": [
              { "type": "TextBlock", "text": "Role: ${user.role}" }
            ]
          }
        }
      ]
    }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON(
{
  "user": {
    "name": "Ignacio",
    "username": "ignacionr",
    "role": "Maintainer"
  }
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
    EXPECT_EQ(bound.body[0].type, "ActionSet");
    ASSERT_EQ(bound.body[0].actions.size(), 2U);
    EXPECT_EQ(bound.body[0].actions[0].title, "Open Ignacio");
    EXPECT_EQ(bound.body[0].actions[0].url, "https://github.com/ignacionr");
    EXPECT_EQ(bound.body[0].actions[1].title, "Show Maintainer");
    ASSERT_EQ(bound.body[0].actions[1].card.body.size(), 1U);
    EXPECT_EQ(bound.body[0].actions[1].card.body[0].text, "Role: Maintainer");

    const auto urls = renderer::collect_action_urls(bound);
    ASSERT_EQ(urls.size(), 1U);
    EXPECT_EQ(urls[0], "https://github.com/ignacionr");

    const auto lines = renderer::collect_lines(bound);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0], "Role: Maintainer");
}

TEST(AdaptiveCardsActionSet, ExpandsRepeatingActionSetOverArray) {
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "ActionSet",
      "$data": "${links}",
      "actions": [
        {
          "type": "Action.OpenUrl",
          "title": "${label}",
          "url": "${href}"
        }
      ]
    }
  ]
}
)JSON";
    const std::string ctx_json = R"JSON(
{
  "links": [
    { "label": "Docs", "href": "https://adaptivecards.io/docs" },
    { "label": "Designer", "href": "https://adaptivecards.io/designer" }
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
    EXPECT_EQ(bound.body[0].actions[0].title, "Docs");
    EXPECT_EQ(bound.body[0].actions[0].url, "https://adaptivecards.io/docs");
    EXPECT_EQ(bound.body[1].actions[0].title, "Designer");
    EXPECT_EQ(bound.body[1].actions[0].url, "https://adaptivecards.io/designer");

    const auto urls = renderer::collect_action_urls(bound);
    ASSERT_EQ(urls.size(), 2U);
    EXPECT_EQ(urls[0], "https://adaptivecards.io/docs");
    EXPECT_EQ(urls[1], "https://adaptivecards.io/designer");
}

