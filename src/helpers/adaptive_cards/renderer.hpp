#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../texture_utils.hpp"
#include "../image_cache.hpp"
#include "../texture_helper.hpp"
#include "../imgui_include.hpp"
#include "../markdown_renderer.hpp"
#include "markdown.hpp"
#include "parser.hpp"

namespace rouen::helpers::adaptive_cards {

// Type alias so existing call sites (adaptive_card.hpp) continue to compile
// unchanged; the canonical definition lives in helpers::markdown_render_config.
using render_config = rouen::helpers::markdown_render_config;
using texture_provider_t = std::function<RouenGPUTexture*(const std::string& url, int& width, int& height)>;

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
        std::unordered_map<std::string, bool> show_card_expanded{};
        // ids of text_values entries that came from an Input.Number, so
        // collect_input_values() can emit them as JSON numbers - Input.Text
        // and Input.Date/Time share the same text_values map but are
        // genuinely strings per the spec.
        std::unordered_set<std::string> numeric_ids{};
        // whether a FactSet value that overflows the collapsed line limit
        // has been expanded to show its full text, keyed by scope+index.
        std::unordered_map<std::string, bool> fact_expanded{};
    };

    void render(const card_document& card) const override {
        input_state state{};
        const render_config default_config{};
        render(card, state, {}, default_config, {});
    }

    void render(
        const card_document& card,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config,
        texture_provider_t texture_provider = {}
    ) const {
        render_elements(card.body, "adaptive", state, callbacks, config, texture_provider);
        render_actions(card.actions, "card-actions", state, callbacks, config, texture_provider, /*show_separator=*/true);
    }

    [[nodiscard]] static std::vector<std::string> collect_lines(const card_document& card) {
        std::vector<std::string> lines;
        collect_lines_recursive(card.body, lines);
        return lines;
    }

    [[nodiscard]] static std::vector<std::string> collect_action_urls(const card_document& card) {
        std::vector<std::string> urls;
        collect_action_urls_recursive(card.body, urls);
        for (const auto& action : card.actions) {
            if (!action.url.empty()) {
                urls.push_back(action.url);
            }
            if (action.type == "Action.ShowCard" && !action.card.body.empty()) {
                collect_action_urls_recursive(action.card.body, urls);
            }
        }
        return urls;
    }

    // Per the Adaptive Cards spec, a submitted/executed action's data is a
    // flat object of every input's current value keyed by its id - not
    // grouped by input type.
    [[nodiscard]] static std::string build_submit_payload(const input_state& state) {
        if (const auto encoded = glz::write_json(collect_input_values(state)); encoded.has_value()) {
            return encoded.value();
        }
        return "{}";
    }

    // Same flat, id-keyed object as build_submit_payload(), merged with
    // the triggering action's own optional "data" property. The spec
    // describes this merge identically for Action.Submit and
    // Action.Execute ("gathers input fields, merges with optional data
    // field"), so both call this. Only an object-valued action_data has
    // keys to merge in as siblings; a string/number/null data value is
    // left out of this flat object (Action.Execute still carries it
    // separately - see render_actions()).
    [[nodiscard]] static std::string build_action_data_payload(const input_state& state, const glz::json_t& action_data) {
        glz::json_t payload = collect_input_values(state);
        if (action_data.is_object()) {
            for (const auto& [key, value] : action_data.get_object()) {
                payload[key] = value;
            }
        }
        if (const auto encoded = glz::write_json(payload); encoded.has_value()) {
            return encoded.value();
        }
        return "{}";
    }

