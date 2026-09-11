#pragma once

#include <string>
#include <thread>
#include <format>
#include <iostream>
#include <filesystem>
#include <SDL3/SDL.h>
#include "../interface/card.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../../external/IconsMaterialDesign.h"

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
    SDL_Renderer* m_renderer = nullptr;

    void fetch_remote_hash() {
        loading = true;
        error_message.clear();
        std::thread([this]() {
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

    explicit about_card(SDL_Renderer* renderer) : m_renderer(renderer) {
        colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};     // Blue primary color (first_color)
        colors[1] = {0.251f, 0.878f, 0.816f, 0.7f};   // Turquoise secondary color (second_color)
        
        name("About Rouen");
        width = 460.0f;
        
        // Load app icon texture
        if (renderer) {
            std::filesystem::path icon_path = rouen::platform::get_resource_path("Rouen.png", "img");
            icon_texture = TextureHelper::loadTextureFromFile(renderer, icon_path.string().c_str(), icon_width, icon_height);
        }
        
        // Fetch remote hash in background thread
        fetch_remote_hash();
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

    std::string get_gpu_driver() const {
        if (!m_renderer) return "Unknown";
        const char* gpu_driver = SDL_GetGPUDeviceDriver(m_renderer);
        return gpu_driver ? std::string(gpu_driver) : "Unknown";
    }

    std::string get_shader_formats() const {
        if (!m_renderer) return "None";
        SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(m_renderer);
        std::string shader_str = "";
        if (formats & SDL_GPU_SHADERFORMAT_PRIVATE) shader_str += "Private, ";
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) shader_str += "SPIR-V (Vulkan), ";
        if (formats & SDL_GPU_SHADERFORMAT_DXBC) shader_str += "DXBC (Direct3D 11), ";
        if (formats & SDL_GPU_SHADERFORMAT_DXIL) shader_str += "DXIL (Direct3D 12), ";
        if (formats & SDL_GPU_SHADERFORMAT_MSL) shader_str += "MSL (Metal), ";
        if (!shader_str.empty()) {
            shader_str = shader_str.substr(0, shader_str.length() - 2);
        } else {
            shader_str = "None";
        }
        return shader_str;
    }

    std::string get_adaptive_card_json() const override {
        std::string status_str;
        if (loading) {
            status_str = "Fetching remote repository state...";
        } else if (!error_message.empty()) {
            status_str = error_message;
        } else if (is_up_to_date) {
            status_str = "Up to date with ignacionr/rouen main branch";
        } else {
            status_str = "Update available on ignacionr/rouen main branch";
        }

        std::string local_short = (local_hash.length() >= 7) ? local_hash.substr(0, 7) : local_hash;
        std::string remote_short = (remote_hash.length() >= 7) ? remote_hash.substr(0, 7) : remote_hash;
        std::string gpu_driver = get_gpu_driver();
        std::string shader_formats = get_shader_formats();

        return std::format(R"({{
  "$schema": "http://adaptivecards.io/schemas/adaptive-card.json",
  "type": "AdaptiveCard",
  "version": "1.5",
  "body": [
    {{
      "type": "ColumnSet",
      "columns": [
        {{
          "type": "Column",
          "width": "auto",
          "items": [
            {{
              "type": "Image",
              "url": "https://raw.githubusercontent.com/ignacionr/rouen/main/assets/Rouen.png",
              "size": "Small",
              "style": "Person"
            }}
          ]
        }},
        {{
          "type": "Column",
          "width": "stretch",
          "items": [
            {{
              "type": "TextBlock",
              "text": "Rouen Dashboard Application",
              "weight": "Bolder",
              "size": "Medium",
              "wrap": true
            }},
            {{
              "type": "TextBlock",
              "text": "A productivity tool built with C++, SDL3, and ImGui.",
              "isSubtle": true,
              "wrap": true,
              "spacing": "None"
            }}
          ]
        }}
      ]
    }},
    {{
      "type": "Container",
      "style": "emphasis",
      "items": [
        {{
          "type": "TextBlock",
          "text": "Version Information",
          "weight": "Bolder",
          "color": "Accent"
        }},
        {{
          "type": "FactSet",
          "facts": [
            {{"title": "Local Commit:", "value": "{}"}},
            {{"title": "Remote Commit:", "value": "{}"}},
            {{"title": "Status:", "value": "{}"}}
          ]
        }}
      ]
    }},
    {{
      "type": "Container",
      "items": [
        {{
          "type": "TextBlock",
          "text": "Graphics Information",
          "weight": "Bolder",
          "color": "Accent"
        }},
        {{
          "type": "FactSet",
          "facts": [
            {{"title": "GPU Driver:", "value": "{}"}},
            {{"title": "Shader Formats:", "value": "{}"}}
          ]
        }}
      ]
    }}
  ],
  "actions": [
    {{
      "type": "Action.Execute",
      "title": "Check Updates",
      "verb": "check_updates"
    }}
  ]
}})", local_short, remote_short, status_str, gpu_driver, shader_formats);
    }

    void handle_action(std::string_view action_json) override {
        try {
            glz::json_t action_obj;
            auto err = glz::read_json(action_obj, std::string(action_json));
            if (!err) {
                std::string verb;
                if (action_obj.contains("verb") && action_obj["verb"].holds<std::string>()) {
                    verb = action_obj["verb"].get<std::string>();
                }
                if (verb == "check_updates" || verb == "refresh") {
                    fetch_remote_hash();
                }
            }
        } catch (...) {}
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
            ui.text("A productivity tool built with C++, SDL3, and ImGui.");
            ui.end_group();
            
            ui.separator();
            
            ui.text_colored(colors[0], "Version Information");
            ui.indent(10.0f);
            ui.text(std::format("Local Commit Hash: {}", local_hash));
            ui.same_line();
            if (ui.button(ICON_MD_CONTENT_COPY " Copy")) {
                ui.set_clipboard_text(local_hash);
            }
            
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

            ui.text_colored(colors[0], "Graphics Information (GPU Accelerated)");
            ui.indent(10.0f);
            std::string gpu_driver = get_gpu_driver();
            std::string shader_str = get_shader_formats();
            ui.text(std::format("GPU Backend Driver: {}", gpu_driver));
            ui.text(std::format("Shader Formats: {}", shader_str));
            ui.unindent(10.0f);
            ui.separator();
            
            if (ui.button("Close")) {
                is_open = false;
            }
        });
    }
};

} // namespace rouen::cards
