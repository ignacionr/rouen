#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "../../helpers/imgui_include.hpp"

#include "../interface/card.hpp"
#include "../../models/radio.hpp"
#include "../../helpers/media_player.hpp"
#include "../../helpers/vu_meter.hpp"

namespace rouen::cards {
    
    class radio : public card {
    public:
        radio() {
            // Set custom colors for the radio card
            colors[0] = {0.5f, 0.3f, 0.7f, 1.0f}; // Purple primary color
            colors[1] = {0.4f, 0.2f, 0.6f, 0.7f}; // Darker purple secondary color
            
            // Additional colors for status indicators
            get_color(2, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Green for playing
            get_color(3, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red for errors
            
            name("Radio");
            requested_fps = 10;  // Higher FPS for smooth dial and needle animations
            width = 390.0f;      // Made the card width 30% wider (default is 300)
            
            // Create radio model
            radio_model = std::make_unique<rouen::models::radio>();
        }
        
        ~radio() override {
            // Ensure the radio is stopped when the card is destroyed
            if (radio_model) {
                radio_model->stopCurrentStation();
            }
        }
        
        std::string get_uri() const override
        {
            return "radio";
        }

        bool render() override {
            return render_window([this]() {
                if (!radio_model) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Radio model not initialized");
                    return;
                }
                
                // Initialize volume knob position from model
                if (!initialized_volume) {
                    volume_knob_val = static_cast<float>(radio_model->getVolume()) / 100.0f;
                    saved_volume = radio_model->getVolume();
                    initialized_volume = true;
                }
                
                const std::string& current_playing_station = radio_model->getCurrentStation();
                const std::vector<std::string>& all_stations = radio_model->getStationNames();
                
                // Apply search filter to presets
                static char search_buffer[256] = "";
                std::string search_text = search_buffer;
                std::transform(search_text.begin(), search_text.end(), search_text.begin(),
                              [](unsigned char c) { return std::tolower(c); });
                              
                std::vector<std::string> visible_stations;
                for (const auto& name : all_stations) {
                    std::string lower_name = name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                                  [](unsigned char c) { return std::tolower(c); });
                    if (search_text.empty() || lower_name.find(search_text) != std::string::npos) {
                        visible_stations.push_back(name);
                    }
                }
                
                // Make vertical dial fill the available height
                float dial_height = std::max(340.0f, ImGui::GetContentRegionAvail().y - 15.0f);
                
                // Draw 2-column layout: Left = Vintage Dial, Right = Controls & VU
                if (ImGui::BeginTable("RadioLayoutTable", 2, ImGuiTableFlags_None)) {
                    ImGui::TableSetupColumn("DialColumn", ImGuiTableColumnFlags_WidthFixed, 240.0f); // Wider column
                    ImGui::TableSetupColumn("ControlsColumn", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableNextRow();
                    
                    // --- COLUMN 0: VINTAGE DIAL SCALE ---
                    ImGui::TableSetColumnIndex(0);
                    
                    ImVec2 dial_size(220.0f, dial_height); // Wider dial size
                    ImVec2 dial_pos = ImGui::GetCursorScreenPos();
                    
                    // Transparent button for drag interaction
                    ImGui::InvisibleButton("##DialClickTarget", dial_size);
                    bool dial_active = ImGui::IsItemActive();
                    
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    
                    // Drag tuning interaction
                    if (dial_active && !visible_stations.empty()) {
                        ImGuiIO& io = ImGui::GetIO();
                        float relative_y = (io.MousePos.y - dial_pos.y - 20.0f) / (dial_size.y - 40.0f);
                        relative_y = std::max(0.0f, std::min(1.0f, relative_y));
                        tuning_knob_val = relative_y;
                        target_needle_pos = relative_y;
                        was_dragging = true;
                    }
                    
                    // Draw outer bezel (Bakelite styling)
                    draw_list->AddRectFilled(dial_pos, ImVec2(dial_pos.x + dial_size.x, dial_pos.y + dial_size.y), IM_COL32(42, 28, 16, 255), 10.0f);
                    draw_list->AddRect(dial_pos, ImVec2(dial_pos.x + dial_size.x, dial_pos.y + dial_size.y), IM_COL32(80, 54, 30, 255), 10.0f, 0, 2.5f);
                    
                    // Smoothly transition backlight intensity (grows to 1.0 when active, dims to 0.22 when stopped)
                    bool playing_active = !current_playing_station.empty() && radio_model->isPlaying();
                    float target_light = playing_active ? 1.0f : 0.22f;
                    current_light += (target_light - current_light) * 0.12f;
                    
                    // Inner glowing amber scale (intensity-controlled)
                    ImVec2 scale_min(dial_pos.x + 10.0f, dial_pos.y + 15.0f);
                    ImVec2 scale_max(dial_pos.x + dial_size.x - 10.0f, dial_pos.y + dial_size.y - 15.0f);
                    
                    ImU32 col_left = IM_COL32(
                        static_cast<int>(200.0f * current_light),
                        static_cast<int>(90.0f * current_light),
                        static_cast<int>(10.0f * current_light),
                        255
                    );
                    ImU32 col_right = IM_COL32(
                        static_cast<int>(235.0f * current_light),
                        static_cast<int>(140.0f * current_light),
                        static_cast<int>(20.0f * current_light),
                        255
                    );
                    
                    draw_list->AddRectFilledMultiColor(scale_min, scale_max, col_left, col_right, col_right, col_left);
                    draw_list->AddRect(scale_min, scale_max, IM_COL32(50, 25, 5, 255), 0.0f, 0, 1.5f);
                    
                    float track_h = scale_max.y - scale_min.y - 40.0f;
                    float track_top = scale_min.y + 20.0f;
                    
                    // Scale contrast modifier based on backlight state
                    float contrast = 0.4f + current_light * 0.6f;
                    
                    // Draw dial ticks & frequency markers (FM: 88-108 MHz)
                    for (int i = 0; i <= 20; ++i) {
                        float t = static_cast<float>(i) / 20.0f;
                        float y_val = track_top + t * track_h;
                        
                        bool is_major = (i % 5 == 0);
                        float tick_len = is_major ? 12.0f : 6.0f;
                        
                        draw_list->AddLine(
                            ImVec2(scale_min.x + 5.0f, y_val),
                            ImVec2(scale_min.x + 5.0f + tick_len, y_val),
                            IM_COL32(30, 15, 0, static_cast<int>(220.0f * contrast)),
                            is_major ? 1.8f : 1.0f
                        );
                        
                        if (is_major) {
                            int mhz = 88 + (20 - i) * 1;
                            char freq_str[8];
                            snprintf(freq_str, sizeof(freq_str), "%d", mhz);
                            draw_list->AddText(
                                ImGui::GetFont(),
                                ImGui::GetFontSize() * 0.7f,
                                ImVec2(scale_min.x + 20.0f, y_val - 6.0f),
                                IM_COL32(40, 15, 0, static_cast<int>(240.0f * contrast)),
                                freq_str
                            );
                        }
                    }
                    
                    // Find the single preset closest to the mouse y-position for click/hover
                    int hover_idx = -1;
                    float min_mouse_dist = 999.0f;
                    ImGuiIO& io = ImGui::GetIO();
                    
                    bool mouse_in_labels_x = (io.MousePos.x >= scale_max.x - 145.0f && io.MousePos.x <= scale_max.x - 5.0f);
                    bool mouse_in_dial_y = (io.MousePos.y >= scale_min.y && io.MousePos.y <= scale_max.y);
                    
                    if (mouse_in_labels_x && mouse_in_dial_y && !visible_stations.empty()) {
                        for (size_t idx = 0; idx < visible_stations.size(); ++idx) {
                            float t_preset = (visible_stations.size() > 1) ? (static_cast<float>(idx) / static_cast<float>(visible_stations.size() - 1)) : 0.5f;
                            float y_val = track_top + t_preset * track_h;
                            float dist = fabsf(io.MousePos.y - y_val);
                            if (dist < min_mouse_dist) {
                                min_mouse_dist = dist;
                                hover_idx = static_cast<int>(idx);
                            }
                        }
                    }
                    
                    // Draw preset station tags
                    int closest_idx = -1;
                    float closest_dist = 999.0f;
                    
                    if (!visible_stations.empty()) {
                        int num_presets = static_cast<int>(visible_stations.size());
                        for (size_t idx = 0; idx < static_cast<size_t>(num_presets); ++idx) {
                            float t_preset = (num_presets > 1) ? (static_cast<float>(idx) / static_cast<float>(num_presets - 1)) : 0.5f;
                            float y_val = track_top + t_preset * track_h;
                            
                            const std::string& name = visible_stations[idx];
                            bool is_playing = (name == current_playing_station);
                            
                            float dist = fabsf(target_needle_pos - t_preset);
                            if (dist < closest_dist) {
                                closest_dist = dist;
                                closest_idx = static_cast<int>(idx);
                            }
                            
                            // Make preset label interactive (clickable)
                            ImVec2 label_min(scale_max.x - 140.0f, y_val - 8.0f);
                            ImVec2 label_max(scale_max.x - 4.0f, y_val + 8.0f);
                            
                            if (hover_idx == static_cast<int>(idx)) {
                                // Draw a subtle hover glow
                                draw_list->AddRectFilled(label_min, label_max, IM_COL32(255, 255, 255, 35), 3.0f);
                                
                                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                    target_needle_pos = t_preset;
                                    tuning_knob_val = t_preset;
                                    radio_model->playStation(name);
                                    radio_model->setVolume(saved_volume);
                                    was_dragging = false; // Prevent drag release override
                                }
                            }
                            
                            // Playing station uses dark amber/brown color
                            ImU32 indicator_color = is_playing ? IM_COL32(90, 45, 10, 255) : IM_COL32(50, 20, 5, static_cast<int>(180.0f * contrast));
                            
                            draw_list->AddCircleFilled(
                                ImVec2(scale_max.x - 135.0f, y_val),
                                is_playing ? 4.0f : 2.5f,
                                indicator_color
                            );
                            
                            std::string display_name = name;
                            if (display_name.length() > 18) {
                                display_name = display_name.substr(0, 17) + ".";
                            }
                            
                            ImVec2 text_pos(scale_max.x - 126.0f, y_val - 6.0f);
                            draw_list->AddText(
                                ImGui::GetFont(),
                                ImGui::GetFontSize() * 0.65f,
                                text_pos,
                                indicator_color,
                                display_name.c_str()
                            );
                        }
                    }
                    
                    // Single closest station snapping (applied outside the loop)
                    if (dial_active && closest_idx >= 0 && closest_dist < 0.030f) {
                        float t_snap = (visible_stations.size() > 1) ? (static_cast<float>(closest_idx) / static_cast<float>(visible_stations.size() - 1)) : 0.5f;
                        target_needle_pos = t_snap;
                        tuning_knob_val = t_snap;
                    }
                    
                    // Animate dial needle
                    current_needle_pos += (target_needle_pos - current_needle_pos) * 0.25f;
                    
                    // Draw red indicator line (Needle)
                    float needle_y = track_top + current_needle_pos * track_h;
                    draw_list->AddLine(
                        ImVec2(scale_min.x + 2.0f, needle_y),
                        ImVec2(scale_max.x - 2.0f, needle_y),
                        IM_COL32(230, 20, 20, static_cast<int>(255.0f * (0.6f + current_light * 0.4f))),
                        2.2f
                    );
                    
                    draw_list->AddCircleFilled(
                        ImVec2(scale_min.x + (scale_max.x - scale_min.x) * 0.28f, needle_y),
                        5.0f,
                        IM_COL32(230, 20, 20, static_cast<int>(255.0f * (0.6f + current_light * 0.4f)))
                    );
                    draw_list->AddCircle(
                        ImVec2(scale_min.x + (scale_max.x - scale_min.x) * 0.28f, needle_y),
                        5.0f,
                        IM_COL32(255, 255, 255, static_cast<int>(180.0f * (0.6f + current_light * 0.4f))),
                        0,
                        1.0f
                    );
                    
                    // Glass glare highlight
                    draw_list->AddTriangleFilled(
                        scale_min,
                        ImVec2(scale_max.x - 30.0f, scale_min.y),
                        ImVec2(scale_min.x, scale_max.y - 50.0f),
                        IM_COL32(255, 255, 255, static_cast<int>(25.0f * (0.4f + current_light * 0.6f)))
                    );
                    
                    // --- COLUMN 1: CONTROL PANEL ---
                    ImGui::TableSetColumnIndex(1);
                    
                    // Signal / VU meter state calculation
                    bool tuning_active = (fabsf(target_needle_pos - current_needle_pos) > 0.005f);
                    
                    if (playing_active) {
                        float noise = (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * 0.015f;
                        target_signal = 0.85f + noise;
                    } else if (tuning_active) {
                        target_signal = 0.15f + (static_cast<float>(rand() % 100) / 100.0f) * 0.20f; // Jitter static noise
                    } else {
                        target_signal = 0.0f;
                    }
                    current_signal += (target_signal - current_signal) * 0.15f;
                    
                    // Draw VU Meter (centered horizontally, fixed dimensions to prevent resizing bugs)
                    float avail_width = ImGui::GetContentRegionAvail().x;
                    float vu_w = 150.0f;
                    float vu_h = 80.0f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_width - vu_w) * 0.5f);
                    ImVec2 vu_pos = ImGui::GetCursorScreenPos();
                    ImVec2 vu_size(vu_w, vu_h);
                    draw_vu_meter(draw_list, vu_pos, vu_size, current_signal);
                    ImGui::Dummy(ImVec2(0.0f, vu_h + 5.0f));
                    
                    // Retro glowing LED Status display (stretches, text centered)
                    float led_w = ImGui::GetContentRegionAvail().x;
                    ImVec2 led_pos = ImGui::GetCursorScreenPos();
                    ImVec2 led_size(led_w, 50.0f);
                    draw_list->AddRectFilled(led_pos, ImVec2(led_pos.x + led_size.x, led_pos.y + led_size.y), IM_COL32(12, 28, 12, 255), 4.0f);
                    draw_list->AddRect(led_pos, ImVec2(led_pos.x + led_size.x, led_pos.y + led_size.y), IM_COL32(40, 50, 40, 255), 4.0f, 0, 1.5f);
                    
                    if (playing_active) {
                        std::string station_text = current_playing_station;
                        ImVec2 text_size = ImGui::CalcTextSize(station_text.c_str());
                        ImVec2 text_pos(led_pos.x + (led_w - text_size.x) * 0.5f, led_pos.y + 8.0f);
                        
                        draw_list->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 0.95f,
                            text_pos,
                            IM_COL32(50, 255, 50, 255),
                            station_text.c_str()
                        );
                        
                        char details_str[32];
                        snprintf(details_str, sizeof(details_str), "Tuned - FM %0.1f MHz", static_cast<double>(88.0f + (1.0f - current_needle_pos) * 20.0f));
                        ImVec2 det_size = ImGui::CalcTextSize(details_str);
                        ImVec2 det_pos(led_pos.x + (led_w - det_size.x) * 0.5f, led_pos.y + 30.0f);
                        
                        draw_list->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 0.65f,
                            det_pos,
                            IM_COL32(30, 200, 30, 220),
                            details_str
                        );
                    } else if (tuning_active) {
                        std::string tune_text = "TUNING...";
                        ImVec2 text_size = ImGui::CalcTextSize(tune_text.c_str());
                        ImVec2 text_pos(led_pos.x + (led_w - text_size.x) * 0.5f, led_pos.y + 16.0f);
                        
                        draw_list->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 0.95f,
                            text_pos,
                            IM_COL32(255, 180, 20, 255),
                            tune_text.c_str()
                        );
                    } else {
                        std::string off_text = "POWER OFF - STANDBY";
                        ImVec2 text_size = ImGui::CalcTextSize(off_text.c_str());
                        ImVec2 text_pos(led_pos.x + (led_w - text_size.x) * 0.5f, led_pos.y + 16.0f);
                        
                        draw_list->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 0.95f,
                            text_pos,
                            IM_COL32(100, 110, 100, 255),
                            off_text.c_str()
                        );
                    }
                    ImGui::Dummy(ImVec2(0.0f, 55.0f));
                    
