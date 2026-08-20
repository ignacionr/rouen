#pragma once

// 1. Standard includes in alphabetic order
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/adaptive_cards/templater.hpp"
#include "../../helpers/platform_utils.hpp"
#include "card.hpp"
#include "rouen_plugin_api.hpp"

// Host-side only: bridges a plugin's rouen::plugin::adaptive_card_plugin
// to the real card interface, reusing Rouen's existing Adaptive Card
// parser/templater/renderer. Never included from plugin code - unlike
// plugin_card_adapter.hpp, a plugin of this style never touches ImGui
// itself, so there is no context-sharing concern here at all; this
// adapter owns 100% of the ImGui interaction.
namespace rouen::cards {

    struct adaptive_card_plugin_adapter final : public card {
        explicit adaptive_card_plugin_adapter(std::unique_ptr<rouen::plugin::adaptive_card_plugin> impl)
            : impl_{std::move(impl)} {
            colors[0] = {0.20f, 0.43f, 0.70f, 1.0f};
            colors[1] = {0.14f, 0.32f, 0.55f, 0.75f};
            width = 420.0f;
            name(impl_->title());
            rebuild();
        }

        bool render() override {
            return render_window([this]() {
                if (!error_.empty()) {
                    ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, "%s", error_.c_str());
                    return;
                }
                renderer_.render(
                    bound_, input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [this](std::string const& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                            impl_->on_open_url(url);
                        },
                        .on_submit = [this](std::string const& payload) {
                            impl_->on_submit(payload);
                        }},
                    helpers::adaptive_cards::render_config{
                        .font_bold = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code = rouen::fonts::get_font(rouen::fonts::FontType::Mono)});
            });
        }

        [[nodiscard]] std::string get_uri() const override {
            return impl_->uri();
        }

        void handle_uri(std::string_view uri) override {
            impl_->handle_uri(uri);
            rebuild();
        }

        void on_close() override {
            impl_->on_close();
        }

    private:
        void rebuild() {
            try {
                auto const parsed = parser_.parse(impl_->card_json());

                helpers::adaptive_cards::context data{};
                if (std::string const context_json = impl_->context_json(); !context_json.empty()) {
                    if (auto const err = glz::read_json(data, context_json)) {
                        throw std::runtime_error(glz::format_error(err, context_json));
                    }
                }

                bound_ = templater_.bind(parsed, data);
                input_state_ = {};
                error_.clear();
            } catch (std::exception const& e) {
                error_ = std::format("Adaptive Card plugin error: {}", e.what());
            }
        }

        std::unique_ptr<rouen::plugin::adaptive_card_plugin> impl_;
        helpers::adaptive_cards::parser parser_{};
        helpers::adaptive_cards::templater templater_{};
        helpers::adaptive_cards::renderer renderer_{};
        helpers::adaptive_cards::renderer::input_state input_state_{};
        helpers::adaptive_cards::card_document bound_{};
        std::string error_{};
    };

} // namespace rouen::cards
