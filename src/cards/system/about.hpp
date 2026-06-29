#pragma once

#include <string>
#include <thread>
#include <format>
#include <iostream>
#include <filesystem>
#include <SDL.h>
#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/platform_utils.hpp"

// Fallback if COMPILE_GIT_HASH is not defined by build system
#ifndef COMPILE_GIT_HASH
#define COMPILE_GIT_HASH "unknown"
#endif

namespace rouen::cards {

struct about_card : public card {
    std::string local_hash = COMPILE_GIT_HASH;
    std::string remote_hash = "Checking...";
    std::string error_message = "";
    bool loading = true;
    bool is_up_to_date = false;
    bool is_open = true;

    SDL_Texture* icon_texture = nullptr;
    int icon_width = 0;
    int icon_height = 0;

    explicit about_card(SDL_Renderer* renderer) {
        colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};     // Blue primary color (first_color)
        colors[1] = {0.251f, 0.878f, 0.816f, 0.7f};   // Turquoise secondary color (second_color)
        
        name("About Rouen");
        width = 460.0f;
        
        // Load app icon texture
        if (renderer) {
            std::filesystem::path icon_path = rouen::platform::get_resource_path("Rouen.png", "img");
            icon_texture = TextureHelper::loadTextureFromFile(renderer, icon_path.string().c_str(), icon_width, icon_height);
        }
        
        // Fetch remote hash in a background thread to prevent UI freezing
        // NOLINTNEXTLINE(bugprone-exception-escape)
        std::thread([this]() noexcept {
            try {
                http::fetch fetch_client;
                std::string response = fetch_client("https://api.github.com/repos/ignacionr/rouen/branches/main");
                
                auto json_obj = glz::read_json<glz::json_t>(response);
                if (json_obj) {
                    auto& val = *json_obj;
                    if (val.contains("commit") && val["commit"].contains("sha")) {
                        remote_hash = val["commit"]["sha"].get<std::string>();
                        
                        // Compare the local hash with the remote hash
                        if (local_hash != "unknown" && !remote_hash.empty()) {
                            std::string local_short = local_hash.substr(0, 7);
                            std::string remote_short = remote_hash.substr(0, 7);
                            is_up_to_date = (local_short == remote_short);
                        }
                    } else {
                        error_message = "Invalid JSON response from GitHub API";
                    }
                } else {
                    error_message = "Failed to parse GitHub API response";
                }
            } catch (const std::exception& e) {
                try {
                    error_message = "Network error: " + std::string(e.what());
                } catch (...) {
                    error_message = "Network error";
                }
            } catch (...) {
                error_message = "Unknown error occurred during fetch";
            }
            loading = false;
        }).detach();
    }

    // Explicit destructor
    ~about_card() override {
        if (icon_texture) {
            TextureHelper::destroyTexture(icon_texture);
        }
    }

    std::string get_uri() const override {
        return "about";
    }

    bool render(rouen::ui::ui_context& ui) override {
        if (!is_open) return false;
        
        return render_window([this, &ui]() {
            // Horizontal layout for icon and title
            if (icon_texture) {
                auto tex_id = rouen::helpers::texture_id_cast(icon_texture);
                ui.image(tex_id, ImVec2(64.0f, 64.0f));
                ui.same_line(0.0f, 15.0f);
            }
            
            ui.begin_group();
            ui.text("Rouen Dashboard Application");
            ui.text("A productivity tool built with C++, SDL2, and ImGui.");
            ui.end_group();
            
            ui.separator();
            
            ui.text_colored(colors[0], "Version Information");
            ui.indent(10.0f);
            ui.text(std::format("Local Commit Hash: {}", local_hash));
            
            if (loading) {
                ui.text_colored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Fetching remote repository state...");
            } else if (!error_message.empty()) {
                ui.text_colored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), error_message);
            } else {
                ui.text(std::format("Remote Commit Hash: {}", remote_hash));
                ui.spacing();
                if (is_up_to_date) {
                    ui.text_colored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Your local build is UP TO DATE with ignacionr/rouen main branch.");
                } else {
                    ui.text_colored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Your local build is DIFFERENT from ignacionr/rouen main branch.");
                }
            }
            ui.unindent(10.0f);
            ui.separator();
            
            if (ui.button("Close")) {
                is_open = false;
            }
        });
    }
};

} // namespace rouen::cards