                    // Search box
                    ImGui::PushItemWidth(-1);
                    ImGui::InputText("##search", search_buffer, static_cast<int>(sizeof(search_buffer)));
                    if (search_buffer[0] == '\0' && !ImGui::IsItemActive()) {
                        auto pos = ImGui::GetItemRectMin();
                        draw_list->AddText(
                            ImVec2(pos.x + 5, pos.y + 2),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            "Search presets..."
                        );
                    }
                    ImGui::PopItemWidth();
                    ImGui::Spacing();
                    
                    // Knobs Table
                    if (ImGui::BeginTable("KnobsTable", 2)) {
                        ImGui::TableNextRow();
                        
                        // Volume Knob
                        ImGui::TableSetColumnIndex(0);
                        ImVec2 knob_size(55.0f, 55.0f);
                        float vol_val = volume_knob_val;
                        
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - knob_size.x) * 0.5f);
                        if (draw_knob("VOLUME##VolKnob", &vol_val, 0.0f, 1.0f, knob_size, draw_list)) {
                            volume_knob_val = vol_val;
                            int vol_pct = static_cast<int>(vol_val * 100.0f);
                            radio_model->setVolume(vol_pct);
                            saved_volume = vol_pct;
                        }
                        
                        // Tuning Knob
                        ImGui::TableSetColumnIndex(1);
                        float tune_val = tuning_knob_val;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - knob_size.x) * 0.5f);
                        if (draw_knob("TUNING##TuneKnob", &tune_val, 0.0f, 1.0f, knob_size, draw_list)) {
                            tuning_knob_val = tune_val;
                            target_needle_pos = tune_val;
                            was_dragging = true;
                        }
                        
                        ImGui::EndTable();
                    }
                    
                    ImGui::Dummy(ImVec2(0.0f, 15.0f));
                    
                    // Stop / Power rocker button
                    ImGui::Spacing();
                    ImVec4 btn_color = playing_active ? colors[3] : colors[2];
                    ImGui::PushStyleColor(ImGuiCol_Button, btn_color);
                    
                    std::string power_btn_label = playing_active ? ICON_MD_POWER_SETTINGS_NEW " STOP RECEIVER" : ICON_MD_PLAY_ARROW " TUNE CLOSE STATION";
                    if (ImGui::Button(power_btn_label.c_str(), ImVec2(-1, 32.0f))) {
                        if (playing_active) {
                            radio_model->stopCurrentStation();
                        } else if (closest_idx >= 0 && closest_idx < static_cast<int>(visible_stations.size())) {
                            target_needle_pos = (visible_stations.size() > 1) ? (static_cast<float>(closest_idx) / static_cast<float>(visible_stations.size() - 1)) : 0.5f;
                            tuning_knob_val = target_needle_pos;
                            radio_model->playStation(visible_stations[static_cast<size_t>(closest_idx)]);
                            radio_model->setVolume(saved_volume);
                        }
                    }
                    ImGui::PopStyleColor();
                    
                    // Drag release activation
                    if (was_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                        was_dragging = false;
                        if (closest_idx >= 0 && closest_idx < static_cast<int>(visible_stations.size())) {
                            target_needle_pos = (visible_stations.size() > 1) ? (static_cast<float>(closest_idx) / static_cast<float>(visible_stations.size() - 1)) : 0.5f;
                            tuning_knob_val = target_needle_pos;
                            radio_model->playStation(visible_stations[static_cast<size_t>(closest_idx)]);
                            radio_model->setVolume(saved_volume);
                        }
                    }
                    
                    ImGui::EndTable();
                }
            });
        }
        
    private:
        std::unique_ptr<rouen::models::radio> radio_model;
        
        // Tuning state variables
        float current_needle_pos = 0.5f;
        float target_needle_pos = 0.5f;
        float current_signal = 0.0f;
        float target_signal = 0.0f;
        float tuning_knob_val = 0.5f;
        float volume_knob_val = 0.8f;
        bool was_dragging = false;
        bool initialized_volume = false;
        int saved_volume = 80;
        float current_light = 0.22f; // Backlight intensity variable (grows when playing, dims when stopped)

        // Vintage VU Signal strength meter draw helper
        void draw_vu_meter(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size, float signal) {
            rouen::helpers::vu_meter::VUMeterConfig config;
            config.scale_type = rouen::helpers::vu_meter::VUMeterScaleType::SignalStrength;
            config.title = "TUNING / SIGNAL";
            config.show_titles = true;
            config.style.theme = rouen::helpers::vu_meter::VUMeterTheme::VintageLitAmber;
            rouen::helpers::vu_meter::draw_analog_dial(draw_list, pos, size, signal, 0.0f, "", config);
        }

        // Rotary knob draw and interaction helper
        bool draw_knob(const char* label, float* value, float min_val, float max_val, const ImVec2& size, ImDrawList* draw_list) {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            
            ImGui::InvisibleButton(label, size);
            bool active = ImGui::IsItemActive();
            
            if (active) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                *value -= delta.y * 0.005f * (max_val - min_val);
                *value = std::max(min_val, std::min(max_val, *value));
            }
            
            float radius = size.x * 0.5f;
            ImVec2 center(pos.x + radius, pos.y + radius);
            
            // Draw clean knob without the background circle
            draw_list->AddCircleFilled(center, radius * 0.95f, IM_COL32(65, 60, 55, 255));
            draw_list->AddCircle(center, radius * 0.95f, IM_COL32(100, 95, 90, 255), 0, 1.0f);
            draw_list->AddCircleFilled(center, radius * 0.75f, IM_COL32(40, 35, 30, 255));
            draw_list->AddCircleFilled(center, radius * 0.4f, IM_COL32(180, 140, 40, 255));
            draw_list->AddCircleFilled(center, radius * 0.35f, IM_COL32(210, 180, 80, 255));
            
            float angle = 2.356f + ((*value - min_val) / (max_val - min_val)) * 3.927f;
            ImVec2 indicator(cosf(angle) * radius * 0.7f, sinf(angle) * radius * 0.7f);
            draw_list->AddLine(
                ImVec2(center.x + indicator.x * 0.3f, center.y + indicator.y * 0.3f),
                ImVec2(center.x + indicator.x, center.y + indicator.y),
                IM_COL32(230, 220, 210, 255),
                2.0f
            );
            
            std::string label_str = label;
            size_t hash_pos = label_str.find("##");
            if (hash_pos != std::string::npos) {
                label_str = label_str.substr(0, hash_pos);
            }
            ImVec2 text_size = ImGui::CalcTextSize(label_str.c_str());
            ImVec2 text_pos(center.x - text_size.x * 0.5f, center.y + radius + 3.0f);
            draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f, text_pos, IM_COL32(180, 180, 180, 255), label_str.c_str());
            
            return active;
        }
    };
    
}
