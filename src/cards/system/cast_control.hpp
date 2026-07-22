#pragma once

#include <chrono>
#include <format>
#include <memory>
#include <string>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../hosts/video_feed_host.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

#ifndef _WIN32
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#endif

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
        bool starting = host->is_starting();

        // Status Header
        ui.text_colored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "📹 ROUEN CAST & VIDEO FEED");
        ui.separator();
        ui.spacing();

        // Stream Status Badge & Controls
        if (running || starting) {
            if (running) {
                ui.text_colored(ImVec4(0.3f, 0.95f, 0.4f, 1.0f), "● STREAM LIVE (TCP)");
            } else {
                ui.text_colored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "● WAITING FOR CLIENT (TCP)...");
            }
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
                    const_cast<char*>("--hwdec=auto"),
                    const_cast<char*>("--ytdl=no"),
                    const_cast<char*>("--cache=yes"),
                    const_cast<char*>("--cache-secs=5"),
                    const_cast<char*>("--demuxer-readahead-secs=5"),
                    const_cast<char*>("--demuxer-max-bytes=104857600"),
                    nullptr
                };

                posix_spawn_file_actions_t actions;
                posix_spawn_file_actions_init(&actions);
                posix_spawn_file_actions_addopen(&actions, 1, "/tmp/rouen_mpv_spawn.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                posix_spawn_file_actions_adddup2(&actions, 1, 2);

                pid_t pid;
                int res = ::posix_spawnp(&pid, mpv_bin.c_str(), &actions, nullptr, argv, environ);
                posix_spawn_file_actions_destroy(&actions);

                if (res == 0) {
                    try { "notify"_sfn(std::format("Launched MPV player for cast stream (PID {})", pid)); } catch (...) {}
                } else {
                    try { "notify"_sfn(std::format("Failed to launch MPV (POSIX error {})", res)); } catch (...) {}
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
            rouen::helpers::ConfigService::instance()->set_env_value("CAST_SHOW_HEADER", show_hdr ? "true" : "false", true);
        }

        bool show_ftr = host->show_footer.load();
        if (ImGui::Checkbox("Bottom Footer Bar (Live Clock & Frame Count)", &show_ftr)) {
            host->show_footer.store(show_ftr);
            rouen::helpers::ConfigService::instance()->set_env_value("CAST_SHOW_FOOTER", show_ftr ? "true" : "false", true);
        }

        bool show_bg = host->show_bg_animation.load();
        if (ImGui::Checkbox("Background Animation (Hue Sweep & Bouncing Square)", &show_bg)) {
            host->show_bg_animation.store(show_bg);
            rouen::helpers::ConfigService::instance()->set_env_value("CAST_SHOW_BG_ANIMATION", show_bg ? "true" : "false", true);
        }

        bool show_cards = host->show_card_overlays.load();
        if (ImGui::Checkbox("Active Card Overlays (e.g. Alarm Card HUD)", &show_cards)) {
            host->show_card_overlays.store(show_cards);
            rouen::helpers::ConfigService::instance()->set_env_value("CAST_SHOW_CARD_OVERLAYS", show_cards ? "true" : "false", true);
        }

        bool noise_enabled = host->enable_pink_noise.load();
        if (ImGui::Checkbox("Enable Ambient Pink Noise Fallback", &noise_enabled)) {
            host->enable_pink_noise.store(noise_enabled);
            rouen::helpers::ConfigService::instance()->set_env_value("CAST_ENABLE_PINK_NOISE", noise_enabled ? "true" : "false", true);
        }

        bool fs_media = host->full_screen_media.load();
        if (ImGui::Checkbox("Full Screen Media Playback", &fs_media)) {
            host->full_screen_media.store(fs_media);
            rouen::helpers::ConfigService::instance()->set_env_value("CAST_FULL_SCREEN_MEDIA", fs_media ? "true" : "false", true);
        }



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
