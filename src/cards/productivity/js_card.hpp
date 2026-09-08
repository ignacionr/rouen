#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/adaptive_cards/templater.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../hosts/quickjs_host.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

/**
 * Modern C++23 Dynamic JavaScript Card powered by QuickJS.
 * Evaluates JavaScript user extensions and dynamically renders Adaptive Card UI.
 */
class js_card : public card {
public:
    explicit js_card(std::string_view locator = {})
        : locator_{locator} {
        colors[0] = {0.85f, 0.42f, 0.15f, 1.0f}; // Orange theme for JS cards
        colors[1] = {0.65f, 0.30f, 0.10f, 0.75f};
        name("JavaScript Extension Card");
        width = 540.0f;

        if (!locator.empty()) {
            load_script(locator);
        } else {
            // Default demo JS card script
            script_code_ = R"(
function onRender() {
    return {
        type: "AdaptiveCard",
        version: "1.5",
        body: [
            {
                type: "TextBlock",
                text: "⚡ QuickJS Interactive Card",
                size: "Large",
                weight: "Bolder"
            },
            {
                type: "TextBlock",
                text: "This card is generated in real-time by QuickJS running inside Rouen.",
                wrap: true
            }
        ]
    };
}
)";
            eval_render();
        }
    }

    [[nodiscard]] std::string get_uri() const override {
        return locator_.empty() ? "js" : std::format("js:{}", locator_);
    }

    void handle_uri(std::string_view locator) override {
        locator_ = locator;
        load_script(locator);
    }

    bool render() override {
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "JS Error: %s", error_.c_str());
            if (ImGui::Button("Retry")) {
                eval_render();
            }
            return true;
        }

        if (bound_.body.empty()) {
            eval_render();
        }

        renderer_.render(
            bound_,
            input_state_,
            helpers::adaptive_cards::renderer::action_callbacks{
                .open_url = [](const std::string& url) {
                    static_cast<void>(rouen::platform::open_url(url));
                },
                .on_submit = [this](const std::string& payload) {
                    handle_submit(payload);
                }
            },
            helpers::adaptive_cards::render_config{
                .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
            }
        );
        return true;
    }

private:
    std::string locator_{};
    std::string script_code_{};
    std::string error_{};

    helpers::adaptive_cards::parser parser_{};
    helpers::adaptive_cards::templater templater_{};
    helpers::adaptive_cards::renderer renderer_{};
    helpers::adaptive_cards::renderer::input_state input_state_{};
    helpers::adaptive_cards::card_document bound_{};

    void load_script(std::string_view locator) {
        std::filesystem::path script_path{locator};
        if (!std::filesystem::exists(script_path)) {
            // Check scripts/ user directory
            script_path = rouen::platform::get_user_data_path() / "scripts" / locator;
        }

        if (std::filesystem::exists(script_path) && std::filesystem::is_regular_file(script_path)) {
            std::ifstream file{script_path};
            if (file) {
                script_code_.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            }
        } else {
            script_code_ = std::string(locator);
        }
        eval_render();
    }

    void eval_render() {
        error_.clear();
        auto& host = hosts::quickjs_host::instance();
        if (!script_code_.empty()) {
            static_cast<void>(host.eval_script(script_code_, locator_.empty() ? "js_card.js" : locator_.c_str()));
        }

        glz::json_t card_json = host.call_function("onRender");
        std::string json_str{};
        static_cast<void>(glz::write_json(card_json, json_str));

        if (json_str.empty() || json_str == "null" || json_str == "{}") {
            // Fallback layout if no onRender returned
            json_str = R"({
                "type": "AdaptiveCard",
                "version": "1.5",
                "body": [
                    { "type": "TextBlock", "text": "QuickJS Card", "weight": "Bolder" }
                ]
            })";
        }

        try {
            helpers::adaptive_cards::context ctx{};
            bound_ = templater_.bind(parser_.parse(json_str), ctx);
        } catch (const std::exception& ex) {
            error_ = ex.what();
        }
    }

    void handle_submit(const std::string& payload_json) {
        glz::json_t payload_obj{};
        static_cast<void>(glz::read_json(payload_obj, payload_json));
        auto& host = hosts::quickjs_host::instance();
        glz::json_t new_card_json = host.call_function("onSubmit", payload_obj);
        std::string json_str{};
        static_cast<void>(glz::write_json(new_card_json, json_str));
        if (!json_str.empty() && json_str != "null" && json_str != "{}") {
            try {
                helpers::adaptive_cards::context ctx{};
                bound_ = templater_.bind(parser_.parse(json_str), ctx);
            } catch (const std::exception& ex) {
                error_ = ex.what();
            }
        }
    }
};

} // namespace rouen::cards
