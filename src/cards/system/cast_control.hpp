#pragma once

#include <chrono>
#include <format>
#include <memory>
#include <string>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../hosts/video_feed_host.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class cast_control : public card {
public:
    cast_control() {
        colors[0] = ImVec4{0.2f, 0.6f, 0.9f, 1.0f};  // Primary accent
        colors[1] = ImVec4{0.15f, 0.2f, 0.3f, 0.8f}; // Card bg
        name("Cast & Video Feed Control");
        width = 460.0f;
    }

    std::string get_uri() const override {
        return "cast-control";
    }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            render_cast_control_content(ui);
        });
    }

private:
    void render_cast_control_content(rouen::ui::ui_context& ui) {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        if (!host) {
            ui.text_colored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Video feed service unavailable");
            return;
        }

        bool running = host->is_running();

        // Status Header
        ui.text_colored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "📹 ROUEN CAST & VIDEO FEED");
        ui.separator();
        ui.spacing();

        // Stream Status Badge & Controls
        if (running) {
            ui.text_colored(ImVec4(0.3f, 0.95f, 0.4f, 1.0f), "● STREAM LIVE (30 FPS)");
            ui.text(std::format("Endpoint: {}", host->endpoint()));

            if (ImGui::Button("Stop Video Stream", ImVec2(200, 32))) {
                host->stop();
            }
            ImGui::SameLine();
            if (ImGui::Button("Launch Player (mpv)", ImVec2(200, 32))) {
                std::string mpv_bin = "mpv";
                rouen::platform::check_mpv_availability(mpv_bin);
                std::string ep = host->endpoint();
#ifndef _WIN32
                ::setenv("PATH", "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin", 1);
                char* argv[] = {
                    const_cast<char*>(mpv_bin.c_str()),
                    const_cast<char*>(ep.c_str()),
                    const_cast<char*>("--title=Rouen Live Video Cast"),
                    nullptr
                };
                pid_t pid;
                if (::posix_spawnp(&pid, mpv_bin.c_str(), nullptr, nullptr, argv, environ) == 0) {
                    try { "notify"_sfn(std::format("Launched MPV player for cast stream (PID {})", pid)); } catch (...) {}
                } else {
                    try { "notify"_sfn("Failed to launch MPV player"); } catch (...) {}
                }
#endif
            }
        } else {
            ui.text_colored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "○ STREAM STOPPED");
            ui.text(std::format("Target Port: {}", host->port()));

            if (ImGui::Button("Start Video Stream", ImVec2(200, 32))) {
                host->start();
            }
        }

        ui.spacing();
        ui.separator();
        ui.spacing();

        // Section Toggles
        ui.text_colored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "OVERLAY SECTIONS CONTROL");
        ui.spacing();

        bool show_hdr = host->show_header.load();
        if (ImGui::Checkbox("Top Header Banner ('ROUEN MULTI-MODAL UI')", &show_hdr)) {
            host->show_header.store(show_hdr);
        }

        bool show_ftr = host->show_footer.load();
        if (ImGui::Checkbox("Bottom Footer Bar (Live Clock & Frame Count)", &show_ftr)) {
            host->show_footer.store(show_ftr);
        }

        bool show_bg = host->show_bg_animation.load();
        if (ImGui::Checkbox("Background Animation (Hue Sweep & Bouncing Square)", &show_bg)) {
            host->show_bg_animation.store(show_bg);
        }

        bool show_cards = host->show_card_overlays.load();
        if (ImGui::Checkbox("Active Card Overlays (e.g. Alarm Card HUD)", &show_cards)) {
            host->show_card_overlays.store(show_cards);
        }

        bool noise_enabled = host->enable_pink_noise.load();
        if (ImGui::Checkbox("Enable Ambient Pink Noise Fallback", &noise_enabled)) {
            host->enable_pink_noise.store(noise_enabled);
        }

        bool fs_media = host->full_screen_media.load();
        if (ImGui::Checkbox("Full Screen Media Playback", &fs_media)) {
            host->full_screen_media.store(fs_media);
        }

        int delay = host->audio_delay_ms.load();
        if (ImGui::SliderInt("Audio Sync Offset (ms)", &delay, -2000, 2000, "%d ms")) {
            host->audio_delay_ms.store(delay);
        }
        ImGui::TextDisabled("Slide left (negative) to delay video; slide right (positive) to delay audio.");

        ui.spacing();
        ui.separator();
        ui.spacing();

        // Live Preview Section
        ui.text_colored(ImVec4(0.8f, 0.8f, 0.85f, 1.0f), "LIVE STREAM PREVIEW");
        ui.spacing();

        ImTextureID tex_id = host->get_texture_id();
        if (tex_id) {
            ImGui::Image(tex_id, ImVec2(420.0f, 236.25f)); // 16:9 aspect ratio preview
        } else {
            ImGui::BeginChild("##PreviewPlaceholder", ImVec2(420.0f, 236.25f), true);
            ImGui::SetCursorPosY(100.0f);
            ImGui::SetCursorPosX(110.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stream preview inactive");
            ImGui::EndChild();
        }
    }
};

} // namespace rouen::cards
