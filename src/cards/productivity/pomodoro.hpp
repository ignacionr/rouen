#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include <format>

#include "imgui.h"

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
                        static_cast<float>(0.45f * ImGui::GetWindowWidth()), 
                        static_cast<float>(0.45f * ImGui::GetWindowHeight()));
                    auto const animation_angle = static_cast<float>(percentage * -75.0);

                    // five spikes
                    for (int i = 0; i < 5; ++i) {
                        auto const angle = static_cast<float>(2 * M_PI * i / 5 + M_PI / 2) + animation_angle;
                        // Use a helper function that takes double parameters to avoid warnings
                        double angle_d = static_cast<double>(angle);
                        auto const spike = ImVec2 {
                            static_cast<float>(center.x) + radius * static_cast<float>(std::cos(angle_d)),
                            static_cast<float>(center.y) + radius * static_cast<float>(std::sin(angle_d))};
                        dd->AddLine(center, spike, ImGui::ColorConvertFloat4ToU32(colors[0]), 2);
                    }
                    // and now fill the percentage that corresponds to the time accrued
                    auto const angle = static_cast<float>(2 * M_PI * percentage + M_PI / 2) + animation_angle;
                    // first the parts that are completed
                    static constexpr double fifth_of_circle {2.0 * M_PI / 5.0};
                    for (auto angle_to = static_cast<float>(M_PI / 2.0 + fifth_of_circle) + animation_angle; angle_to <= angle; angle_to += static_cast<float>(fifth_of_circle)) {
                        auto const angle_from = angle_to - static_cast<float>(fifth_of_circle);
                        double angle_from_d = static_cast<double>(angle_from);
                        double angle_to_d = static_cast<double>(angle_to);
                        auto const spike_from = ImVec2 {
                            static_cast<float>(center.x) + radius * static_cast<float>(std::cos(angle_from_d)),
                            static_cast<float>(center.y) + radius * static_cast<float>(std::sin(angle_from_d))};
                        auto const spike_to = ImVec2 {
                            static_cast<float>(center.x) + radius * static_cast<float>(std::cos(angle_to_d)),
                            static_cast<float>(center.y) + radius * static_cast<float>(std::sin(angle_to_d))};
                        dd->AddTriangleFilled(center, spike_from, spike_to, 
                            ImGui::ColorConvertFloat4ToU32(colors[0]));
                    }
                    // and now the rest
                    double angle_d = static_cast<double>(angle);
                    auto const spike_from = ImVec2 {
                        static_cast<float>(center.x) + radius * static_cast<float>(std::cos(angle_d)),
                        static_cast<float>(center.y) + radius * static_cast<float>(std::sin(angle_d))};
                    double angle_plus_fifth_d = static_cast<double>(angle + static_cast<float>(fifth_of_circle));
                    auto const spike_to = ImVec2 {
                        static_cast<float>(center.x) + radius * static_cast<float>(std::cos(angle_plus_fifth_d)),
                        static_cast<float>(center.y) + radius * static_cast<float>(std::sin(angle_plus_fifth_d))};
                    dd->AddTriangleFilled(center, spike_from, spike_to, 
                        ImGui::ColorConvertFloat4ToU32(colors[1]));
                    
                    // and now the reference angle part
                    auto const reference_angle = static_cast<float>(std::floor((static_cast<double>(angle) - static_cast<double>(animation_angle) - M_PI / 2) / fifth_of_circle) * fifth_of_circle + M_PI / 2) + animation_angle;
                    double ref_angle_d = static_cast<double>(reference_angle);
                    auto const reference_spike = ImVec2 {
                        static_cast<float>(center.x) + radius * static_cast<float>(std::cos(ref_angle_d)),
                        static_cast<float>(center.y) + radius * static_cast<float>(std::sin(ref_angle_d))};
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