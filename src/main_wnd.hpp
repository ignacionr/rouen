#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <memory>
#include <functional>

#include "helpers/deferred_operations.hpp"
#include "registrar.hpp"

class main_wnd {
public:
    main_wnd();
    ~main_wnd();

    bool initialize();
    void run();
    bool is_done() const { return m_done; }
    void set_done(bool done) { m_done = done; }
    SDL_Renderer* get_renderer() const { return m_renderer; }
    SDL_Window* get_window() const { return m_window; }

private:
    bool process_events();
    void process_deferred_operations();

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    std::shared_ptr<deferred_operations> m_deferred_ops;
    bool m_done = false;
    bool m_immediate = false;
    int m_requested_fps = 1;
};