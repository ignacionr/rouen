#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include "../imgui_include.hpp"
#include "../markdown_renderer.hpp"
#include "markdown.hpp"
#include "parser.hpp"

namespace rouen::helpers::adaptive_cards {

// Type alias so existing call sites (adaptive_card.hpp) continue to compile
// unchanged; the canonical definition lives in helpers::markdown_render_config.
using render_config = rouen::helpers::markdown_render_config;

struct renderer_interface {
    virtual ~renderer_interface() = default;
    virtual void render(const card_document& card) const = 0;
};

class renderer final : public renderer_interface {
public:
    struct action_callbacks {
        std::function<void(const std::string&)> open_url = [](const std::string&) {};
        std::function<void(const std::string&)> on_submit = [](const std::string&) {};
    };

    struct input_state {
        std::unordered_map<std::string, std::string> text_values{};
        std::unordered_map<std::string, bool> toggle_values{};
        std::unordered_map<std::size_t, bool> show_card_expanded{};
    };

    void render(const card_document& card) const override {
        input_state state{};
        const render_config default_config{};
        render(card, state, {}, default_config);
    }

    void render(
        const card_document& card,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config
    ) const {
        render_elements(card.body, "adaptive", state, callbacks, config);
        render_actions(card.actions, state, callbacks, config);
    }

    [[nodiscard]] static std::vector<std::string> collect_lines(const card_document& card) {
        std::vector<std::string> lines;
        collect_lines_recursive(card.body, lines);
        return lines;
    }

    [[nodiscard]] static std::vector<std::string> collect_action_urls(const card_document& card) {
        std::vector<std::string> urls;
        urls.reserve(card.actions.size());
        for (const auto& action : card.actions) {
            if (!action.url.empty()) {
                urls.push_back(action.url);
            }
        }
        return urls;
    }

    [[nodiscard]] static std::string build_submit_payload(const input_state& state) {
        glz::json_t payload = glz::json_t::object_t{};
        payload["text"] = glz::json_t::object_t{};
        payload["toggle"] = glz::json_t::object_t{};

        for (const auto& [key, value] : state.text_values) {
            payload["text"][key] = value;
        }
        for (const auto& [key, value] : state.toggle_values) {
            payload["toggle"][key] = value;
        }

        if (const auto encoded = glz::write_json(payload); encoded.has_value()) {
            return encoded.value();
        }
        return "{}";
    }

private:
    static void render_elements(
        const std::vector<element>& elements,
        const std::string& scope,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config
    ) {
        for (std::size_t index = 0; index < elements.size(); ++index) {
            const auto& node = elements[index];
            const std::string id = std::format("{}-{}-{}", scope, node.type, index);
            if (node.type == "TextBlock") {
                render_text_block(node, callbacks, config);
            } else if (node.type == "Container") {
                // Render containers inline so repeated `$data` entries don't consume all remaining space.
                ImGui::PushID(id.c_str());
                ImGui::BeginGroup();
                render_elements(node.items, id, state, callbacks, config);
                ImGui::EndGroup();
                ImGui::PopID();
            } else if (node.type == "ColumnSet") {
                render_column_set(node, id, state, callbacks, config);
            } else if (node.type == "Column") {
                render_elements(node.items, id, state, callbacks, config);
            } else if (node.type == "FactSet") {
                render_fact_set(node, id);
            } else if (node.type == "Input.Text") {
                render_input_text(node, id, state);
            } else if (node.type == "Input.Toggle") {
                render_input_toggle(node, id, state);
            }
        }
    }

