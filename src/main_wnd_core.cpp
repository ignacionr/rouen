#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "api_server_host.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "main_wnd.hpp"
#include "helpers/deferred_operations.hpp"
#include "registrar.hpp"
#include "helpers/texture_helper.hpp"

#include "helpers/media_player.hpp"

main_wnd::main_wnd()
    : m_window(nullptr)
    , m_device(nullptr)
    , m_deferred_ops(std::make_shared<deferred_operations>())
    , m_done(false)
    , m_immediate(false)
    , m_requested_fps(1)
    , m_imgui_sdl_initialized(false)
    , m_imgui_renderer_initialized(false)
    , m_imgui_context_created(false)
    , m_api_server(std::make_unique<rouen::helpers::api_server>())
{
    // register an extractor for keystrokes
    registrar::add<std::function<std::string()>>(
        "keystrokes",
        std::make_shared<std::function<std::string()>>([this]() -> std::string {
            return keystrokes_;
        }));

    // register a setter for keystrokes
    registrar::add<std::function<void(std::string)>>(
        "set_keystrokes",
        std::make_shared<std::function<void(std::string)>>([this](std::string value) {
            keystrokes_ = std::move(value);
        }));
}

main_wnd::~main_wnd() {
    // Destroy secondary/detached ImGui context before main ImGui shutdown
    if (m_detached_imgui_ctx) {
        ImGui::SetCurrentContext(m_detached_imgui_ctx);
        ImGui::DestroyContext(m_detached_imgui_ctx);
        m_detached_imgui_ctx = nullptr;
    }

    // Cleanup ImGui main context and backends
    if (m_imgui_renderer_initialized) {
        ImGui_ImplSDLGPU3_Shutdown();
    }

    if (m_imgui_sdl_initialized) {
        ImGui_ImplSDL3_Shutdown();
    }

    if (m_imgui_context_created) {
        ImGui::DestroyContext();
    }

    // Remove device from registrar before destroying it
    registrar::remove<SDL_GPUDevice*>("main_gpu_device");

    // Stop API server before cleanup
    if (m_api_server) {
        m_api_server->stop();
    }

    // Clean up SDL
    if (m_device) {
        TextureHelper::shutdown();
        SDL_DestroyGPUDevice(m_device);
    }
    if (m_detached_window) {
        SDL_DestroyWindow(m_detached_window);
        m_detached_window = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    media_player::stopAll();
    SDL_Quit();
}

void main_wnd::process_deferred_operations() {
    if (m_deferred_ops && m_deferred_ops->has_operations()) {
        m_deferred_ops->process_queue(m_device);
        m_immediate = true;
    }
}

void main_wnd::resize_window(int width, int height) {
    if (m_window) {
        SDL_SetWindowSize(m_window, width, height);
        
        // Update viewport if needed
        if (m_device) {
            std::cout << "Window resized to " << width << "x" << height << '\n';
        }
    }
}

void main_wnd::expand_to_full_width() {
    if (m_window) {
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_window);
        if (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) {
            SDL_RestoreWindow(m_window);
        }
        SDL_DisplayID display_id = SDL_GetDisplayForWindow(m_window);
        SDL_Rect usable_bounds{};
        if (SDL_GetDisplayUsableBounds(display_id, &usable_bounds) ||
            SDL_GetDisplayBounds(display_id, &usable_bounds)) {
            int current_x = 0, current_y = 0;
            int current_w = 0, current_h = 0;
            SDL_GetWindowPosition(m_window, &current_x, &current_y);
            SDL_GetWindowSize(m_window, &current_w, &current_h);
            
            SDL_SetWindowPosition(m_window, usable_bounds.x, current_y);
            resize_window(usable_bounds.w, current_h);
        }
    }
}
