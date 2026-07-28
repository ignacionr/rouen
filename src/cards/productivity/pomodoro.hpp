#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include <format>
#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/process_helper.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
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
            load_settings();
        }
        ~pomodoro() override {
            media_player_alarm_helper::stop_sound_loop();
            pomodoro_playing = false;
            
            // Execute the end command (closing action) if the session was started but not finished yet
            if (start_command_executed && !end_command_executed && !end_command.empty()) {
                std::thread([cmd = end_command]() {
                    ProcessHelper::executeCommand(cmd);
                }).detach();
            }
        }
        bool render() override {
            auto const current_time = std::chrono::system_clock::now();
            
            // Handle command execution at session start/end
            if (!is_done(current_time)) {
                if (!start_command_executed) {
                    start_command_executed = true;
                    if (!start_command.empty()) {
                        std::thread([cmd = start_command]() {
                            ProcessHelper::executeCommand(cmd);
                        }).detach();
                    }
                }
            } else {
                if (!end_command_executed) {
                    end_command_executed = true;
                    if (!end_command.empty()) {
                        std::thread([cmd = end_command]() {
                            ProcessHelper::executeCommand(cmd);
                        }).detach();
                    }
                }
            }

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
                
                ImGui::Separator();
                
                if (ImGui::CollapsingHeader("Settings")) {
                    ImGui::Text("Start Command:");
                    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.7f);
                    if (ImGui::InputText("##StartCommand", start_command_buffer, sizeof(start_command_buffer))) {
                        start_command = start_command_buffer;
                        save_settings();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Command run when Pomodoro starts (e.g. Do Not Disturb mode script/command)");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Test##StartCmd")) {
                        std::thread([cmd = std::string(start_command_buffer)]() {
                            ProcessHelper::executeCommand(cmd);
                        }).detach();
                    }

                    ImGui::Text("End Command:");
                    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.7f);
                    if (ImGui::InputText("##EndCommand", end_command_buffer, sizeof(end_command_buffer))) {
                        end_command = end_command_buffer;
                        save_settings();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Command run when Pomodoro ends (e.g. Turn off Do Not Disturb)");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Test##EndCmd")) {
                        std::thread([cmd = std::string(end_command_buffer)]() {
                            ProcessHelper::executeCommand(cmd);
                        }).detach();
                    }

                    ImGui::Text("Completion Sound:");
                    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.7f);
                    if (ImGui::Combo("##PomodorSoundSelect", &selected_sound, 
                        [](void* data, int idx, const char** out_text) {
                            auto* options = static_cast<std::vector<std::string>*>(data);
                            if (idx >= 0 && idx < static_cast<int>(options->size())) {
                                *out_text = (*options)[static_cast<std::size_t>(idx)].c_str();
                                return true;
                            }
                            return false;
                        }, &sound_options, static_cast<int>(sound_options.size()))) {
                        if (pomodoro_playing) {
                            media_player_alarm_helper::stop_sound_loop();
                            media_player_alarm_helper::play_sound_loop(sound_files[static_cast<std::size_t>(selected_sound)]);
                        }
                        save_settings();
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
                }
                
                ImGui::Separator();
                
                auto const now = std::chrono::system_clock::now();
                if (is_done(now)) {
                    ImGui::TextUnformatted("Pomodoro done!");
                } else {
                    auto percentage = percentaged_done(now);
                    auto dd = ImGui::GetWindowDrawList();
                    auto const pos {ImGui::GetWindowPos()};
                    
                    float const cursor_y = ImGui::GetCursorPosY();
                    float const remaining_height = ImGui::GetWindowHeight() - cursor_y - 15.0f;
                    
                    if (remaining_height > 40.0f) {
                        auto const center = ImVec2 {
                            pos.x + ImGui::GetWindowWidth() / 2.0f,
                            pos.y + cursor_y + remaining_height / 2.0f};
                        auto const radius = std::min(
                            static_cast<float>(0.40f * ImGui::GetWindowWidth()), 
                            static_cast<float>(0.40f * remaining_height));
                        auto const animation_angle = static_cast<float>(percentage * -75.0);

                        // five spikes
                        for (int i = 0; i < 5; ++i) {
                            auto const angle = static_cast<float>(2 * M_PI * i / 5 + M_PI / 2) + animation_angle;
                            auto angle_d = static_cast<double>(angle);
                            auto const spike = ImVec2 {
                                static_cast<float>(center.x) + radius * static_cast<float>(std::cos(angle_d)),
                                static_cast<float>(center.y) + radius * static_cast<float>(std::sin(angle_d))};
                            dd->AddLine(center, spike, ImGui::ColorConvertFloat4ToU32(colors[0]), 2);
                        }
                        // and now fill the percentage that corresponds to the time accrued
                        auto const angle = static_cast<float>(2 * M_PI * percentage + M_PI / 2) + animation_angle;
                        static constexpr double fifth_of_circle {2.0 * M_PI / 5.0};
                        
                        auto const start_angle = static_cast<float>(M_PI / 2.0 + fifth_of_circle) + animation_angle;
                        auto const step_val = static_cast<float>(fifth_of_circle);
                        int const steps = static_cast<int>(std::floor((angle - start_angle) / step_val)) + 1;
                        
                        for (int i = 0; i < steps; ++i) {
                            float const angle_to = start_angle + static_cast<float>(i) * step_val;
                            auto const angle_from = angle_to - step_val;
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
                        auto angle_d = static_cast<double>(angle);
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
                }
            });
        }
        void reset() {
            start_time = std::chrono::system_clock::now();
            if (pomodoro_playing) {
                media_player_alarm_helper::stop_sound_loop();
                pomodoro_playing = false;
            }
            start_command_executed = false;
            end_command_executed = false;
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

        bool has_video_overlay() const override { return true; }

        void render_video_ui() override {
            auto const now = std::chrono::system_clock::now();
            bool done = is_done(now);
            double pct = std::clamp(percentaged_done(now), 0.0, 1.0);

            long long total_sec = 25 * 60;
            long long elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            long long remaining_sec = std::max(0LL, total_sec - elapsed_sec);
            long long rem_min = remaining_sec / 60;
            long long rem_s = remaining_sec % 60;
            long long elap_min = elapsed_sec / 60;
            long long elap_s = elapsed_sec % 60;

            ImVec4 accent_color;
            ImVec4 bg_color;
            ImVec4 border_color;

            if (done) {
                // Completed celebration styling (emerald green glow)
                static auto flash_start = std::chrono::steady_clock::now();
                double ms_count = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - flash_start).count());
                float pulse = static_cast<float>(0.8 + 0.2 * std::sin(ms_count * 0.005));

                accent_color = ImVec4(0.2f * pulse, 0.95f * pulse, 0.55f * pulse, 1.0f);
                bg_color     = ImVec4(0.04f, 0.12f, 0.08f, 0.92f);
                border_color = ImVec4(0.3f, 1.0f, 0.6f, 1.0f);
            } else {
                // Active focus session (vibrant Pomodoro red-orange)
                accent_color = ImVec4(1.0f, 0.42f, 0.22f, 1.0f);
                bg_color     = ImVec4(0.07f, 0.05f, 0.09f, 0.92f);
                border_color = accent_color;
            }

            ImGuiViewport* vp = ImGui::GetMainViewport();
            float vp_w = vp ? vp->Size.x : 1920.0f;
            float win_w = std::min(520.0f, vp_w - 80.0f);
            float pos_x = std::max(40.0f, vp_w - win_w - 40.0f);
            float pos_y = 40.0f;

            ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
            ImGui::SetNextWindowSize(ImVec2(win_w, 220.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);

            ImGui::PushStyleColor(ImGuiCol_WindowBg, bg_color);
            ImGui::PushStyleColor(ImGuiCol_Border, border_color);

            if (ImGui::Begin("##PomodoroVideoOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
                // Header Bar
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextColored(accent_color, "%s  POMODORO TIMER", ICON_MD_TIMER);

                ImGui::SameLine(ImGui::GetWindowWidth() - 160.0f);
                ImGui::SetWindowFontScale(1.05f);
                if (done) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "%s DONE!", ICON_MD_CHECK_CIRCLE);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s FOCUSING", ICON_MD_PLAY_ARROW);
                }

                ImGui::Separator();
                ImGui::Spacing();

                // Main Body: Radial Gauge on Left, Session Telemetry on Right
                ImGui::BeginGroup();
                
                // Left Group: Radial Gauge
                ImVec2 gauge_size(150.0f, 130.0f);
                ImGui::Dummy(gauge_size);
                ImVec2 rect_min = ImGui::GetItemRectMin();
                ImVec2 gauge_center = ImVec2(rect_min.x + gauge_size.x * 0.5f, rect_min.y + gauge_size.y * 0.48f);
                float radius = 48.0f;

                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Outer subtle track
                draw_list->AddCircle(gauge_center, radius, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.12f)), 64, 7.0f);

                // Progress Arc
                float start_angle = -static_cast<float>(M_PI) / 2.0f; // 12 o'clock
                float end_angle   = start_angle + static_cast<float>(pct * 2.0 * M_PI);

                if (pct > 0.001) {
                    draw_list->PathClear();
                    draw_list->PathArcTo(gauge_center, radius, start_angle, end_angle, 64);
                    draw_list->PathStroke(ImGui::ColorConvertFloat4ToU32(accent_color), 0, 7.5f);

                    // Indicator dot at current progress head
                    ImVec2 head_pt = ImVec2(gauge_center.x + std::cos(end_angle) * radius,
                                            gauge_center.y + std::sin(end_angle) * radius);
                    draw_list->AddCircleFilled(head_pt, 6.0f, ImGui::ColorConvertFloat4ToU32(accent_color));
                    draw_list->AddCircleFilled(head_pt, 3.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
                }

                // Digital Time Remaining in Center of Gauge
                std::string time_str = std::format("{:02d}:{:02d}", rem_min, rem_s);
                ImVec2 time_size = ImGui::CalcTextSize(time_str.c_str());
                
                float text_scale = 1.45f;
                ImVec2 scaled_size = ImVec2(time_size.x * text_scale, time_size.y * text_scale);
                
                draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * text_scale,
                    ImVec2(gauge_center.x - scaled_size.x * 0.5f, gauge_center.y - scaled_size.y * 0.5f - 4.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), time_str.c_str());

                // Label underneath time
                std::string label_str = done ? "COMPLETED" : "REMAINING";
                float label_scale = 0.75f;
                ImVec2 lbl_size = ImGui::CalcTextSize(label_str.c_str());
                draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * label_scale,
                    ImVec2(gauge_center.x - (lbl_size.x * label_scale) * 0.5f, gauge_center.y + 14.0f),
                    ImGui::ColorConvertFloat4ToU32(colors[1]), label_str.c_str());

                ImGui::EndGroup();

                // Right Group: Detailed Session Telemetry
                ImGui::SameLine();
                ImGui::BeginGroup();

                ImGui::SetWindowFontScale(1.05f);
                ImGui::TextColored(colors[1], "%s SESSION METRICS", ICON_MD_TIMELINE);
                ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.98f, 1.0f), "Target: 25m | Elapsed: %02lld:%02lld (%.0f%%)", elap_min, elap_s, pct * 100.0);

                ImGui::Spacing();
                
                // Custom styled progress bar
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, accent_color);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.16f, 0.22f, 0.8f));
                ImGui::ProgressBar(static_cast<float>(pct), ImVec2(280.0f, 12.0f), "");
                ImGui::PopStyleColor(2);

                ImGui::Spacing();

                // Start / End script status badges
                if (!start_command.empty()) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s Start Cmd: Active", ICON_MD_TERMINAL);
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 0.8f), "%s Start Cmd: None", ICON_MD_TERMINAL);
                }

                if (!end_command.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s End Cmd: Ready", ICON_MD_TERMINAL);
                }

                // Audio Alert
                if (selected_sound >= 0 && selected_sound < static_cast<int>(sound_options.size())) {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1.0f), "%s Alert: %s", ICON_MD_VOLUME_UP, sound_options[static_cast<std::size_t>(selected_sound)].c_str());
                }

                ImGui::EndGroup();
            }
            ImGui::End();

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
        
        // Override to provide MCP functions
        std::vector<mcp_function> get_mcp_functions() const override {
            return {
                mcp_function(
                    "start_pomodoro",
                    "Start a new 25-minute Pomodoro session.",
                    R"({"type": "object", "properties": {}})",
                    [this](const std::string&) -> std::string {
                        const_cast<pomodoro*>(this)->reset();
                        return R"({"status": "success", "message": "Pomodoro started"})";
                    }
                ),
                mcp_function(
                    "get_pomodoro_status",
                    "Get the current status of the Pomodoro session including state, time remaining, and commands configured.",
                    R"({"type": "object", "properties": {}})",
                    [this](const std::string&) -> std::string {
                        auto const now = std::chrono::system_clock::now();
                        bool done = is_done(now);
                        
                        double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time).count();
                        double total = 25.0 * 60.0;
                        double remaining = std::max(0.0, total - elapsed);
                        
                        glz::json_t res;
                        res["status"] = done ? "completed" : "running";
                        res["time_remaining_seconds"] = remaining;
                        res["percentage_done"] = percentaged_done(now);
                        res["start_command"] = start_command;
                        res["end_command"] = end_command;
                        res["start_command_executed"] = start_command_executed;
                        res["end_command_executed"] = end_command_executed;
                        
                        std::string out;
                        auto err = glz::write_json(res, out);
                        if (err) {
                            return R"({"status": "error", "message": "Failed to serialize status"})";
                        }
                        return out;
                    }
                )
            };
        }
    private:
        void save_settings() {
            try {
                auto config_dir = rouen::platform::get_user_config_directory();
                std::filesystem::create_directories(config_dir);
                auto config_path = config_dir / "pomodoro_settings.json";
                
                glz::json_t json;
                json["start_command"] = start_command;
                json["end_command"] = end_command;
                json["selected_sound"] = static_cast<double>(selected_sound);
                
                std::ofstream file(config_path);
                if (file.is_open()) {
                    std::string json_str;
                    auto result = glz::write_json(json, json_str);
                    if (!result) {
                        file << json_str;
                    }
                    file.close();
                }
            } catch (const std::exception&) {
                // Ignore
            }
        }
        void load_settings() {
            // Load environment variable defaults first
            auto env_start = GET_CONFIG_OPT("POMODORO_START_COMMAND");
            if (env_start) {
                start_command = *env_start;
                std::snprintf(start_command_buffer, sizeof(start_command_buffer), "%s", start_command.c_str());
            }
            auto env_end = GET_CONFIG_OPT("POMODORO_END_COMMAND");
            if (env_end) {
                end_command = *env_end;
                std::snprintf(end_command_buffer, sizeof(end_command_buffer), "%s", end_command.c_str());
            }

            try {
                auto config_path = rouen::platform::get_user_config_directory() / "pomodoro_settings.json";
                if (!std::filesystem::exists(config_path)) {
                    return;
                }
                std::ifstream file(config_path);
                if (file.is_open()) {
                    std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    glz::json_t json;
                    auto ec = glz::read_json(json, json_str);
                    if (!ec) {
                        if (json.contains("start_command")) {
                            start_command = json["start_command"].get<std::string>();
                            std::snprintf(start_command_buffer, sizeof(start_command_buffer), "%s", start_command.c_str());
                        }
                        if (json.contains("end_command")) {
                            end_command = json["end_command"].get<std::string>();
                            std::snprintf(end_command_buffer, sizeof(end_command_buffer), "%s", end_command.c_str());
                        }
                        if (json.contains("selected_sound")) {
                            selected_sound = static_cast<int>(json["selected_sound"].get<double>());
                        }
                    }
                    file.close();
                }
            } catch (const std::exception&) {
                // Ignore
            }
        }

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

        // Command settings
        std::string start_command;
        std::string end_command;
        char start_command_buffer[1024] = "";
        char end_command_buffer[1024] = "";
        bool start_command_executed = false;
        bool end_command_executed = false;
    };
}