    static void render_column_set(
        const element& node,
        const std::string& scope,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config
    ) {
        if (node.columns.empty()) {
            return;
        }
        ImGui::Columns(static_cast<int>(node.columns.size()), nullptr, false);
        for (std::size_t idx = 0; idx < node.columns.size(); ++idx) {
            render_elements(node.columns[idx].items, std::format("{}-column-{}", scope, idx), state, callbacks, config);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    static void render_fact_set(const element& node, const std::string& scope) {
        if (node.facts.empty()) {
            return;
        }
        if (ImGui::BeginTable(scope.c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            for (const auto& pair : node.facts) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(pair.title.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(pair.value.c_str());
            }
            ImGui::EndTable();
        }
    }

    static void render_input_text(const element& node, const std::string& scope, input_state& state) {
        const std::string key = node.id.empty() ? scope : node.id;
        auto [it, inserted] = state.text_values.try_emplace(key, node.value);
        if (inserted && it->second.empty()) {
            it->second = node.value;
        }

        std::string label = node.title.empty() ? key : node.title;
        if (!node.placeholder.empty()) {
            label = std::format("{} ({})", label, node.placeholder);
        }
        std::array<char, 1024> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", it->second.c_str());
        if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size())) {
            it->second = buffer.data();
        }
    }

    static bool parse_toggle_value(const std::string& value) {
        return value == "true" || value == "True" || value == "1" || value == "on";
    }

    static void render_input_toggle(const element& node, const std::string& scope, input_state& state) {
        const std::string key = node.id.empty() ? scope : node.id;
        auto [it, inserted] = state.toggle_values.try_emplace(key, parse_toggle_value(node.value));
        if (inserted) {
            it->second = parse_toggle_value(node.value);
        }
        const std::string label = node.title.empty() ? key : node.title;
        ImGui::Checkbox(label.c_str(), &it->second);
    }

    static ImVec4 color_for(std::string_view color_name) {
        if (color_name == "Good") return {0.25f, 0.74f, 0.37f, 1.0f};
        if (color_name == "Warning") return {0.96f, 0.70f, 0.22f, 1.0f};
        if (color_name == "Attention") return {0.90f, 0.25f, 0.20f, 1.0f};
        if (color_name == "Accent") return {0.23f, 0.55f, 0.95f, 1.0f};
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    static float weight_boost(std::string_view weight_name) {
        if (weight_name == "Bolder" || weight_name == "Bold") {
            return 1.15f;
        }
        return 1.0f;
    }

    static void render_text_block(const element& node, const action_callbacks& callbacks, const render_config& config) {
        ImVec4 color = color_for(node.color);
        const float boost = weight_boost(node.weight);
        color.x = std::min(1.0f, color.x * boost);
        color.y = std::min(1.0f, color.y * boost);
        color.z = std::min(1.0f, color.z * boost);

        const auto spans = parse_inline_markdown(node.text);
        const bool is_plain = spans.size() == 1 && spans[0].kind == span_kind::normal;

        if (node.size == "Large" || node.size == "ExtraLarge") {
            if (is_plain) {
                // Plain large text: SeparatorText gives a nice header look.
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::SeparatorText(node.text.c_str());
                ImGui::PopStyleColor();
            } else {
                // Markdown large text: render spans so bold/italic markers are visible,
                // then add a separator line below for the header visual grouping.
                rouen::helpers::render_inline_markdown(node.text, color, config, callbacks.open_url);
                ImGui::Separator();
            }
            return;
        }

        if (is_plain) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", node.text.c_str());
            ImGui::PopStyleColor();
        } else {
            // Delegate to the shared inline markdown renderer.
            rouen::helpers::render_inline_markdown(node.text, color, config, callbacks.open_url);
        }
    }

    static void collect_lines_recursive(const std::vector<element>& nodes, std::vector<std::string>& lines) {
        for (const auto& node : nodes) {
            if (node.type == "TextBlock" && !node.text.empty()) {
                lines.push_back(strip_markdown(node.text));
            } else if (node.type == "FactSet") {
                for (const auto& pair : node.facts) {
                    lines.push_back(std::format("{}: {}", pair.title, pair.value));
                }
            } else if (node.type == "Input.Text" && !node.id.empty()) {
                lines.push_back(std::format("Input.Text: {}", node.id));
            } else if (node.type == "Input.Toggle" && !node.id.empty()) {
                lines.push_back(std::format("Input.Toggle: {}", node.id));
            }
            if (!node.items.empty()) {
                collect_lines_recursive(node.items, lines);
            }
            if (!node.columns.empty()) {
                collect_lines_recursive(node.columns, lines);
            }
        }
    }

    static void render_actions(
        const std::vector<action>& actions,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config
    ) {
        if (actions.empty()) {
            return;
        }

        ImGui::Separator();
        for (std::size_t idx = 0; idx < actions.size(); ++idx) {
            const auto& card_action = actions[idx];
            const std::string label = card_action.title.empty()
                ? std::format("{}##{}", card_action.type, idx)
                : std::format("{}##{}", card_action.title, idx);
            if (ImGui::Button(label.c_str())) {
                if (card_action.type == "Action.OpenUrl" && !card_action.url.empty()) {
                    callbacks.open_url(card_action.url);
                } else if (card_action.type == "Action.Submit") {
                    callbacks.on_submit(build_submit_payload(state));
                } else if (card_action.type == "Action.ShowCard") {
                    state.show_card_expanded[idx] = !state.show_card_expanded[idx];
                }
            }
            if (idx + 1 < actions.size()) {
                ImGui::SameLine();
            }

            if (card_action.type == "Action.ShowCard" && state.show_card_expanded[idx]) {
                ImGui::Indent();
                render_elements(card_action.card.body, std::format("showcard-{}", idx), state, callbacks, config);
                ImGui::Unindent();
            }
        }
    }
};

} // namespace rouen::helpers::adaptive_cards
