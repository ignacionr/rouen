#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include <format>
#include <thread>
#include <vector>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/media_player_alarm.hpp"

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
        ~pomodoro() override {
            media_player_alarm_helper::stop_sound_loop();
            pomodoro_playing = false;
        }
        bool render() override {
            auto const current_time = std::chrono::system_clock::now();
            if (is_done(current_time)) {
                if (!pomodoro_playing) {
                    media_player_alarm_helper::play_sound_loop(sound_files[static_cast<std::size_t>(selected_sound)]);
                    pomodoro_playing = true;
                }
            } else {
                if (pomodoro_playing) {
                    media_player_alarm_helper::stop_sound_loop();
                    pomodoro_playing = false;
                }
            }
            return render_window([this]() {
                if (ImGui::SmallButton("Reset")) {
                    reset();
                }
                
                // Sound selection UI
                ImGui::Separator();
                ImGui::Text("Completion Sound:");
                if (ImGui::Combo("##PomodorSoundSelect", &selected_sound, 
                    [](void* data, int idx, const char** out_text) {
                        auto* options = static_cast<std::vector<std::string>*>(data);
                        if (idx >= 0 && idx < static_cast<int>(options->size())) {
                            *out_text = (*options)[static_cast<std::size_t>(idx)].c_str();
                            return true;
                        }
                        return false;
                    }, &sound_options, static_cast<int>(sound_options.size()))) {
                    // Sound selection changed - optionally stop current sound
                    if (pomodoro_playing) {
                        media_player_alarm_helper::stop_sound_loop();
                        media_player_alarm_helper::play_sound_loop(sound_files[static_cast<std::size_t>(selected_sound)]);
                    }
                }
                
                ImGui::SameLine();
                if (ImGui::SmallButton("Test##PomodorSound")) {
                    media_player_alarm_helper::stop_sound_loop();
                    media_player_alarm_helper::play_sound_loop(sound_files[static_cast<std::size_t>(selected_sound)]);
                    std::thread([]() {
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                        media_player_alarm_helper::stop_sound_loop();
                    }).detach();
                }
                ImGui::Separator();
                
                auto const now = std::chrono::system_clock::now();
                if (is_done(now)) {
                    ImGui::TextUnformatted("Pomodoro done!");
                } else {
                    auto percentage = percentaged_done(now);
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
            if (pomodoro_playing) {
                media_player_alarm_helper::stop_sound_loop();
                pomodoro_playing = false;
            }
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
        bool pomodoro_playing = false;
        
        // Sound selection for Pomodoro completion
        std::vector<std::string> sound_options = {
            "Default Sound", 
            "Classic Alert", 
            "Simple Tone", 
            "Beeping Pattern"
        };
        std::vector<std::string> sound_files = {
            "img/alarm.mp3", 
            "img/alarm_classic.wav", 
            "img/new_alarm.wav", 
            "img/alarm_beeps.wav"
        };
        int selected_sound = 0;
    };
}
