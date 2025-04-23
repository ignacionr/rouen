#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include <format>

#include <imgui/imgui.h>

#include "../interface/card.hpp"

namespace rouen::cards {
    class pomodoro : public card {
    public:
        pomodoro() {
            name("Pomodoro");
            colors[0] = {0.37f, 0.53f, 0.71f, 1.0f}; // Changed from orange to blue accent color (first_color)
            colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Turquoise color (second_color)
            requested_fps = 60;
        }
        bool render() override {
            return render_window([this]() {
                if (ImGui::SmallButton("Reset")) {
                    reset();
                }
                auto const current_time = std::chrono::system_clock::now();
                if (is_done(current_time)) {
                    ImGui::TextUnformatted("Pomodoro done!");
                } else {
                    auto percentage = percentaged_done(current_time);
                    auto dd = ImGui::GetWindowDrawList();
                    // make a circle
                    auto const pos {ImGui::GetWindowPos()};
                    auto const center = ImVec2 {
                        pos.x + ImGui::GetWindowWidth() / 2,
                        pos.y + ImGui::GetWindowHeight() / 2};
                    auto const radius = std::min(
                        0.45 * ImGui::GetWindowWidth(), 
                        0.45 * ImGui::GetWindowHeight());
                    auto const animation_angle = percentage * -75.0f;

                    // five spikes
                    for (int i = 0; i < 5; ++i) {
                        auto const angle = 2 * M_PI * i / 5 + M_PI / 2 + animation_angle;
                        auto const spike = ImVec2 {
                            static_cast<float>(center.x + radius * std::cos(angle)),
                            static_cast<float>(center.y + radius * std::sin(angle))};
                        dd->AddLine(center, spike, ImGui::ColorConvertFloat4ToU32(colors[0]), 2);
                    }
                    // and now fill the percentage that corresponds to the time accrued
                    auto const angle = 2 * M_PI * percentage + M_PI / 2 + animation_angle;
                    // first the parts that are completed
                    static constexpr double fifth_of_circle {2.0 * M_PI / 5.0};
                    for (auto angle_to {M_PI / 2.0 + fifth_of_circle + animation_angle}; angle_to <= angle ; angle_to += fifth_of_circle) {
                        auto const angle_from = angle_to - fifth_of_circle;
                        auto const spike_from = ImVec2 {
                            static_cast<float>(center.x + radius * std::cos(angle_from)),
                            static_cast<float>(center.y + radius * std::sin(angle_from))};
                        auto const spike_to = ImVec2 {
                            static_cast<float>(center.x + radius * std::cos(angle_to)),
                            static_cast<float>(center.y + radius * std::sin(angle_to))};
                        dd->AddTriangleFilled(center, spike_from, spike_to, 
                            ImGui::ColorConvertFloat4ToU32(colors[0]));
                    }
                    // and now the rest
                    auto const spike_from = ImVec2 {
                        static_cast<float>(center.x + radius * std::cos(angle)),
                        static_cast<float>(center.y + radius * std::sin(angle))};
                    auto const spike_to = ImVec2 {
                        static_cast<float>(center.x + radius * std::cos(angle + fifth_of_circle)),
                        static_cast<float>(center.y + radius * std::sin(angle + fifth_of_circle))};
                    dd->AddTriangleFilled(center, spike_from, spike_to, 
                        ImGui::ColorConvertFloat4ToU32(colors[1]));
                    // and now the rest
                    auto const reference_angle = std::floor((angle - animation_angle - M_PI / 2) / fifth_of_circle) * fifth_of_circle + M_PI / 2 + animation_angle;
                    auto const reference_spike = ImVec2 {
                        static_cast<float>(center.x + radius * std::cos(reference_angle)),
                        static_cast<float>(center.y + radius * std::sin(reference_angle))};
                    dd->AddTriangleFilled(center, spike_from, reference_spike, 
                        ImGui::ColorConvertFloat4ToU32(colors[0]));
                }
            });
        }
        void reset() {
            start_time = std::chrono::system_clock::now();
        }
        bool is_done(std::chrono::system_clock::time_point current_time) const {
            return std::chrono::duration_cast<std::chrono::minutes>(current_time - start_time).count() >= 25;
        }
        double percentaged_done(std::chrono::system_clock::time_point current_time) const {
            auto const elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(current_time - start_time).count();
            auto const total = 25 * 60.0;
            return elapsed / total;
        }
        std::string get_uri() const override {
            return "pomodoro";
        }
    private:
        std::chrono::system_clock::time_point start_time {std::chrono::system_clock::now()};
        std::chrono::system_clock::time_point end_time {start_time + std::chrono::minutes(25)};
        ImVec4 third_color {1.0f, 1.0f, 0.0f, 0.5f};
    };
}