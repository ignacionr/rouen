#pragma once

#include <rouen_plugin_api.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace sample_plugin {

    // A minimal example of the declarative Adaptive Card plugin style:
    // no ImGui code at all, just a JSON template plus a native handler
    // for the card's actions. Registers under the "hello-adaptive"
    // schema (see plugin_entry.cpp).
    class hello_adaptive_card final : public rouen::plugin::adaptive_card_plugin {
    public:
        explicit hello_adaptive_card(std::string_view /*locator*/) {}

        [[nodiscard]] std::string card_json() const override {
            return R"JSON({
  "type": "AdaptiveCard",
  "body": [
    { "type": "TextBlock", "text": "${greeting}", "size": "large", "weight": "bolder" },
    { "type": "TextBlock", "text": "Rendered from a plugin-supplied Adaptive Card template - no ImGui code involved.", "wrap": true },
    { "type": "Input.Text", "id": "name", "title": "Your name", "placeholder": "Ada" }
  ],
  "actions": [
    { "type": "Action.Submit", "title": "Say hi" },
    { "type": "Action.OpenUrl", "title": "Rouen on GitHub", "url": "${repo_url}" }
  ]
})JSON";
        }

        [[nodiscard]] std::string context_json() const override {
            return R"JSON({
  "greeting": "Hello from the plugin's Adaptive Card!",
  "repo_url": "https://github.com/ignacionr/rouen"
})JSON";
        }

        [[nodiscard]] std::string title() const override { return "Hello Adaptive Card"; }

        [[nodiscard]] std::string uri() const override { return "hello-adaptive"; }

        void on_submit(std::string const& payload_json) override {
            std::cout << "[hello-adaptive] submit payload: " << payload_json << '\n';
        }
    };

} // namespace sample_plugin
