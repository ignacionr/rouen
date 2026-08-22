#pragma once

#include <rouen_plugin_api.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

namespace sample_plugin {

    // A minimal example of a plugin-provided card. It registers under
    // the "hello" schema (see plugin_entry.cpp), so "hello:Ada" creates
    // one greeting "Ada" by default.
    class hello_card final : public rouen::plugin::plugin_card {
    public:
        explicit hello_card(std::string_view locator) {
            handle_uri(locator);
        }

        void draw() override {
            ImGui::TextUnformatted("Hello from a Rouen plugin!");
            ImGui::Separator();
            ImGui::InputText("Name", name_buffer_.data(), name_buffer_.size());
            if (ImGui::Button("Greet")) {
                ++greeting_count_;
            }
            ImGui::SameLine();
            ImGui::Text("Greeted %d time(s)", greeting_count_);
            if (greeting_count_ > 0) {
                ImGui::TextColored(ImVec4{0.4f, 0.9f, 0.5f, 1.0f}, "Hello, %s!", name_buffer_.data());
            }
        }

        [[nodiscard]] std::string title() const override { return "Hello Plugin"; }

        [[nodiscard]] std::string uri() const override {
            return std::format("hello:{}", name_buffer_.data());
        }

        void handle_uri(std::string_view locator) override {
            if (locator.empty()) {
                return;
            }
            auto const len = std::min(locator.size(), name_buffer_.size() - 1);
            std::copy_n(locator.data(), len, name_buffer_.begin());
            name_buffer_[len] = '\0';
        }

    private:
        std::array<char, 128> name_buffer_{"World"};
        int greeting_count_{0};
    };

} // namespace sample_plugin
