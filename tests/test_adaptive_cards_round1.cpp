#include <gtest/gtest.h>

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
    card_document card{
        .type = "AdaptiveCard",
        .body = {
            element{.type = "TextBlock", .id = "greeting", .text = "Hello ${name}"}
        }
    };

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