private:
    [[nodiscard]] static glz::json_t collect_input_values(const input_state& state) {
        glz::json_t values = glz::json_t::object_t{};
        for (const auto& [key, value] : state.text_values) {
            if (state.numeric_ids.contains(key)) {
                double parsed = 0.0;
                auto const [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (ec == std::errc{} && ptr == value.data() + value.size()) {
                    values[key] = parsed;
                    continue;
                }
                // Not a complete, valid number yet (e.g. still empty or
                // being typed) - fall through and send it as a string
                // rather than dropping the key entirely.
            }
            values[key] = value;
        }
        for (const auto& [key, value] : state.toggle_values) {
            values[key] = value;
        }
        return values;
    }

    static void render_input_choice_set(const element& node, const std::string& scope, input_state& state) {
        const std::string key = node.id.empty() ? scope : node.id;
        auto [it, inserted] = state.text_values.try_emplace(key, node.value);
        if (inserted && it->second.empty() && !node.choices.empty()) {
            it->second = node.choices[0].value;
        }

        const std::string label = node.title.empty() ? key : node.title;
        ImGui::TextUnformatted(label.c_str());

        if (node.isMultiSelect) {
            for (std::size_t idx = 0; idx < node.choices.size(); ++idx) {
                const auto& choice_item = node.choices[idx];
                const std::string choice_key = std::format("{}_{}", key, choice_item.value);
                auto [toggle_it, _] = state.toggle_values.try_emplace(choice_key, false);
                ImGui::Checkbox(choice_item.title.c_str(), &toggle_it->second);
            }
        } else if (node.style == "expanded") {
            for (const auto& choice_item : node.choices) {
                bool selected = (it->second == choice_item.value);
                if (ImGui::RadioButton(choice_item.title.c_str(), selected)) {
                    it->second = choice_item.value;
                }
            }
        } else {
            std::string current_title = it->second;
            for (const auto& choice_item : node.choices) {
                if (choice_item.value == it->second) {
                    current_title = choice_item.title;
                    break;
                }
            }
            if (ImGui::BeginCombo(std::format("##{}", key).c_str(), current_title.c_str())) {
                for (const auto& choice_item : node.choices) {
                    bool is_selected = (it->second == choice_item.value);
                    if (ImGui::Selectable(choice_item.title.c_str(), is_selected)) {
                        it->second = choice_item.value;
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    static void render_input_number(const element& node, const std::string& scope, input_state& state) {
        const std::string key = node.id.empty() ? scope : node.id;
        auto [it, inserted] = state.text_values.try_emplace(key, node.value);
        state.numeric_ids.insert(key);
        const std::string label = node.title.empty() ? key : node.title;
        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", it->second.c_str());
        if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size())) {
            it->second = buffer.data();
        }
    }

    static void render_input_date_or_time(const element& node, const std::string& scope, input_state& state) {
        const std::string key = node.id.empty() ? scope : node.id;
        auto [it, inserted] = state.text_values.try_emplace(key, node.value);
        std::string label = node.title.empty() ? key : node.title;
        if (!node.placeholder.empty()) {
            label = std::format("{} ({})", label, node.placeholder);
        }
        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", it->second.c_str());
        if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size())) {
            it->second = buffer.data();
        }
    }

    static void render_media(const element& node, [[maybe_unused]] const std::string& scope, const texture_provider_t& texture_provider) {
        float width = 320.0f;
        float height = 180.0f;
        align_cursor(width, node.horizontalAlignment);

        std::string poster_url = node.poster;
        if (poster_url.empty() && !node.sources.empty()) {
            poster_url = node.sources[0].url;
        }

        int img_w = 0, img_h = 0;
        RouenGPUTexture* tex = nullptr;
        if (texture_provider && !poster_url.empty()) {
            tex = texture_provider(poster_url, img_w, img_h);
        }

        ImVec2 start_pos = ImGui::GetCursorScreenPos();
        if (tex) {
            ImGui::Image(rouen::helpers::texture_id_cast(tex), ImVec2(width, height));
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(
                start_pos, ImVec2(start_pos.x + width, start_pos.y + height),
                ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f
            );
            ImGui::GetWindowDrawList()->AddRect(
                start_pos, ImVec2(start_pos.x + width, start_pos.y + height),
                ImGui::GetColorU32(ImGuiCol_Border), 6.0f
            );
            std::string label = node.altText.empty() ? "Media Video Player" : node.altText;
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 12.0f, start_pos.y + (height * 0.5f) - 6.0f));
            ImGui::TextDisabled("%s", label.c_str());
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x, start_pos.y + height + 4.0f));
        }
    }

    static void render_image_set(const element& node, const std::string& scope, const texture_provider_t& texture_provider) {
        for (std::size_t idx = 0; idx < node.images.size(); ++idx) {
            element img = node.images[idx];
            if (img.size.empty() && !node.imageSize.empty()) {
                img.size = node.imageSize;
            }
            render_image(img, std::format("{}-img-{}", scope, idx), texture_provider);
            if (idx + 1 < node.images.size()) {
                ImGui::SameLine();
            }
        }
    }

    static void render_rich_text_block(const element& node, [[maybe_unused]] const action_callbacks& callbacks, const render_config& config) {
        for (std::size_t idx = 0; idx < node.inlines.size(); ++idx) {
            const auto& run = node.inlines[idx];
            ImVec4 color = color_for(run.color);
            const float font_scale = font_scale_for(run.size);

            if (font_scale != 1.0f) {
                ImGui::SetWindowFontScale(font_scale);
            }
            if (run.bold && config.font_bold) {
                ImGui::PushFont(config.font_bold);
            } else if (run.italic && config.font_italic) {
                ImGui::PushFont(config.font_italic);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(run.text.c_str());
            ImGui::PopStyleColor();

            if (run.bold && config.font_bold) {
                ImGui::PopFont();
            } else if (run.italic && config.font_italic) {
                ImGui::PopFont();
            }
            if (font_scale != 1.0f) {
                ImGui::SetWindowFontScale(1.0f);
            }

            if (idx + 1 < node.inlines.size()) {
                ImGui::SameLine(0.0f, 0.0f);
            }
        }
    }

    struct column_width_spec {
        ImGuiTableColumnFlags flags{ImGuiTableColumnFlags_WidthStretch};
        float weight_or_width{1.0f};
    };

    [[nodiscard]] static column_width_spec parse_column_width(std::string_view width_str) {
        if (width_str.empty() || width_str == "stretch" || width_str == "Stretch") {
            return {ImGuiTableColumnFlags_WidthStretch, 1.0f};
        }
        if (width_str == "auto" || width_str == "Auto") {
            return {ImGuiTableColumnFlags_WidthFixed, 0.0f};
        }
        if (width_str.ends_with("px") || width_str.ends_with("PX")) {
            try {
                float px = std::stof(std::string(width_str.substr(0, width_str.size() - 2)));
                return {ImGuiTableColumnFlags_WidthFixed, std::max(0.0f, px)};
            } catch (...) {
                return {ImGuiTableColumnFlags_WidthStretch, 1.0f};
            }
        }
        try {
            float val = std::stof(std::string(width_str));
            return {ImGuiTableColumnFlags_WidthStretch, std::max(0.1f, val)};
        } catch (...) {
            return {ImGuiTableColumnFlags_WidthStretch, 1.0f};
        }
    }

    static void render_table(
        const element& node,
        const std::string& scope,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config,
        const texture_provider_t& texture_provider
    ) {
        std::size_t num_cols = node.columns.size();
        if (num_cols == 0 && !node.rows.empty()) {
            num_cols = node.rows[0].cells.size();
        }
        if (num_cols == 0) return;

        ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg
                                    | ImGuiTableFlags_BordersInnerH
                                    | ImGuiTableFlags_BordersInnerV
                                    | ImGuiTableFlags_BordersOuter
                                    | ImGuiTableFlags_Resizable
                                    | ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable(scope.c_str(), static_cast<int>(num_cols), table_flags)) {
            for (std::size_t c_idx = 0; c_idx < num_cols; ++c_idx) {
                std::string col_width;
                if (c_idx < node.columns.size()) {
                    col_width = node.columns[c_idx].width;
                }
                const auto spec = parse_column_width(col_width);
                ImGui::TableSetupColumn(nullptr, spec.flags, spec.weight_or_width);
            }

            for (std::size_t r_idx = 0; r_idx < node.rows.size(); ++r_idx) {
                ImGui::TableNextRow();
                const auto& row = node.rows[r_idx];
                for (std::size_t c_idx = 0; c_idx < row.cells.size() && c_idx < num_cols; ++c_idx) {
                    ImGui::TableSetColumnIndex(static_cast<int>(c_idx));
                    render_elements(row.cells[c_idx].items, std::format("{}-r{}-c{}", scope, r_idx, c_idx), state, callbacks, config, texture_provider);
                }
            }
            ImGui::EndTable();
        }
    }

    static void render_elements(
        const std::vector<element>& elements,
        const std::string& scope,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config,
        const texture_provider_t& texture_provider = {}
    ) {
        for (std::size_t index = 0; index < elements.size(); ++index) {
            const auto& node = elements[index];
            const std::string id = std::format("{}-{}-{}", scope, node.type, index);

            if (node.separator) {
                ImGui::Separator();
            }
            if (node.spacing == "large" || node.spacing == "extraLarge") {
                ImGui::Spacing();
                ImGui::Spacing();
            } else if (node.spacing == "medium" || node.spacing == "default") {
                ImGui::Spacing();
            }

            if (node.type == "TextBlock") {
                render_text_block(node, callbacks, config);
            } else if (node.type == "Image") {
                render_image(node, id, texture_provider);
            } else if (node.type == "Container") {
                ImGui::PushID(id.c_str());
                if (node.style == "emphasis" || node.style == "accent") {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg));
                    ImGui::BeginChild(id.c_str(), ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                } else {
                    ImGui::BeginGroup();
                }

                render_elements(node.items, id, state, callbacks, config, texture_provider);

                if (node.style == "emphasis" || node.style == "accent") {
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                } else {
                    ImGui::EndGroup();
                }

                if (!node.selectAction.type.empty() && ImGui::IsItemClicked()) {
                    if (node.selectAction.type == "Action.OpenUrl" && !node.selectAction.url.empty()) {
                        callbacks.open_url(node.selectAction.url);
                    }
                }
                ImGui::PopID();
            } else if (node.type == "ColumnSet") {
                render_column_set(node, id, state, callbacks, config, texture_provider);
            } else if (node.type == "Column") {
                render_elements(node.items, id, state, callbacks, config, texture_provider);
            } else if (node.type == "FactSet") {
                render_fact_set(node, id, state);
            } else if (node.type == "Input.Text") {
                render_input_text(node, id, state);
            } else if (node.type == "Input.Toggle") {
                render_input_toggle(node, id, state);
            } else if (node.type == "Input.ChoiceSet") {
                render_input_choice_set(node, id, state);
            } else if (node.type == "Input.Number") {
                render_input_number(node, id, state);
            } else if (node.type == "Input.Date" || node.type == "Input.Time") {
                render_input_date_or_time(node, id, state);
            } else if (node.type == "Media") {
                render_media(node, id, texture_provider);
            } else if (node.type == "ImageSet") {
                render_image_set(node, id, texture_provider);
            } else if (node.type == "RichTextBlock") {
                render_rich_text_block(node, callbacks, config);
            } else if (node.type == "Table") {
                render_table(node, id, state, callbacks, config, texture_provider);
            } else if (node.type == "ActionSet") {
                render_actions(node.actions, id, state, callbacks, config, texture_provider, /*show_separator=*/false);
            }
        }
    }

    static void render_column_set(
        const element& node,
        const std::string& scope,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config,
        const texture_provider_t& texture_provider = {}
    ) {
        if (node.columns.empty()) {
            return;
        }
        ImGui::Columns(static_cast<int>(node.columns.size()), nullptr, false);
        for (std::size_t idx = 0; idx < node.columns.size(); ++idx) {
            render_elements(node.columns[idx].items, std::format("{}-column-{}", scope, idx), state, callbacks, config, texture_provider);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    static void align_cursor(float item_width, std::string_view alignment) {
        if (alignment.empty() || alignment == "Left" || alignment == "left") {
            return;
        }
        float avail_w = ImGui::GetContentRegionAvail().x;
        if (item_width >= avail_w || avail_w <= 0.0f) {
            return;
        }
        if (alignment == "Center" || alignment == "center") {
            float offset = (avail_w - item_width) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
        } else if (alignment == "Right" || alignment == "right") {
            float offset = avail_w - item_width;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
        }
    }

    static void render_image(const element& node, [[maybe_unused]] const std::string& scope, const texture_provider_t& texture_provider) {
        std::string img_url = node.url;
        if (img_url.empty()) img_url = node.data;
        if (img_url.empty()) return;

        int img_w = 0, img_h = 0;
        RouenGPUTexture* tex = nullptr;
        if (texture_provider) {
            tex = texture_provider(img_url, img_w, img_h);
        }

        float display_w = 160.0f;
        if (node.size == "Small" || node.size == "small") display_w = 80.0f;
        else if (node.size == "Medium" || node.size == "medium") display_w = 160.0f;
        else if (node.size == "Large" || node.size == "large") display_w = 280.0f;
        else if (node.size == "Auto" || node.size == "Stretch" || node.size == "auto" || node.size == "stretch") {
            float avail_w = ImGui::GetContentRegionAvail().x;
            display_w = (img_w > 0) ? std::min(avail_w, static_cast<float>(img_w)) : std::min(avail_w, 400.0f);
        }

        float display_h = display_w;
        if (img_w > 0 && img_h > 0) {
            display_h = display_w * (static_cast<float>(img_h) / static_cast<float>(img_w));
        }

        align_cursor(display_w, node.horizontalAlignment);

        if (tex) {
            ImGui::Image(rouen::helpers::texture_id_cast(tex), ImVec2(display_w, display_h));
        } else {
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            float fill_h = std::clamp(display_h, 40.0f, 180.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                start_pos,
                ImVec2(start_pos.x + display_w, start_pos.y + fill_h),
                ImGui::GetColorU32(ImGuiCol_FrameBg),
                6.0f
            );
            ImGui::GetWindowDrawList()->AddRect(
                start_pos,
                ImVec2(start_pos.x + display_w, start_pos.y + fill_h),
                ImGui::GetColorU32(ImGuiCol_Border),
                6.0f
            );
            std::string label = node.altText.empty() ? "Loading image..." : node.altText;
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 8.0f, start_pos.y + (fill_h * 0.5f) - 6.0f));
            ImGui::TextDisabled("%s", label.c_str());
            ImGui::SetCursorScreenPos(ImVec2(start_pos.x, start_pos.y + fill_h + 4.0f));
        }
    }

    // Facts with values that wrap past this many lines get truncated, with
    // the would-be next line replaced by a toggle button so long values
    // don't dominate an otherwise simple card.
    static constexpr int kFactValueCollapsedLines = 3;

    [[nodiscard]] static int wrapped_line_count(const std::string& text, float wrap_width) {
        const float line_height = ImGui::GetTextLineHeight();
        if (text.empty() || line_height <= 0.0f) {
            return text.empty() ? 0 : 1;
        }
        const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap_width);
        return std::max(1, static_cast<int>(std::round(size.y / line_height)));
    }

    // Returns the prefix of text that wraps to at most max_lines lines at
    // wrap_width, so a truncated preview can be rendered with TextWrapped.
    [[nodiscard]] static std::string wrapped_line_prefix(const std::string& text, float wrap_width, int max_lines) {
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        const char* begin = text.c_str();
        const char* end = begin + text.size();
        const char* cursor = begin;
        for (int line = 0; line < max_lines && cursor < end; ++line) {
            const char* wrap_pos = font->CalcWordWrapPositionA(font_size, cursor, end, wrap_width);
            if (wrap_pos <= cursor) {
                wrap_pos = cursor + 1;
            }
            cursor = wrap_pos;
        }
        return std::string(begin, cursor);
    }

    static void render_fact_set(const element& node, const std::string& scope, input_state& state) {
        if (node.facts.empty()) {
            return;
        }
        if (ImGui::BeginTable(scope.c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            for (std::size_t idx = 0; idx < node.facts.size(); ++idx) {
                const auto& pair = node.facts[idx];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(pair.title.c_str());
                ImGui::TableSetColumnIndex(1);

                const float wrap_width = ImGui::GetContentRegionAvail().x;
                if (wrapped_line_count(pair.value, wrap_width) <= kFactValueCollapsedLines) {
                    ImGui::TextWrapped("%s", pair.value.c_str());
                    continue;
                }

                const std::string fact_key = std::format("{}-fact-{}", scope, idx);
                auto [it, _] = state.fact_expanded.try_emplace(fact_key, false);
                if (it->second) {
                    ImGui::TextWrapped("%s", pair.value.c_str());
                    if (ImGui::SmallButton(std::format("Show less##{}", fact_key).c_str())) {
                        it->second = false;
                    }
                } else {
                    const std::string preview = wrapped_line_prefix(pair.value, wrap_width, kFactValueCollapsedLines);
                    ImGui::TextWrapped("%s", preview.c_str());
                    if (ImGui::SmallButton(std::format("...##{}", fact_key).c_str())) {
                        it->second = true;
                    }
                }
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
        if (color_name == "Good" || color_name == "good") return {0.25f, 0.74f, 0.37f, 1.0f};
        if (color_name == "Warning" || color_name == "warning") return {0.96f, 0.70f, 0.22f, 1.0f};
        if (color_name == "Attention" || color_name == "attention") return {0.90f, 0.25f, 0.20f, 1.0f};
        if (color_name == "Accent" || color_name == "accent") return {0.23f, 0.55f, 0.95f, 1.0f};
        if (color_name == "Dark" || color_name == "dark") return {0.20f, 0.20f, 0.20f, 1.0f};
        if (color_name == "Light" || color_name == "light") return {0.90f, 0.90f, 0.90f, 1.0f};
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    static float weight_boost(std::string_view weight_name) {
        if (weight_name == "Bolder" || weight_name == "Bold" || weight_name == "bolder" || weight_name == "bold") {
            return 1.15f;
        }
        return 1.0f;
    }

    static float font_scale_for(std::string_view size_name) {
        if (size_name.empty() || size_name == "Default" || size_name == "default") {
            return 1.0f;
        }
        if (size_name == "Small" || size_name == "small") {
            return 0.82f;
        }
        if (size_name == "Medium" || size_name == "medium") {
            return 1.15f;
        }
        if (size_name == "Large" || size_name == "large") {
            return 1.35f;
        }
        if (size_name == "ExtraLarge" || size_name == "extralarge" || size_name == "extraLarge") {
            return 1.65f;
        }
        return 1.0f;
    }

    static void render_text_block(const element& node, const action_callbacks& callbacks, const render_config& config) {
        ImVec4 color = color_for(node.color);
        const float boost = weight_boost(node.weight);
        color.x = std::min(1.0f, color.x * boost);
        color.y = std::min(1.0f, color.y * boost);
        color.z = std::min(1.0f, color.z * boost);

        const float font_scale = font_scale_for(node.size);
        const bool apply_font_scale = (font_scale != 1.0f);
        const bool is_bold = (node.weight == "Bolder" || node.weight == "Bold" || node.weight == "bolder" || node.weight == "bold");

        if (apply_font_scale) {
            ImGui::SetWindowFontScale(font_scale);
        }
        if (is_bold && config.font_bold) {
            ImGui::PushFont(config.font_bold);
        }

        if (!node.horizontalAlignment.empty() && node.horizontalAlignment != "Left" && node.horizontalAlignment != "left") {
            const std::string plain_text = strip_markdown(node.text);
            float text_width = ImGui::CalcTextSize(plain_text.c_str()).x * font_scale;
            align_cursor(text_width, node.horizontalAlignment);
        }

        // Delegate to the shared inline markdown renderer.
        rouen::helpers::render_inline_markdown(node.text, color, config, callbacks.open_url);

        if (is_bold && config.font_bold) {
            ImGui::PopFont();
        }
        if (apply_font_scale) {
            ImGui::SetWindowFontScale(1.0f);
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
            for (const auto& row : node.rows) {
                for (const auto& cell : row.cells) {
                    collect_lines_recursive(cell.items, lines);
                }
            }
            for (const auto& act : node.actions) {
                if (act.type == "Action.ShowCard" && !act.card.body.empty()) {
                    collect_lines_recursive(act.card.body, lines);
                }
            }
        }
    }

    static void collect_action_urls_recursive(const std::vector<element>& nodes, std::vector<std::string>& urls) {
        for (const auto& node : nodes) {
            if (!node.selectAction.url.empty()) {
                urls.push_back(node.selectAction.url);
            }
            for (const auto& act : node.actions) {
                if (!act.url.empty()) {
                    urls.push_back(act.url);
                }
                if (act.type == "Action.ShowCard" && !act.card.body.empty()) {
                    collect_action_urls_recursive(act.card.body, urls);
                }
            }
            if (!node.items.empty()) {
                collect_action_urls_recursive(node.items, urls);
            }
            if (!node.columns.empty()) {
                collect_action_urls_recursive(node.columns, urls);
            }
            for (const auto& row : node.rows) {
                for (const auto& cell : row.cells) {
                    collect_action_urls_recursive(cell.items, urls);
                }
            }
        }
    }

    static void render_actions(
        const std::vector<action>& actions,
        const std::string& scope,
        input_state& state,
        const action_callbacks& callbacks,
        const render_config& config,
        const texture_provider_t& texture_provider = {},
        bool show_separator = true
    ) {
        if (actions.empty()) {
            return;
        }

        if (show_separator) {
            ImGui::Separator();
        }
        for (std::size_t idx = 0; idx < actions.size(); ++idx) {
            const auto& card_action = actions[idx];
            const std::string action_id = std::format("{}-act-{}", scope, idx);
            const std::string label = card_action.title.empty()
                ? std::format("{}##{}", card_action.type, action_id)
                : std::format("{}##{}", card_action.title, action_id);
            if (ImGui::Button(label.c_str())) {
                if (card_action.type == "Action.OpenUrl" && !card_action.url.empty()) {
                    callbacks.open_url(card_action.url);
                } else if (card_action.type == "Action.Submit") {
                    callbacks.on_submit(build_action_data_payload(state, card_action.data));
                } else if (card_action.type == "Action.Execute") {
                    callbacks.on_submit(std::format(
                        "{{\"verb\":\"{}\",\"data\":{}}}", card_action.verb,
                        build_action_data_payload(state, card_action.data)));
                } else if (card_action.type == "Action.ToggleVisibility") {
                    for (const auto& target_id : card_action.targetElements) {
                        state.toggle_values[target_id] = !state.toggle_values[target_id];
                    }
                } else if (card_action.type == "Action.ShowCard") {
                    state.show_card_expanded[action_id] = !state.show_card_expanded[action_id];
                }
            }
            if (idx + 1 < actions.size()) {
                ImGui::SameLine();
            }

            if (card_action.type == "Action.ShowCard" && state.show_card_expanded[action_id]) {
                ImGui::Indent();
                render_elements(card_action.card.body, std::format("{}-showcard-{}", scope, idx), state, callbacks, config, texture_provider);
                ImGui::Unindent();
            }
        }
    }
};

} // namespace rouen::helpers::adaptive_cards
