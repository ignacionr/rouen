#pragma once

#include <chrono>
#include <cmath>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/media_player.hpp"

#include "../interface/card.hpp"
#include "../../hosts/video_feed_host.hpp"
#include <glaze/glaze.hpp>

namespace rouen::cards {
    struct mcp_create_alarm_params {
        std::optional<std::string> datetime;
    };
    class alarm : public card {
    public:
        alarm(std::string_view target_time_str = {}) {
            name("Alarm");
            colors[0] = {0.8f, 0.3f, 0.3f, 1.0f}; // Red-orange primary color (first_color)
            colors[1] = {0.9f, 0.5f, 0.3f, 0.7f}; // Light orange secondary color (second_color)
            // Additional color for visualization
            get_color(2, ImVec4(1.0f, 0.6f, 0.2f, 1.0f)); // Brighter orange for highlights
            get_color(3, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
            
            requested_fps = 60;
            width = 175.0f; // Reduced to half the default width (default is 300.0f)
            
            // Set up default alarm time (1 hour from current time)
            auto current_time = std::chrono::system_clock::now();
            auto current_time_t = std::chrono::system_clock::to_time_t(current_time);
            std::tm current_tm = *std::localtime(&current_time_t);
            
            // Default to 1 hour from now
            current_tm.tm_hour += 1;
            target_time = std::chrono::system_clock::from_time_t(std::mktime(&current_tm));
            
            // Parse target time if provided
            if (!target_time_str.empty()) {
                parse_time(std::string(target_time_str));
            }
            
            // Format the initial time for display
            update_time_string();
        }

        bool render() override {
            auto time_remaining = get_time_remaining(std::chrono::system_clock::now());

            // Play alarm sound in loop if ringing, active and not already playing
            if (alarm_active && time_remaining <= std::chrono::seconds(0)) {
                if (!alarm_playing) {
                    media_player_alarm_helper::play_sound_loop(alarm_sounds[static_cast<size_t>(selected_alarm_sound)].second);
                    alarm_playing = true;
                }
            } else {
                // Stop alarm sound if not active or not ringing
                if (alarm_playing) {
                    media_player_alarm_helper::stop_sound_loop();
                    alarm_playing = false;
                }
            }

            if (alarm_active && time_remaining <= std::chrono::seconds(0)) {
                static auto last_blink_time = std::chrono::steady_clock::now();
                static bool is_visible = true;
                auto now = std::chrono::steady_clock::now();
                auto time_since_blink = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_blink_time).count();
                if (time_since_blink >= 1000) {
                    is_visible = !is_visible;
                    last_blink_time = now;
                }
                if (!is_visible) {
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.9f, 0.3f, 0.3f, 0.9f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.8f, 0.0f, 0.0f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.8f, 0.0f, 0.0f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.9f, 0.1f, 0.1f, 0.7f));
                }
                bool result = render_window([this]() {
                    render_alarm_content();
                });
                ImGui::PopStyleColor(3);
                return result;
            }
            return render_window([this]() {
                render_alarm_content();
            });
        }

        void render_video_ui() override {
            auto now = std::chrono::system_clock::now();
            auto time_remaining = get_time_remaining(now);
            bool is_ringing = alarm_active && time_remaining <= std::chrono::seconds(0);

            ImGui::SetNextWindowPos(ImVec2(1380.0f, 40.0f));
            ImGui::SetNextWindowSize(ImVec2(500.0f, 220.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);

            if (is_ringing) {
                static auto last_flash = std::chrono::steady_clock::now();
                static bool flash = false;
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_flash).count() > 250) {
                    flash = !flash;
                    last_flash = std::chrono::steady_clock::now();
                }
                ImGui::PushStyleColor(ImGuiCol_WindowBg, flash ? ImVec4(0.85f, 0.15f, 0.15f, 0.95f) : ImVec4(0.55f, 0.08f, 0.08f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.9f, 0.3f, 1.0f));
            } else if (alarm_active) {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.14f, 0.24f, 0.92f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 0.88f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.45f, 0.8f));
            }

            if (ImGui::Begin("##AlarmVideoOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
                ImGui::SetWindowFontScale(1.8f);
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "⏰ ALARM CARD");
                ImGui::Separator();
                ImGui::Spacing();

                if (is_ringing) {
                    ImGui::SetWindowFontScale(2.4f);
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "🚨 ALARM RINGING!");
                    ImGui::SetWindowFontScale(1.6f);
                    ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.5f, 1.0f), "Time's up! Attention required.");
                } else if (alarm_active) {
                    auto hours = std::chrono::duration_cast<std::chrono::hours>(time_remaining).count();
                    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time_remaining).count() % 60;
                    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count() % 60;
                    std::string timer_str = std::format("{:02d}:{:02d}:{:02d}", hours, minutes, seconds);

                    ImGui::SetWindowFontScale(1.4f);
                    ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.6f, 1.0f), "COUNTDOWN ACTIVE");

                    ImGui::SetWindowFontScale(2.6f);
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", timer_str.c_str());

                    // Visual Progress Bar
                    ImGui::SetWindowFontScale(1.0f);
                    float total_sec = 1800.0f; // 30 min default
                    float rem_sec = static_cast<float>(std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count());
                    float progress = std::clamp(rem_sec / total_sec, 0.0f, 1.0f);
                    ImGui::ProgressBar(progress, ImVec2(-1.0f, 12.0f), "");
                } else {
                    ImGui::SetWindowFontScale(1.8f);
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "ALARM INACTIVE");
                    ImGui::SetWindowFontScale(1.3f);
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Set a timer to enable overlay countdown.");
                }
            }
            ImGui::End();

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        void paint_video_surface(SDL_Surface* surface, int surface_w, int surface_h) override {
            if (!surface) return;
            (void)surface_h;

            auto now = std::chrono::system_clock::now();
            auto time_remaining = get_time_remaining(now);
            bool is_ringing = alarm_active && time_remaining <= std::chrono::seconds(0);

            // Card position & dimensions on the 1080p video feed (top right overlay box)
            int card_w = 480;
            int card_h = 160;
            int card_x = surface_w - card_w - 40;
            int card_y = 40;

            // Draw card background box
            SDL_Rect box_rect{card_x, card_y, card_w, card_h};
            uint32_t bg_color;

            if (is_ringing) {
                // Flashing red container when ringing
                static auto last_flash = std::chrono::steady_clock::now();
                static bool flash_state = false;
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_flash).count() > 250) {
                    flash_state = !flash_state;
                    last_flash = std::chrono::steady_clock::now();
                }
                bg_color = flash_state ? SDL_MapSurfaceRGB(surface, 220, 40, 40) : SDL_MapSurfaceRGB(surface, 150, 20, 20);
            } else if (alarm_active) {
                // Deep navy container when counting down
                bg_color = SDL_MapSurfaceRGB(surface, 20, 30, 50);
            } else {
                // Dark grey container when stopped
                bg_color = SDL_MapSurfaceRGB(surface, 35, 35, 35);
            }

            SDL_FillSurfaceRect(surface, &box_rect, bg_color);

            // Draw 2px border around card box
            uint32_t border_color = is_ringing ? SDL_MapSurfaceRGB(surface, 255, 255, 100) : SDL_MapSurfaceRGB(surface, 255, 140, 40);
            SDL_Rect top_b{card_x - 2, card_y - 2, card_w + 4, 2};
            SDL_Rect bot_b{card_x - 2, card_y + card_h, card_w + 4, 2};
            SDL_Rect left_b{card_x - 2, card_y - 2, 2, card_h + 4};
            SDL_Rect right_b{card_x + card_w, card_y - 2, 2, card_h + 4};
            SDL_FillSurfaceRect(surface, &top_b, border_color);
            SDL_FillSurfaceRect(surface, &bot_b, border_color);
            SDL_FillSurfaceRect(surface, &left_b, border_color);
            SDL_FillSurfaceRect(surface, &right_b, border_color);
        }

        void render_alarm_content() {
            auto time_remaining = get_time_remaining(std::chrono::system_clock::now());
            
            // Draw visual alarm ringing or countdown
            if (alarm_active && time_remaining <= std::chrono::seconds(0)) {
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("Time's up!").x) * 0.5f);
                ImGui::TextColored(colors[0], "Time's up!");
                static float flash_intensity = 0.0f;
                static float flash_direction = 1.0f;
                flash_intensity += flash_direction * 0.05f;
                if (flash_intensity >= 1.0f || flash_intensity <= 0.0f) {
                    flash_direction *= -1.0f;
                }
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("ALARM").x) * 0.5f);
                ImGui::TextColored(ImVec4(1.0f, flash_intensity, flash_intensity, 1.0f), "ALARM");
                ImGui::PopFont();
                // Snooze and Stop buttons
                ImGui::Spacing();
                float button_width = ImGui::GetWindowWidth() * 0.45f;
                if (ImGui::Button("Snooze 5m", ImVec2(button_width, 0))) {
                    snooze(5);
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop", ImVec2(button_width, 0))) {
                    stop_alarm();
                }
            } else {
                if (!alarm_active) {
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Use a larger font
                    std::string label = "Stopped";
                    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(label.c_str()).x) * 0.5f);
                    ImGui::TextColored(colors[3], "%s", label.c_str());
                    ImGui::PopFont();
                } else {
                    // Show remaining time in hours, minutes, seconds - centered
                    auto hours = std::chrono::duration_cast<std::chrono::hours>(time_remaining).count();
                    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time_remaining).count() % 60;
                    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count() % 60;
                    
                    std::string time_str = std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
                    
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Use a larger font
                    ImGui::TextColored(colors[2], "%s", time_str.c_str());
                    ImGui::PopFont();
                    
                    // Visual progress indicator with stacked blocks
                    draw_vertical_progress_blocks(time_remaining);
                }
            }

            // Time setting interface - now vertical layout
            float y_before = ImGui::GetCursorPosY();
            if (ImGui::CollapsingHeader("Set")) {
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.9f);
                
                if (ImGui::InputText("##time", time_buffer, sizeof(time_buffer), 
                                    ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank)) {
                    parse_time(std::string(time_buffer));
                }
                
                // Show hint text when input is empty
                if (time_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                    auto pos = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(pos.x + 5, pos.y + 2),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        "HH:MM"
                    );
                }
                
                ImGui::PopItemWidth();
                
                if (ImGui::Button("Reset", ImVec2(ImGui::GetWindowWidth() * 0.9f, 0))) {
                    reset();
                }
                
                ImGui::Spacing();
                
                float button_width = ImGui::GetWindowWidth() * 0.4f;

                if (ImGui::Button("-2m", ImVec2(button_width, 0))) add_minutes(-2);
                ImGui::SameLine();
                if (ImGui::Button("-5m", ImVec2(button_width, 0))) add_minutes(-5);
                
                if (ImGui::Button("+5m", ImVec2(button_width, 0))) add_minutes(5);
                ImGui::SameLine();
                if (ImGui::Button("+15m", ImVec2(button_width, 0))) add_minutes(15);
                
                if (ImGui::Button("+30m", ImVec2(button_width, 0))) add_minutes(30);
                ImGui::SameLine();
                if (ImGui::Button("+1h", ImVec2(button_width, 0))) add_minutes(60);
                
                // Alarm sound selection
                ImGui::Spacing();
                ImGui::Text("Alarm Sound:");
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.9f);
                if (ImGui::Combo("##alarm_sound", &selected_alarm_sound, 
                                [](void* data, int idx, const char** out_text) -> bool {
                                    auto& sounds = *static_cast<std::vector<std::pair<std::string, std::string>>*>(data);
                                    if (idx < 0 || idx >= static_cast<int>(sounds.size())) return false;
                                    *out_text = sounds[static_cast<size_t>(idx)].first.c_str();
                                    return true;
                                }, &alarm_sounds, static_cast<int>(alarm_sounds.size()))) {
                    // Sound selection changed
                }
                ImGui::PopItemWidth();
                
                // Test alarm sound button
                if (ImGui::Button("Test Sound", ImVec2(ImGui::GetWindowWidth() * 0.9f, 0))) {
                    if (!alarm_playing) {
                        media_player_alarm_helper::play_sound_loop(alarm_sounds[static_cast<size_t>(selected_alarm_sound)].second);
                        // Schedule stop after 2 seconds
                        std::thread([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            if (!alarm_playing) { // Only stop if it's still the test sound
                                media_player_alarm_helper::stop_sound_loop();
                            }
                        }).detach();
                    }
                }
            }
            float y_after = ImGui::GetCursorPosY();
            set_header_height = y_after - y_before;
            
        }
        
        void snooze(int minutes) {
            set_to_current_time();
            add_minutes(minutes);
            alarm_playing = false;
            media_player_alarm_helper::stop_sound_loop();
            alarm_active = true;
        }
        void stop_alarm() {
            alarm_playing = false;
            media_player_alarm_helper::stop_sound_loop();
            set_to_current_time();
            alarm_active = false;
        }
        void reset() {
            set_to_current_time();
            add_minutes(30);
            alarm_active = true;
            if (alarm_playing) {
                alarm_playing = false;
                media_player_alarm_helper::stop_sound_loop();
            }
        }
        void set_to_current_time() {
            target_time = std::chrono::system_clock::now();
            update_time_string();
            if (alarm_playing) {
                alarm_playing = false;
                media_player_alarm_helper::stop_sound_loop();
            }
        }
        void add_minutes(int minutes) {
            target_time += std::chrono::minutes(minutes);
            update_time_string();
            alarm_active = true;
            auto new_time_remaining = get_time_remaining(std::chrono::system_clock::now());
            if (alarm_playing && new_time_remaining > std::chrono::seconds(0)) {
                alarm_playing = false;
                media_player_alarm_helper::stop_sound_loop();
            }
        }
        void parse_time(const std::string& time_str) {
            // Try to parse ISO date time format first (e.g. YYYY-MM-DDTHH:MM:SS or YYYY-MM-DD HH:MM:SS)
            if (time_str.length() >= 16 && time_str[4] == '-' && time_str[7] == '-') {
                int year = 0, month = 0, day = 0, hours = 0, minutes = 0, seconds = 0;
                char dash1{'\0'}, dash2{'\0'}, sep{'\0'}, colon1{'\0'}, colon2{'\0'};
                std::stringstream ss(time_str);
                if (ss >> year >> dash1 >> month >> dash2 >> day >> sep >> hours >> colon1 >> minutes) {
                    if ((sep == 'T' || sep == ' ') && dash1 == '-' && dash2 == '-' && colon1 == ':') {
                        // Optional seconds
                        if (ss >> colon2 >> seconds && colon2 == ':') {
                            // parsed seconds
                        }
                        
                        // Validate and set target_time
                        if (year >= 1970 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
                            hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60 && seconds >= 0 && seconds < 60) {
                            
                            std::tm target_tm{};
                            target_tm.tm_year = year - 1900;
                            target_tm.tm_mon = month - 1;
                            target_tm.tm_mday = day;
                            target_tm.tm_hour = hours;
                            target_tm.tm_min = minutes;
                            target_tm.tm_sec = seconds;
                            target_tm.tm_isdst = -1; // Let system determine DST
                            
                            auto t_time = std::mktime(&target_tm);
                            if (t_time != -1) {
                                target_time = std::chrono::system_clock::from_time_t(t_time);
                                update_time_string();
                                alarm_active = true;
                                if (alarm_playing) {
                                    alarm_playing = false;
                                    media_player_alarm_helper::stop_sound_loop();
                                }
                                return;
                            }
                        }
                    }
                }
            }

            // Handle several formats: "HH:MM", "HH:MM:SS", or just "HHMM"
            int hours = 0, minutes = 0;
            
            // Remove any non-digit or non-colon characters
            std::string clean_str;
            for (char c : time_str) {
                if (std::isdigit(c) || c == ':') {
                    clean_str += c;
                }
            }
            
            // Parse different time formats
            if (clean_str.find(':') != std::string::npos) {
                // Format with colon (HH:MM or HH:MM:SS)
                std::stringstream ss(clean_str);
                char delimiter{'\0'};
                ss >> hours >> delimiter >> minutes;
            } else if (clean_str.length() >= 3) {
                // Format without colon (HHMM)
                if (clean_str.length() >= 4) {
                    hours = std::stoi(clean_str.substr(0, 2));
                    minutes = std::stoi(clean_str.substr(2, 2));
                } else {
                    hours = std::stoi(clean_str.substr(0, 1));
                    minutes = std::stoi(clean_str.substr(1, 2));
                }
            }
            
            // Validate and set time
            if (hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60) {
                auto current_time = std::chrono::system_clock::now();
                auto current_time_t = std::chrono::system_clock::to_time_t(current_time);
                std::tm current_tm = *std::localtime(&current_time_t);
                current_tm.tm_hour = hours;
                current_tm.tm_min = minutes;
                current_tm.tm_sec = 0;
                auto new_target = std::chrono::system_clock::from_time_t(std::mktime(&current_tm));
                if (new_target < current_time) {
                    new_target += std::chrono::hours(24);
                }
                target_time = new_target;
                update_time_string();
                alarm_active = true;
                if (alarm_playing) {
                    alarm_playing = false;
                    media_player_alarm_helper::stop_sound_loop();
                }
            }
        }
        
        void update_time_string() {
            auto time_t = std::chrono::system_clock::to_time_t(target_time);
            std::tm time_tm = *std::localtime(&time_t);
            
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm = *std::localtime(&now_time_t);
            
            std::string formatted;
            if (time_tm.tm_year != now_tm.tm_year || time_tm.tm_mon != now_tm.tm_mon || time_tm.tm_mday != now_tm.tm_mday) {
                formatted = std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}", 
                                        time_tm.tm_year + 1900, time_tm.tm_mon + 1, time_tm.tm_mday,
                                        time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec);
            } else {
                formatted = std::format("{:02d}:{:02d}", time_tm.tm_hour, time_tm.tm_min);
            }
            std::strncpy(time_buffer, formatted.c_str(), sizeof(time_buffer) - 1);
            time_buffer[sizeof(time_buffer) - 1] = '\0'; // Ensure null termination
        }
        
        std::chrono::seconds get_time_remaining(std::chrono::system_clock::time_point current_time) const {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(target_time - current_time);
            return duration > std::chrono::seconds(0) ? duration : std::chrono::seconds(0);
        }
        
        // Draw progress blocks in a vertical layout
        void draw_vertical_progress_blocks(std::chrono::seconds time_remaining) {
            auto const pos = ImGui::GetWindowPos();
            auto const content_width = ImGui::GetWindowWidth() - 20.0f; // Padding
            
            auto dd = ImGui::GetWindowDrawList();
            
            // Let's find out the remaining available height for blocks.
            // In ImGui, ImGui::GetContentRegionAvail().y gives the height from the current cursor position
            // to the bottom of the window content area.
            // But we must also leave room for the "Set" collapsing header, which is drawn after this!
            // We know the height of the collapsing header from the last frame (stored in set_header_height).
            float spacing = ImGui::GetStyle().ItemSpacing.y;
            float blocks_avail_height = ImGui::GetContentRegionAvail().y - set_header_height - spacing - 5.0f;
            
            const float block_height = 24.0f;
            const float block_padding = 4.0f;
            
            // Calculate how many blocks can fit in the available height (limited to range [3, 24])
            int num_blocks = static_cast<int>(std::floor(blocks_avail_height / (block_height + block_padding)));
            num_blocks = std::clamp(num_blocks, 3, 24);
            
            // Adaptive block visualization based on remaining time
            struct BlockInterval {
                double seconds;
                std::string suffix;
                double divisor;
            };

            static const std::vector<BlockInterval> intervals = {
                // Seconds
                { 1.0, "s", 1.0 },
                { 2.0, "s", 1.0 },
                { 5.0, "s", 1.0 },
                { 10.0, "s", 1.0 },
                { 15.0, "s", 1.0 },
                { 30.0, "s", 1.0 },
                // Minutes
                { 60.0, "m", 60.0 },
                { 120.0, "m", 60.0 },
                { 300.0, "m", 60.0 },
                { 600.0, "m", 60.0 },
                { 900.0, "m", 60.0 },
                { 1800.0, "m", 60.0 },
                // Hours
                { 3600.0, "h", 3600.0 },
                { 7200.0, "h", 3600.0 },
                { 10800.0, "h", 3600.0 },
                { 14400.0, "h", 3600.0 },
                { 21600.0, "h", 3600.0 },
                { 43200.0, "h", 3600.0 },
                { 86400.0, "h", 3600.0 }
            };

            double total_seconds = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count());
            
            // Find the best interval
            BlockInterval chosen_interval = intervals.front();
            for (const auto& interval : intervals) {
                chosen_interval = interval;
                if (static_cast<double>(num_blocks) * interval.seconds >= total_seconds) {
                    break;
                }
            }
            
            // Draw blocks vertically
            const float block_width = content_width * 0.9f;
            const float start_x = pos.x + (ImGui::GetWindowWidth() - block_width) * 0.5f;
            float start_y = ImGui::GetCursorScreenPos().y;
            
            // Calculate how many blocks to fill
            double blocks_to_fill = total_seconds / chosen_interval.seconds;
            int complete_blocks = static_cast<int>(std::floor(blocks_to_fill));
            float partial_block_pct = static_cast<float>(blocks_to_fill - complete_blocks);
            
            // Cap complete blocks to num_blocks
            if (complete_blocks >= num_blocks) {
                complete_blocks = num_blocks;
                partial_block_pct = 0.0f;
            }
            
            // Reserve space for the blocks
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (block_height + block_padding) * static_cast<float>(num_blocks) + 5.0f);
            
            // Draw blocks from bottom to top (lowest time value at the bottom)
            for (int i = 0; i < num_blocks; ++i) {
                float block_y = start_y + static_cast<float>(num_blocks - 1 - i) * (block_height + block_padding);
                
                // Draw outline
                dd->AddRect(
                    ImVec2(start_x, block_y),
                    ImVec2(start_x + block_width, block_y + block_height),
                    ImGui::ColorConvertFloat4ToU32(colors[3]),
                    4.0f
                );
                
                // Fill completed blocks
                if (i < complete_blocks) {
                    // Fully fill this block
                    dd->AddRectFilled(
                        ImVec2(start_x, block_y),
                        ImVec2(start_x + block_width, block_y + block_height),
                        ImGui::ColorConvertFloat4ToU32(colors[0]),
                        4.0f
                    );
                } else if (i == complete_blocks && partial_block_pct > 0.0f) {
                    // Partially fill this block
                    auto color {colors[2]};
                    // Adjust alpha for a breathing effect
                    static int breath_passe = 0;
                    color.w = 0.65f + 0.35f * std::sin(0.02f * static_cast<float>(++breath_passe));
                    dd->AddRectFilled(
                        ImVec2(start_x, block_y + block_height * (1.0f - partial_block_pct)),
                        ImVec2(start_x + block_width, block_y + block_height),
                        ImGui::ColorConvertFloat4ToU32(color),
                        4.0f
                    );
                }
                
                // Add the block time label
                double label_val_raw = static_cast<double>(i + 1) * chosen_interval.seconds / chosen_interval.divisor;
                int label_val = static_cast<int>(std::round(label_val_raw));
                std::string time_label = std::format("{}{}", label_val, chosen_interval.suffix);
                
                // Center the text in the block
                auto text_size = ImGui::CalcTextSize(time_label.c_str());
                dd->AddText(
                    ImVec2(start_x + block_width / 2 - text_size.x / 2, 
                         block_y + block_height / 2 - text_size.y / 2),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)),
                    time_label.c_str()
                );
            }
        }
        
        std::string get_uri() const override {
            // Support for URI parameters like alarm:14:30
            if (time_buffer[0] != '\0') {
                return "alarm:" + std::string(time_buffer);
            }
            return "alarm";
        }

        std::vector<mcp_function> get_mcp_functions() const override {
            return {
                mcp_function(
                    "create_alarm",
                    "Create a new alarm for a specific date and time. The datetime should be in ISO format 'YYYY-MM-DDTHH:MM:SS' or 'YYYY-MM-DD HH:MM'. An alarm card will be automatically created in the deck.",
                    R"mcp({"type":"object","properties":{"datetime":{"type":"string","description":"The target date and time. Can be full ISO format 'YYYY-MM-DDTHH:MM:SS' or 'YYYY-MM-DD HH:MM'. For relative times (e.g. 'in 20 minutes') or natural language (e.g. '5pm'), calculate the exact future date/time first based on the current local time."}},"required":["datetime"]})mcp",
                    [](const std::string& params) -> std::string {
                        std::string datetime;
                        
                        if (!params.empty()) {
                            mcp_create_alarm_params request{};
                            auto parse_result = glz::read_json(request, params);
                            if (!parse_result && request.datetime) {
                                datetime = *request.datetime;
                            }
                        }
                        
                        if (datetime.empty()) {
                            return R"({"status":"error","message":"Missing required 'datetime' parameter"})";
                        }
                        
                        try {
                            auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                            std::string card_uri = "alarm:" + datetime;
                            (*create_card_fn)(card_uri);
                            
                            return std::format(R"({{"status":"success","message":"Alarm successfully set for {}"}})", datetime);
                        } catch (const std::exception& e) {
                            return std::format(R"({{"status":"error","message":"create_card service is not available: {}"}})", e.what());
                        }
                    }
                )
            };
        }

        ~alarm() override {
            media_player_alarm_helper::stop_sound_loop();
            alarm_playing = false;
        }

    private:
        std::chrono::system_clock::time_point target_time;
        char time_buffer[32] = {0};
        bool alarm_playing = false;
        bool alarm_active = true;
        float set_header_height = 30.0f; // Track the height of the "Set" collapsing header
        
        // Available alarm sounds
        std::vector<std::pair<std::string, std::string>> alarm_sounds = {
            {"Classic Beep", "img/alarm.mp3"},
            {"Modern Alert", "img/alarm_classic.wav"},
            {"Simple Tone", "img/new_alarm.wav"}
        };
        int selected_alarm_sound = 0;
    };
}
