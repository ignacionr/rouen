#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include <glaze/glaze.hpp>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../hosts/video_feed_host.hpp"
#include "../../registrar.hpp"
#include "../../../external/IconsMaterialDesign.h"
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
        auto host = rouen::hosts::VideoFeedHost::get_host();
        if (host) {
            stream_endpoint_ = host->endpoint();
        }
    }

    std::string get_uri() const override {
        return "cast-control";
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";
        card["refreshIntervalMs"] = 2000;

        std::vector<glz::json_t> body;

        bool broadcasting = is_broadcasting();

        // 1. Header Container
        glz::json_t header;
        header["type"] = "Container";
        header["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> columns;

        // Column 1: Emoji icon
        glz::json_t icon_col;
        icon_col["type"] = "Column";
        icon_col["width"] = "auto";
        std::vector<glz::json_t> icon_items;
        glz::json_t icon_tb;
        icon_tb["type"] = "TextBlock";
        icon_tb["text"] = "📡";
        icon_tb["size"] = "Large";
        icon_items.push_back(std::move(icon_tb));
        icon_col["items"] = std::move(icon_items);
        columns.push_back(std::move(icon_col));

        // Column 2: Title & Subtitle
        glz::json_t title_col;
        title_col["type"] = "Column";
        title_col["width"] = "stretch";
        std::vector<glz::json_t> title_items;
        glz::json_t title_tb;
        title_tb["type"] = "TextBlock";
        title_tb["text"] = "Rouen Cast Server";
        title_tb["weight"] = "Bolder";
        title_tb["size"] = "Large";
        title_items.push_back(std::move(title_tb));

        glz::json_t subtitle_tb;
        subtitle_tb["type"] = "TextBlock";
        subtitle_tb["text"] = "Local video feed and hardware encoder";
        subtitle_tb["isSubtle"] = true;
        subtitle_tb["size"] = "Small";
        title_items.push_back(std::move(subtitle_tb));
        title_col["items"] = std::move(title_items);
        columns.push_back(std::move(title_col));

        // Column 3: Status Badge
        glz::json_t status_col;
        status_col["type"] = "Column";
        status_col["width"] = "auto";
        std::vector<glz::json_t> status_items;
        glz::json_t status_tb;
        status_tb["type"] = "TextBlock";
        if (broadcasting) {
            status_tb["text"] = "STREAMING";
            status_tb["weight"] = "Bolder";
            status_tb["color"] = "Good";
            status_tb["size"] = "Small";
        } else {
            status_tb["text"] = "STOPPED";
            status_tb["weight"] = "Bolder";
            status_tb["color"] = "Attention";
            status_tb["size"] = "Small";
        }
        status_items.push_back(std::move(status_tb));
        status_col["items"] = std::move(status_items);
        columns.push_back(std::move(status_col));

        col_set["columns"] = std::move(columns);

        std::vector<glz::json_t> header_items;
        header_items.push_back(std::move(col_set));
        header["items"] = std::move(header_items);
        body.push_back(std::move(header));

        // 2. Broadcast Configuration Container (Inputs)
        glz::json_t config_container;
        config_container["type"] = "Container";
        std::vector<glz::json_t> config_items;

        glz::json_t choice_set;
        choice_set["type"] = "Input.ChoiceSet";
        choice_set["id"] = "resolution";
        choice_set["label"] = "Stream Resolution";
        choice_set["value"] = resolution_;
        std::vector<glz::json_t> choices;
        choices.push_back(glz::json_t::object_t{{"title", "1920 x 1080 (1080p)"}, {"value", "1920 x 1080 (1080p)"}});
        choices.push_back(glz::json_t::object_t{{"title", "1280 x 720 (720p)"}, {"value", "1280 x 720 (720p)"}});
        choices.push_back(glz::json_t::object_t{{"title", "3840 x 2160 (4K UHD)"}, {"value", "3840 x 2160 (4K UHD)"}});
        choice_set["choices"] = std::move(choices);
        config_items.push_back(std::move(choice_set));

        glz::json_t number_fps;
        number_fps["type"] = "Input.Number";
        number_fps["id"] = "fps";
        number_fps["label"] = "Target Framerate (FPS)";
        number_fps["value"] = std::to_string(target_fps_);
        number_fps["min"] = 15.0;
        number_fps["max"] = 120.0;
        config_items.push_back(std::move(number_fps));

        config_container["items"] = std::move(config_items);
        body.push_back(std::move(config_container));

        // 3. FactSet
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;
        facts.push_back(glz::json_t::object_t{{"title", "Stream Endpoint:"}, {"value", get_stream_endpoint()}});
        facts.push_back(glz::json_t::object_t{{"title", "Active Viewers:"}, {"value", std::format("{} connected client{}", active_viewers_, (active_viewers_ == 1 ? "" : "s"))}});
        facts.push_back(glz::json_t::object_t{{"title", "Resolution:"}, {"value", resolution_}});
        facts.push_back(glz::json_t::object_t{{"title", "Framerate:"}, {"value", std::format("{} FPS (Hardware accelerated)", target_fps_)}});
        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;
        if (broadcasting) {
            glz::json_t stop_act;
            stop_act["type"] = "Action.Execute";
            stop_act["title"] = "Stop Broadcast";
            stop_act["verb"] = "cast_stop";
            actions.push_back(std::move(stop_act));
        } else {
            glz::json_t start_act;
            start_act["type"] = "Action.Execute";
            start_act["title"] = "Start Broadcast";
            start_act["verb"] = "cast_start";
            actions.push_back(std::move(start_act));
        }

        glz::json_t copy_act;
        copy_act["type"] = "Action.Execute";
        copy_act["title"] = "Copy URL";
        copy_act["verb"] = "cast_copy_url";
        actions.push_back(std::move(copy_act));

        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override {
        if (action_json.empty()) return;
        try {
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "cast_start") {
                is_broadcasting_ = true;
                if (data.is_object()) {
                    if (data.contains("fps")) {
                        if (data["fps"].holds<double>()) {
                            target_fps_ = static_cast<int>(data["fps"].get<double>());
                        } else if (data["fps"].holds<std::string>()) {
                            try {
                                target_fps_ = std::stoi(data["fps"].get<std::string>());
                            } catch (...) {}
                        }
                    }
                    if (data.contains("resolution") && data["resolution"].holds<std::string>()) {
                        resolution_ = data["resolution"].get<std::string>();
                    }
                }
                auto host = rouen::hosts::VideoFeedHost::get_host();
                if (host && !host->is_running() && !host->is_starting()) {
                    host->start();
                }
            } else if (verb == "cast_stop") {
                is_broadcasting_ = false;
                auto host = rouen::hosts::VideoFeedHost::get_host();
                if (host && (host->is_running() || host->is_starting())) {
                    host->stop();
                }
            } else if (verb == "cast_copy_url") {
                std::string ep = get_stream_endpoint();
                last_copied_url_ = ep;
                SDL_SetClipboardText(ep.c_str());
                if (ImGui::GetCurrentContext()) {
                    ImGui::SetClipboardText(ep.c_str());
                }
                try {
                    "notify"_sfn("Stream URL copied to clipboard");
                } catch (...) {}
            }
        } catch (const std::exception& e) {
            std::cerr << "[cast-control] Action error: " << e.what() << std::endl;
        }
    }

    [[nodiscard]] bool is_broadcasting() const {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        if (host && host->is_running()) {
            return true;
        }
        return is_broadcasting_;
    }

    [[nodiscard]] int get_target_fps() const { return target_fps_; }
    [[nodiscard]] const std::string& get_resolution() const { return resolution_; }
    [[nodiscard]] std::string get_stream_endpoint() const {
        auto host = rouen::hosts::VideoFeedHost::get_host();
        if (host && !host->endpoint().empty()) {
            return host->endpoint();
        }
        return stream_endpoint_;
    }
    [[nodiscard]] int get_active_viewers() const { return active_viewers_; }
    [[nodiscard]] const std::string& get_last_copied_url() const { return last_copied_url_; }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        }
                    );
                }
                return;
            }

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
        ui.text_colored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), ICON_MD_VIDEOCAM " ROUEN CAST & VIDEO FEED");
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

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // State Synchronization
    bool is_broadcasting_{false};
    std::string stream_endpoint_{"tcp://127.0.0.1:8889"};
    int active_viewers_{2};
    int target_fps_{60};
    std::string resolution_{"1920 x 1080 (1080p)"};
    std::string last_copied_url_{};
};

} // namespace rouen::cards
