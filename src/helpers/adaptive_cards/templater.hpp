#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parser.hpp"

namespace rouen::helpers::adaptive_cards {

using context = glz::json_t;

struct templater_interface {
    virtual ~templater_interface() = default;
    [[nodiscard]] virtual card_document bind(const card_document& card, const context& values) const = 0;
};

class templater final : public templater_interface {
public:
    [[nodiscard]] card_document bind(const card_document& card, const context& values) const override {
        card_document result = card;
        bind_elements(result.body, values, nullptr);
        for (auto& action : result.actions) {
            action.title = expand(action.title, values, nullptr);
            action.url = expand(action.url, values, nullptr);
            bind_elements(action.card.body, values, nullptr);
        }
        return result;
    }

private:
    [[nodiscard]] static std::string normalize_path(std::string_view path) {
        if (path.size() >= 3 && path.starts_with("${") && path.ends_with('}')) {
            return std::string(path.substr(2, path.size() - 3));
        }
        return std::string(path);
    }

    [[nodiscard]] static const context* resolve_node(std::string_view path, const context& source) {
        const context* cursor = &source;
        std::size_t begin = 0;
        while (begin <= path.size()) {
            const std::size_t end = path.find('.', begin);
            const std::string token(path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin));
            if (token.empty() || !cursor->is_object()) {
                return nullptr;
            }
            const auto& object = cursor->get_object();
            const auto found = object.find(token);
            if (found == object.end()) {
                return nullptr;
            }
            cursor = &found->second;
            if (end == std::string_view::npos) {
                return cursor;
            }
            begin = end + 1;
        }
        return cursor;
    }

    [[nodiscard]] static const context* resolve_node(
        std::string_view path,
        const context& root,
        const context* local
    ) {
        const std::string normalized = normalize_path(path);
        if (local != nullptr) {
            if (const auto* local_resolved = resolve_node(normalized, *local); local_resolved != nullptr) {
                return local_resolved;
            }
        }
        return resolve_node(normalized, root);
    }

    static void bind_single_element(element& node, const context& root, const context* local) {
        node.text = expand(node.text, root, local);
        node.id = expand(node.id, root, local);
        node.title = expand(node.title, root, local);
        node.value = expand(node.value, root, local);
        node.placeholder = expand(node.placeholder, root, local);
        node.width = expand(node.width, root, local);
        for (auto& pair : node.facts) {
            pair.title = expand(pair.title, root, local);
            pair.value = expand(pair.value, root, local);
        }
        for (auto& act : node.actions) {
            act.title = expand(act.title, root, local);
            act.url = expand(act.url, root, local);
            if (!act.card.body.empty()) {
                bind_elements(act.card.body, root, local);
            }
        }
        if (!node.selectAction.type.empty()) {
            node.selectAction.title = expand(node.selectAction.title, root, local);
            node.selectAction.url = expand(node.selectAction.url, root, local);
            if (!node.selectAction.card.body.empty()) {
                bind_elements(node.selectAction.card.body, root, local);
            }
        }

        if (!node.items.empty()) {
            bind_elements(node.items, root, local);
        }
        if (!node.columns.empty()) {
            bind_elements(node.columns, root, local);
        }
        for (auto& row : node.rows) {
            for (auto& cell : row.cells) {
                bind_elements(cell.items, root, local);
            }
        }
    }

    static void bind_elements(std::vector<element>& nodes, const context& root, const context* local) {
        std::vector<element> expanded;
        expanded.reserve(nodes.size());

        for (auto node : nodes) {
            if (!node.data.empty()) {
                if (const auto* values = resolve_node(node.data, root, local);
                    values != nullptr && values->is_array()) {
                    const auto& arr = values->get_array();
                    for (const auto& entry : arr) {
                        element clone = node;
                        clone.data.clear();
                        bind_single_element(clone, root, &entry);
                        expanded.push_back(std::move(clone));
                    }
                }
                continue;
            }

            bind_single_element(node, root, local);
            expanded.push_back(std::move(node));
        }

        nodes = std::move(expanded);
    }

    [[nodiscard]] static std::optional<std::string> resolve_path(
        std::string_view path,
        const context& root,
        const context* local
    ) {
        const context* cursor = resolve_node(path, root, local);
        if (cursor == nullptr) {
            return std::nullopt;
        }
        if (cursor->is_null()) {
            return std::string{};
        }
        if (cursor->is_string()) {
            return cursor->get<std::string>();
        }
        std::string encoded;
        static_cast<void>(glz::write_json(*cursor, encoded));
        if (cursor->is_boolean() || cursor->is_number()) {
            return encoded;
        }
        return encoded;
    }

    [[nodiscard]] static std::string expand(const std::string& input, const context& root, const context* local) {
        std::string output;
        output.reserve(input.size());

        std::size_t cursor = 0;
        while (cursor < input.size()) {
            const std::size_t open = input.find("${", cursor);
            if (open == std::string::npos) {
                output.append(input.substr(cursor));
                break;
            }

            output.append(input.substr(cursor, open - cursor));
            const std::size_t close = input.find('}', open + 2);
            if (close == std::string::npos) {
                output.append(input.substr(open));
                break;
            }

            const std::string key = input.substr(open + 2, close - (open + 2));
            if (const auto resolved = resolve_path(key, root, local); resolved.has_value()) {
                output.append(*resolved);
            }

            cursor = close + 1;
        }

        return output;
    }
};

} // namespace rouen::helpers::adaptive_cards
