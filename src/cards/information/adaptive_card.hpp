#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/adaptive_cards/templater.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../models/adaptive_cards/adaptive_cards_repository.hpp"
#include "../interface/card.hpp"
#include "IconsMaterialDesign.h"

namespace rouen::cards {

class adaptive_card : public card {
public:
    using card_record = models::adaptive_cards::adaptive_card_record;

    struct preset {
        std::string name;
        std::string card_file;
        std::string context_file;
    };

    explicit adaptive_card(std::string_view locator = {})
        : locator_(locator), repository_{} {
        colors[0] = {0.20f, 0.43f, 0.70f, 1.0f};
        colors[1] = {0.14f, 0.32f, 0.55f, 0.75f};
        name("Adaptive Card");
        width = 540.0f;
        const auto db_path = rouen::platform::get_user_data_path("adaptive_cards.db").string();
        const auto cache_dir = (rouen::platform::get_user_data_path() / "images").string();
        image_cache_ = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
        presets_ = default_presets();
        refresh_saved_cards();

        if (!locator.empty() && locator != "adaptive-card") {
            handle_uri(locator.starts_with("adaptive-card:") ? locator : std::format("adaptive-card:{}", locator));
        } else if (!saved_cards_.empty()) {
            selected_card_index_ = 0;
            current_card_ = saved_cards_[0];
            try {
                load_from_json(current_card_.card_json, current_card_.context_json);
                error_.clear();
            } catch (const std::exception& ex) {
                error_ = std::format("Adaptive Card error: {}", ex.what());
            }
            name(current_card_.title.empty() ? "Adaptive Card" : current_card_.title);
        } else {
            load_from_locator();
        }
    }

    std::string get_uri() const override {
        if (!current_card_.name.empty()) {
            return std::format("adaptive-card:{}", current_card_.name);
        }
        if (locator_.empty()) {
            return "adaptive-card";
        }
        return std::format("adaptive-card:{}", locator_);
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "adaptive-card" || uri.starts_with("adaptive-card:");
    }

    void handle_uri(std::string_view uri) override {
        std::string target;
        if (uri.starts_with("adaptive-card:")) {
            target = models::adaptive_cards::adaptive_cards_repository::trim(uri.substr(14));
        } else {
            target = models::adaptive_cards::adaptive_cards_repository::trim(uri);
        }

        if (target.empty() || target == "adaptive-card") {
            refresh_saved_cards();
            if (!saved_cards_.empty()) {
                select_card_by_name(saved_cards_[0].name);
            } else {
                try {
                    load_selected_preset();
                } catch (...) {
                    load_from_json(std::string(default_card_json), std::string(default_context_json));
                }
            }
            return;
        }

        if (target.starts_with('{')) {
            // Raw JSON string passed via URI
            try {
                load_from_json(target, "{}");
                current_card_ = card_record{0, "custom-card", "Custom Adaptive Card", target, "{}", "", ""};
                name("Custom Adaptive Card");
            } catch (const std::exception& ex) {
                error_ = std::format("Adaptive Card error: {}", ex.what());
            }
            return;
        }

        // Try loading from repository by name
        auto existing = repository_.get_card_by_name(target);
        if (existing.has_value()) {
            refresh_saved_cards();
            select_card_by_name(existing->name);
            return;
        }

        // Fallback to locator path parsing (e.g. card_path|context_path)
        locator_ = target;
        load_from_locator();
    }

