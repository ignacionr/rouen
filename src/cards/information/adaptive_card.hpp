#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/adaptive_cards/templater.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class adaptive_card : public card {
public:
    struct preset {
        std::string name;
        std::string card_file;
        std::string context_file;
    };

    explicit adaptive_card(std::string_view locator = {})
        : locator_(locator) {
        colors[0] = {0.20f, 0.43f, 0.70f, 1.0f};
        colors[1] = {0.14f, 0.32f, 0.55f, 0.75f};
        name("Adaptive Card");
        width = 520.0f;
        presets_ = default_presets();
        load_from_locator();
    }

    std::string get_uri() const override {
        if (locator_.empty()) {
            return "adaptive-card";
        }
        return std::format("adaptive-card:{}", locator_);
    }

    bool render() override {
        return render_window([this]() {
            if (!error_.empty()) {
                ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, "%s", error_.c_str());
                ImGui::Separator();
                ImGui::TextWrapped(
                    "Use URI format: adaptive-card:<card_json_path>|<context_json_path>"
                );
                return;
            }
            render_preset_selector();
            if (ImGui::BeginTabBar("AdaptiveCardTabs")) {
                if (ImGui::BeginTabItem("Rendered")) {
                    renderer_.render(bound_, input_state_, {
                        .open_url = [this](const std::string& url) {
                            last_opened_url_ = url;
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            last_submit_payload_ = payload;
                        }
                    });
                    if (!last_opened_url_.empty()) {
                        ImGui::Separator();
                        ImGui::TextWrapped("Last opened URL: %s", last_opened_url_.c_str());
                    }
                    if (!last_submit_payload_.empty()) {
                        ImGui::TextWrapped("Last submit payload: %s", last_submit_payload_.c_str());
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("JSON")) {
                    render_json_block("Card JSON", card_json_source_);
                    render_json_block("Context JSON", context_json_source_.empty() ? "{}" : context_json_source_);
                    render_json_block("Bound JSON", bound_json_.empty() ? "{}" : bound_json_);
                    render_json_block("Input State JSON", input_state_json());
                    render_json_block("Last Submit JSON", last_submit_payload_.empty() ? "{}" : last_submit_payload_);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        });
    }

private:
    static constexpr std::string_view default_card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "body": [
    {
      "type": "TextBlock",
      "id": "greeting",
      "text": "Hello ${name}"
    }
  ]
}
)JSON";

    static constexpr std::string_view default_context_json = R"JSON(
{
  "name": "Rouen"
}
)JSON";

    static std::string trim(const std::string& input) {
        const auto first = input.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            return {};
        }
        const auto last = input.find_last_not_of(" \t\n\r");
        return input.substr(first, last - first + 1);
    }

    static std::string read_file_or_throw(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error(std::format("Unable to open file: {}", path.string()));
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    [[nodiscard]] std::string input_state_json() const {
        glz::json_t state = glz::json_t::object_t{};
        state["text"] = glz::json_t::object_t{};
        state["toggle"] = glz::json_t::object_t{};

        for (const auto& [key, value] : input_state_.text_values) {
            state["text"][key] = value;
        }
        for (const auto& [key, value] : input_state_.toggle_values) {
            state["toggle"][key] = value;
        }

        std::string encoded{"{}"};
        if (const auto result = glz::write_json(state); result.has_value()) {
            encoded = result.value();
        }
        return encoded;
    }

    static void render_json_block(const char* label, const std::string& json) {
        ImGui::TextUnformatted(label);
        ImGui::BeginChild(label, ImVec2(0.0f, 140.0f), true);
        ImGui::TextUnformatted(json.c_str());
        ImGui::EndChild();
        ImGui::Spacing();
    }

    static void parse_locator(
        const std::string& locator,
        std::string& card_path,
        std::string& context_path
    ) {
        const std::size_t separator = locator.find('|');
        if (separator == std::string::npos) {
            card_path = trim(locator);
            context_path.clear();
            return;
        }

        card_path = trim(locator.substr(0, separator));
        context_path = trim(locator.substr(separator + 1));
    }

    [[nodiscard]] static std::vector<preset> default_presets() {
        return {
            {"Round 1 - Text + Flat Binding", "round1_card.json", "round1_context.json"},
            {"Round 2 - Layouts + Nested Binding", "round2_card.json", "round2_context.json"},
            {"Round 3 - Inputs + OpenUrl", "round3_card.json", "round3_context.json"},
            {"Round 4 - Repeat + Submit + ShowCard", "round4_card.json", "round4_context.json"}
        };
    }

    void render_preset_selector() {
        if (!locator_.empty() || presets_.empty()) {
            return;
        }
        const auto& current = presets_.at(selected_preset_index_);
        if (ImGui::BeginCombo("Adaptive Card Test Set", current.name.c_str())) {
            for (std::size_t index = 0; index < presets_.size(); ++index) {
                const bool is_selected = (index == selected_preset_index_);
                if (ImGui::Selectable(presets_[index].name.c_str(), is_selected)) {
                    selected_preset_index_ = index;
                    try {
                        load_selected_preset();
                        error_.clear();
                    } catch (const std::exception& exception) {
                        error_ = std::format("Adaptive Card error: {}", exception.what());
                    }
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (!preset_card_path_.empty()) {
            ImGui::TextWrapped("Preset card: %s", preset_card_path_.c_str());
        }
    }

    void load_selected_preset() {
        if (selected_preset_index_ >= presets_.size()) {
            return;
        }
        const auto& selected = presets_[selected_preset_index_];
        const auto card_path = rouen::platform::get_resource_path(selected.card_file, "adaptive_cards");
        const auto context_path = rouen::platform::get_resource_path(selected.context_file, "adaptive_cards");
        preset_card_path_ = card_path.string();
        preset_context_path_ = context_path.string();
        load_from_paths(card_path, context_path);
    }

    void load_from_paths(const std::filesystem::path& card_path, const std::filesystem::path& context_path) {
        const std::string card_json = read_file_or_throw(card_path);
        const std::string context_json = read_file_or_throw(context_path);
        load_from_json(card_json, context_json);
    }

    void load_from_json(const std::string& card_json, const std::string& context_json) {
        auto parsed = parser_.parse(card_json);

        helpers::adaptive_cards::context data{};
        if (!context_json.empty()) {
            const auto context_err = glz::read_json(data, context_json);
            if (context_err) {
                throw std::runtime_error(glz::format_error(context_err, context_json));
            }
        }

        bound_ = templater_.bind(parsed, data);
        input_state_ = {};
        error_.clear();
        last_opened_url_.clear();
        last_submit_payload_.clear();
        card_json_source_ = card_json;
        context_json_source_ = context_json;
        if (const auto encoded = glz::write_json(bound_); encoded.has_value()) {
            bound_json_ = encoded.value();
        } else {
            bound_json_.clear();
        }
    }

    void load_from_locator() {
        try {
            if (locator_.empty()) {
                try {
                    load_selected_preset();
                    if (!bound_.body.empty() || !bound_.actions.empty()) {
                        return;
                    }
                } catch (...) {
                    preset_card_path_.clear();
                    preset_context_path_.clear();
                }
                load_from_json(std::string(default_card_json), std::string(default_context_json));
            } else {
                std::string card_path{};
                std::string context_path{};
                parse_locator(locator_, card_path, context_path);
                if (card_path.empty()) {
                    throw std::runtime_error("Adaptive card JSON path cannot be empty");
                }

                const std::string card_json = read_file_or_throw(card_path);
                const std::string context_json = context_path.empty()
                    ? std::string("{}")
                    : read_file_or_throw(context_path);
                load_from_json(card_json, context_json);
            }
        } catch (const std::exception& exception) {
            error_ = std::format("Adaptive Card error: {}", exception.what());
        }
    }

    std::string locator_;
    std::string error_;
    helpers::adaptive_cards::parser parser_{};
    helpers::adaptive_cards::templater templater_{};
    helpers::adaptive_cards::renderer renderer_{};
    helpers::adaptive_cards::renderer::input_state input_state_{};
    helpers::adaptive_cards::card_document bound_{};
    std::vector<preset> presets_{};
    std::size_t selected_preset_index_{0};
    std::string preset_card_path_{};
    std::string preset_context_path_{};
    std::string last_opened_url_{};
    std::string last_submit_payload_{};
    std::string card_json_source_{};
    std::string context_json_source_{};
    std::string bound_json_{};
};

} // namespace rouen::cards
