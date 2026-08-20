#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
#include "card.hpp"
#include "rouen_plugin_api.hpp"

// Host-side only: bridges a plugin's rouen::plugin::plugin_card to the
// real card interface. Never included from plugin code - it pulls in
// card.hpp deliberately, which plugins must not do (see
// plugin-sdk/rouen_plugin_api.hpp and docs/PLUGINS.md).
namespace rouen::cards {

    struct plugin_card_adapter final : public card {
        explicit plugin_card_adapter(std::unique_ptr<rouen::plugin::plugin_card> impl)
            : impl_{std::move(impl)} {
            name(impl_->title());
        }

        bool render() override {
            // Pass along whatever context the host is currently
            // rendering into (it is not always the main one - see
            // rouen::plugin::plugin_card::render() for why this matters).
            return render_window([this]() { impl_->render(ImGui::GetCurrentContext()); });
        }

        [[nodiscard]] std::string get_uri() const override {
            return impl_->uri();
        }

        void handle_uri(std::string_view uri) override {
            impl_->handle_uri(uri);
        }

        void on_close() override {
            impl_->on_close();
        }

    private:
        std::unique_ptr<rouen::plugin::plugin_card> impl_;
    };

} // namespace rouen::cards
