#include "main_wnd.hpp"
#include "helpers/deferred_operations.hpp"
#include "helpers/api_server.hpp"
#include "helpers/debug.hpp"
#include "registrar.hpp"

main_wnd::main_wnd()
    : m_window(nullptr)
    , m_renderer(nullptr)
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
            keystrokes_ = value;
        }));
}

main_wnd::~main_wnd() {
    // Cleanup ImGui only if it was properly initialized
    if (m_imgui_renderer_initialized) {
        ImGui_ImplSDLRenderer2_Shutdown();
    }

    if (m_imgui_sdl_initialized) {
        ImGui_ImplSDL2_Shutdown();
    }

    if (m_imgui_context_created) {
        ImGui::DestroyContext();
    }

    // Remove renderer from registrar before destroying it
    registrar::remove<SDL_Renderer*>("main_renderer");

    // Stop API server before cleanup
    if (m_api_server) {
        m_api_server->stop();
    }

    // Clean up SDL
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

void main_wnd::process_deferred_operations() {
    if (m_deferred_ops && m_deferred_ops->has_operations()) {
        m_deferred_ops->process_queue(m_renderer);
        m_immediate = true;
    }
}

void main_wnd::resize_window(int width, int height) {
    if (m_window) {
        SDL_SetWindowSize(m_window, width, height);
        
        // Update viewport if needed
        if (m_renderer) {
            // SDL handles the renderer viewport automatically
            std::cout << "Window resized to " << width << "x" << height << std::endl;
        }
    }
}
