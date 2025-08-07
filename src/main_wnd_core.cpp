// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "helpers/imgui_include.hpp"

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "main_wnd.hpp"

main_wnd::main_wnd() 
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_deferred_ops(std::make_shared<deferred_operations>())
    , m_done(false)
    , m_immediate(false)
    , m_requested_fps(1)
{
    // register an extractor for keystrokes
    registrar::add<std::function<std::string()>>(
        "keystrokes", 
        std::make_shared<std::function<std::string()>>(
            [this]() {
                auto result = keystrokes_;
                keystrokes_.clear();
                return result;
            }
        )
    );
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
            DB_INFO_FMT("Window resized to {}x{}", width, height);
        }
    }
}