    bool render() override {
        return render_window([this]() {
            // Action Toolbar: New Card, Save Card, Delete Card
            if (ImGui::Button("New Card")) {
                create_new_card();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Card")) {
                save_current_card();
            }
            ImGui::SameLine();

            bool can_delete = !current_card_.name.empty() && repository_.get_card_by_name(current_card_.name).has_value();
            if (!can_delete) ImGui::BeginDisabled();
            if (ImGui::Button("Delete Card")) {
                if (!current_card_.name.empty()) {
                    repository_.delete_card(current_card_.name);
                    refresh_saved_cards();
                    if (!saved_cards_.empty()) {
                        selected_card_index_ = 0;
                        current_card_ = saved_cards_[0];
                        load_from_json(current_card_.card_json, current_card_.context_json);
                        name(current_card_.title);
                    } else {
                        current_card_ = {};
                        load_from_json(std::string(default_card_json), std::string(default_context_json));
                    }
                }
            }
            if (!can_delete) ImGui::EndDisabled();

            ImGui::Spacing();

            // Selector dropdown for saved cards & presets
            render_card_selector();

            if (!error_.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, "%s", error_.c_str());
                ImGui::Separator();
            }

            if (ImGui::BeginTabBar("AdaptiveCardTabs")) {
                if (ImGui::BeginTabItem("Rendered")) {
                    renderer_.render(bound_, input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [this](const std::string& url) {
                                last_opened_url_ = url;
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                last_submit_payload_ = payload;
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        },
                        [this](const std::string& url, int& w, int& h) {
                            return get_image_texture(url, w, h);
                        }
                    );
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

    bool has_video_overlay() const override { return true; }

    void render_video_ui() override {
        if (bound_.body.empty() && bound_.actions.empty()) return;

        ImVec2 display_size = ImGui::GetIO().DisplaySize;
        float overlay_w = std::clamp(display_size.x * 0.42f, 380.0f, 620.0f);
        float overlay_h = std::clamp(display_size.y * 0.42f, 280.0f, 440.0f);
        float pos_x = std::max(36.0f, display_size.x - overlay_w - 36.0f);
        float pos_y = 36.0f;

        ImVec4 active_color = colors[0];

        ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
        ImGui::SetNextWindowSize(ImVec2(overlay_w, overlay_h));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.5f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.12f, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_Border, active_color);

        if (ImGui::Begin("##AdaptiveCardVideoOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
            // Header Bar
            ImGui::TextColored(active_color, "%s", ICON_MD_DASHBOARD);
            ImGui::SameLine();
            ImGui::Text("%s", current_card_.title.empty() ? "ADAPTIVE CARD HUD" : current_card_.title.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Render Card Elements in Video Stream HUD
            renderer_.render(
                bound_,
                input_state_,
                helpers::adaptive_cards::renderer::action_callbacks{},
                helpers::adaptive_cards::render_config{
                    .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                    .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                    .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                }
            );
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
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
        return helpers::adaptive_cards::renderer::build_submit_payload(input_state_);
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
            {"Round 4 - Repeat + Submit + ShowCard", "round4_card.json", "round4_context.json"},
            {"Round 5 - Markdown Text", "round5_card.json", "round5_context.json"}
        };
    }

    void refresh_saved_cards() {
        saved_cards_ = repository_.list_cards();
    }

    void select_card_by_name(const std::string& card_name) {
        for (size_t i = 0; i < saved_cards_.size(); ++i) {
            if (saved_cards_[i].name == card_name) {
                selected_card_index_ = i;
                current_card_ = saved_cards_[i];
                try {
                    load_from_json(current_card_.card_json, current_card_.context_json);
                    error_.clear();
                } catch (const std::exception& ex) {
                    error_ = std::format("Adaptive Card error: {}", ex.what());
                }
                name(current_card_.title.empty() ? "Adaptive Card" : current_card_.title);
                break;
            }
        }
    }

    void create_new_card() {
        current_card_ = card_record{
            0,
            "new-card",
            "New Adaptive Card",
            std::string(default_card_json),
            std::string(default_context_json),
            "", ""
        };
        load_from_json(current_card_.card_json, current_card_.context_json);
        name(current_card_.title);
    }

    void save_current_card() {
        if (card_json_source_.empty()) return;
        
        current_card_.card_json = card_json_source_;
        current_card_.context_json = context_json_source_;
        if (current_card_.title.empty()) {
            current_card_.title = "Saved Adaptive Card";
        }
        if (current_card_.name.empty()) {
            current_card_.name = models::adaptive_cards::adaptive_cards_repository::slugify(current_card_.title);
        }

        repository_.save_card(current_card_);
        refresh_saved_cards();
        select_card_by_name(current_card_.name);
    }

    void render_card_selector() {
        ImGui::SetNextItemWidth(260.0f);
        std::string combo_preview = current_card_.title.empty() ? "Select Adaptive Card" : current_card_.title;
        if (ImGui::BeginCombo("Cards", combo_preview.c_str())) {
            if (!saved_cards_.empty()) {
                ImGui::SeparatorText("Saved Cards");
                for (size_t i = 0; i < saved_cards_.size(); ++i) {
                    bool is_selected = (current_card_.name == saved_cards_[i].name);
                    if (ImGui::Selectable(saved_cards_[i].title.c_str(), is_selected)) {
                        selected_card_index_ = i;
                        current_card_ = saved_cards_[i];
                        load_from_json(current_card_.card_json, current_card_.context_json);
                        name(current_card_.title);
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if (!presets_.empty()) {
                ImGui::SeparatorText("Built-in Presets");
                for (size_t index = 0; index < presets_.size(); ++index) {
                    if (ImGui::Selectable(presets_[index].name.c_str(), false)) {
                        selected_preset_index_ = index;
                        try {
                            load_selected_preset();
                            current_card_ = card_record{
                                0,
                                models::adaptive_cards::adaptive_cards_repository::slugify(presets_[index].name),
                                presets_[index].name,
                                card_json_source_,
                                context_json_source_,
                                "", ""
                            };
                            name(current_card_.title);
                            error_.clear();
                        } catch (const std::exception& exception) {
                            error_ = std::format("Adaptive Card error: {}", exception.what());
                        }
                    }
                }
            }
            ImGui::EndCombo();
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

    struct texture_info {
        RouenGPUTexture* texture{nullptr};
        int width{0};
        int height{0};
    };

    RouenGPUTexture* get_image_texture(const std::string& url, int& out_w, int& out_h) {
        out_w = 0;
        out_h = 0;
        if (url.empty() || !image_cache_) return nullptr;

        {
            std::lock_guard<std::mutex> lock(image_mutex_);
            auto it = loaded_textures_.find(url);
            if (it != loaded_textures_.end()) {
                out_w = it->second.width;
                out_h = it->second.height;
                return it->second.texture;
            }
        }

        if (image_cache_->isCached(url, out_w, out_h)) {
            RouenGPUTexture* tex = image_cache_->getTexture(TextureHelper::g_gpu_device, url, out_w, out_h);
            if (tex) {
                std::lock_guard<std::mutex> lock(image_mutex_);
                loaded_textures_[url] = {tex, out_w, out_h};
                return tex;
            }
        } else {
            std::lock_guard<std::mutex> lock(image_mutex_);
            if (downloading_urls_.find(url) == downloading_urls_.end()) {
                downloading_urls_.insert(url);
                std::thread([this, url]() {
                    try {
                        image_cache_->downloadAndCache(url);
                    } catch (...) {}
                    std::lock_guard<std::mutex> lock2(image_mutex_);
                    downloading_urls_.erase(url);
                }).detach();
            }
        }
        return nullptr;
    }

    models::adaptive_cards::adaptive_cards_repository repository_;
    std::shared_ptr<::helpers::ImageCache> image_cache_;
    std::unordered_map<std::string, texture_info> loaded_textures_{};
    std::set<std::string> downloading_urls_{};
    std::mutex image_mutex_{};
    std::vector<card_record> saved_cards_{};
    std::size_t selected_card_index_{0};
    card_record current_card_{};
};

} // namespace rouen::cards
