#pragma once

#include <array>
#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../glaze_include.hpp"

namespace rouen::helpers::adaptive_cards {

struct fact {
    std::string title;
    std::string value;
};

struct element {
    std::string type;
    std::string id;
    std::string title;
    std::string text;
    std::string value;
    std::string placeholder;
    std::string data;
    std::string size;
    std::string color;
    std::string weight;
    std::vector<element> items;
    std::vector<element> columns;
    std::vector<fact> facts;
};

struct show_card {
    std::string type;
    std::vector<element> body;
};

struct action {
    std::string type;
    std::string title;
    std::string url;
    show_card card;
};

struct card_document {
    std::string type;
    std::vector<element> body;
    std::vector<action> actions;
};

struct parser_interface {
    virtual ~parser_interface() = default;
    [[nodiscard]] virtual card_document parse(std::string_view json) const = 0;
};

class parser final : public parser_interface {
public:
    [[nodiscard]] card_document parse(std::string_view json) const override {
        card_document card{};
        std::string payload = normalize_keys(json);
        const auto err = glz::read_json(card, payload);
        if (err) {
            throw std::runtime_error(glz::format_error(err, payload));
        }

        if (card.type != "AdaptiveCard") {
            throw std::runtime_error("AdaptiveCard parser expects top-level type 'AdaptiveCard'");
        }

        validate_elements(card.body);
        validate_actions(card.actions);

        return card;
    }

private:
    [[nodiscard]] static std::string normalize_keys(std::string_view input) {
        std::string output(input);
        std::size_t pos = 0;
        while ((pos = output.find("\"$data\"", pos)) != std::string::npos) {
            output.replace(pos, 7, "\"data\"");
            pos += 6;
        }
        return output;
    }

    [[nodiscard]] static bool is_supported_type(const std::string& type) {
        static constexpr std::array<std::string_view, 7> supported{
            "TextBlock", "Container", "ColumnSet", "Column", "FactSet", "Input.Text", "Input.Toggle"
        };
        return std::ranges::find(supported, type) != supported.end();
    }

    static void validate_elements(const std::vector<element>& elements) {
        for (const auto& node : elements) {
            if (!is_supported_type(node.type)) {
                throw std::runtime_error(std::format("Round 3 does not support element type '{}'", node.type));
            }

            if (node.type == "Container" || node.type == "Column") {
                validate_elements(node.items);
            } else if (node.type == "ColumnSet") {
                for (const auto& column : node.columns) {
                    if (column.type != "Column") {
                        throw std::runtime_error("ColumnSet only supports Column children");
                    }
                }
                validate_elements(node.columns);
            }
        }
    }

    static void validate_actions(const std::vector<action>& actions) {
        for (const auto& card_action : actions) {
            if (card_action.type == "Action.OpenUrl" || card_action.type == "Action.Submit") {
                continue;
            }
            if (card_action.type == "Action.ShowCard") {
                if (card_action.card.type != "AdaptiveCard" || card_action.card.body.empty()) {
                    throw std::runtime_error("Action.ShowCard requires a nested card body");
                }
                validate_elements(card_action.card.body);
                continue;
            }
            throw std::runtime_error(std::format("Round 4 does not support action type '{}'", card_action.type));
        }
    }
};

} // namespace rouen::helpers::adaptive_cards

template <>
struct glz::meta<rouen::helpers::adaptive_cards::fact> {
    using T = rouen::helpers::adaptive_cards::fact;
    static constexpr auto values = glz::object(
        "title", &T::title,
        "value", &T::value
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::element> {
    using T = rouen::helpers::adaptive_cards::element;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "id", &T::id,
        "title", &T::title,
        "text", &T::text,
        "value", &T::value,
        "placeholder", &T::placeholder,
        "data", &T::data,
        "size", &T::size,
        "color", &T::color,
        "weight", &T::weight,
        "items", &T::items,
        "columns", &T::columns,
        "facts", &T::facts
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::action> {
    using T = rouen::helpers::adaptive_cards::action;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "title", &T::title,
        "url", &T::url,
        "card", &T::card
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::show_card> {
    using T = rouen::helpers::adaptive_cards::show_card;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "body", &T::body
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::card_document> {
    using T = rouen::helpers::adaptive_cards::card_document;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "body", &T::body,
        "actions", &T::actions
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};
