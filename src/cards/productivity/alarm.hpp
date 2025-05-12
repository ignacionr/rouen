#pragma once

#include <chrono>
#include <cmath>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>

#include "imgui.h"

#include "../interface/card.hpp"

namespace rouen::cards {
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
            // Add card blinking effect when alarm is going off
            auto time_remaining = get_time_remaining(std::chrono::system_clock::now());
            
            if (time_remaining <= std::chrono::seconds(0)) {
                // When alarm is going off, blink the entire card with a 1-second interval
                static auto last_blink_time = std::chrono::steady_clock::now();
                static bool is_visible = true;
                
                auto now = std::chrono::steady_clock::now();
                auto time_since_blink = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_blink_time).count();
                
                // Blink every 1000ms (1 second)
                if (time_since_blink >= 1000) {
                    is_visible = !is_visible;
                    last_blink_time = now;
                }
                
                // Make the card background blink by changing its alpha
                if (!is_visible) {
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.9f, 0.3f, 0.3f, 0.9f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.8f, 0.0f, 0.0f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.8f, 0.0f, 0.0f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.9f, 0.1f, 0.1f, 0.7f));
                }
                
                // Ensure we return the stack to normal state
                bool result = render_window([this]() {
                    render_alarm_content();
                });
                
                ImGui::PopStyleColor(3);
                return result;
            }
            
            // Normal rendering when alarm is not going off
            return render_window([this]() {
                render_alarm_content();
            });
        }
        
        // Separated the actual content rendering to avoid duplication
        void render_alarm_content() {
            auto const current_time = std::chrono::system_clock::now();
            
            ImGui::TextUnformatted(time_buffer);
            ImGui::SameLine();
                        
            // Show alarm status
            auto time_remaining = get_time_remaining(current_time);
            
            if (time_remaining <= std::chrono::seconds(0)) {
                // Center the alarm notification
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("Time's up!").x) * 0.5f);
                ImGui::TextColored(colors[0], "Time's up!");
                
                // Flashing effect for alarm text
                static float flash_intensity = 0.0f;
                static float flash_direction = 1.0f;
                
                flash_intensity += flash_direction * 0.05f;
                if (flash_intensity >= 1.0f || flash_intensity <= 0.0f) {
                    flash_direction *= -1.0f;
                }
                
                // Make the alarm text extra large and centered
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Use a larger font
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("ALARM").x) * 0.5f);
                ImGui::TextColored(
                    ImVec4(1.0f, flash_intensity, flash_intensity, 1.0f),
                    "ALARM"
                );
                ImGui::PopFont();
            } else {
                // Show remaining time in hours, minutes, seconds - centered
                auto hours = std::chrono::duration_cast<std::chrono::hours>(time_remaining).count();
                auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time_remaining).count() % 60;
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count() % 60;
                
                // Make the time display larger and centered using std::format (C++20)
                // This eliminates platform-specific format specifier issues
                std::string time_str = std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
                
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Use a larger font
                ImGui::TextColored(colors[2], "%s", time_str.c_str());
                ImGui::PopFont();
                
                // Visual progress indicator with stacked blocks
                draw_vertical_progress_blocks(time_remaining);
            }

            // Time setting interface - now vertical layout
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
            }
            
        }
        
        void reset() {
            // Reset to current time
            set_to_current_time();
            // Add default time (30 minutes)
            add_minutes(30);
        }
        
        void set_to_current_time() {
            auto current_time = std::chrono::system_clock::now();
            target_time = current_time;
            update_time_string();
        }
        
        void add_minutes(int minutes) {
            target_time += std::chrono::minutes(minutes);
            update_time_string();
        }
        
        void parse_time(const std::string& time_str) {
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
                char delimiter;
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
                
                // Set the target time to the specified hour and minute of today
                current_tm.tm_hour = hours;
                current_tm.tm_min = minutes;
                current_tm.tm_sec = 0;
                
                auto new_target = std::chrono::system_clock::from_time_t(std::mktime(&current_tm));
                
                // If the target time is in the past, add 24 hours
                if (new_target < current_time) {
                    new_target += std::chrono::hours(24);
                }
                
                target_time = new_target;
                update_time_string();
            }
        }
        
        void update_time_string() {
            auto time_t = std::chrono::system_clock::to_time_t(target_time);
            std::tm time_tm = *std::localtime(&time_t);
            
            // Use std::format instead of std::snprintf for type safety and modern C++ style
            std::string formatted = std::format("{:02d}:{:02d}", time_tm.tm_hour, time_tm.tm_min);
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
            
            // Adaptive block visualization based on remaining time
            int num_blocks = 6;  // Default: 6 blocks of 10 minutes (1 hour)
            int minutes_per_block = 10;
            std::string label_suffix = "min";
            
            auto total_minutes = std::chrono::duration_cast<std::chrono::minutes>(time_remaining).count();
            
            if (total_minutes > 60) {
                // For times > 1 hour, show in 1-hour blocks
                num_blocks = std::min(static_cast<int>(total_minutes / 60) + 1, 8); // Limit to 8 blocks
                minutes_per_block = 60;
                label_suffix = "hr";
            } else if (total_minutes <= 5) {
                // For times <= 5 minutes, show in 1-minute blocks
                num_blocks = 5;
                minutes_per_block = 1;
                label_suffix = "min";
            } else if (total_minutes <= 30) {
                num_blocks = std::min(6, static_cast<int>(total_minutes / 5) + 1); // Adapt to available time
                minutes_per_block = 5;
                label_suffix = "min";
            } else {
                // Default: 5-minute blocks
                num_blocks = std::min(6, static_cast<int>(total_minutes / 6) + 1); // Adapt to available time
                minutes_per_block = 10;
                label_suffix = "min";
            }
            
            // Draw blocks vertically
            const float block_height = 24.0f;
            const float block_width = content_width * 0.9f;
            const float block_padding = 4.0f;
            const float start_x = pos.x + (ImGui::GetWindowWidth() - block_width) * 0.5f;
            float start_y = ImGui::GetCursorScreenPos().y;
            
            // Calculate how many blocks to fill
            float blocks_to_fill = 0.0f;
            
            if (minutes_per_block == 60) {
                // For hour blocks
                blocks_to_fill = static_cast<float>(total_minutes) / static_cast<float>(minutes_per_block);
            } else if (minutes_per_block == 10) {
                // For 5-minute blocks (only count up to 1 hour)
                auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count();
                blocks_to_fill = std::min(static_cast<float>(total_seconds), 3600.0f) / (static_cast<float>(minutes_per_block) * 60.0f);
            } else if (minutes_per_block == 5) {
                // For 5-minute blocks (only count up to 30 minutes)
                auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count();
                blocks_to_fill = std::min(static_cast<float>(total_seconds), 3600.0f) / (static_cast<float>(minutes_per_block) * 60.0f);
            } else {
                // For 1-minute blocks (only count up to 5 minutes)
                auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remaining).count();
                blocks_to_fill = std::min(static_cast<float>(total_seconds), 300.0f) / 60.0f;
            }
            
            auto complete_blocks = static_cast<int>(blocks_to_fill);
            auto partial_block_pct = blocks_to_fill - static_cast<float>(complete_blocks);
            
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
                std::string time_label;
                int label_value = (i + 1) * minutes_per_block;
                
                if (minutes_per_block == 60) {
                    // For hour blocks, show as "1hr", "2hr", etc.
                    time_label = std::format("{}{}", (i + 1), label_suffix);
                } else {
                    // For minute blocks, show as "5", "10", etc.
                    time_label = std::format("{}", label_value);
                }
                
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

    private:
        std::chrono::system_clock::time_point target_time;
        char time_buffer[16] = {0}; // C-style array instead of std::array
    };
}