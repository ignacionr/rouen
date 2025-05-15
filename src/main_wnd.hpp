#pragma once

// 1. Standard includes in alphabetic order
#include <functional>
#include <iostream>
#include <memory>
#include <string>

// 2. Libraries used in the project, in alphabetic order
#include "helpers/imgui_include.hpp"
#include <SDL.h>
#include <SDL_image.h>

// 3. All other includes
#include "helpers/deferred_operations.hpp"
#include "registrar.hpp"

// Define MainWindow class in the rouen namespace for consistency
namespace rouen {
    class MainWindow {
    public:
        void render_font_check();
    };
}

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

    void setup_dark_theme();

private:
    bool process_events();
    void process_deferred_operations();

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    std::shared_ptr<deferred_operations> m_deferred_ops;
    bool m_done = false;
    bool m_immediate = false;
    int m_requested_fps = 1;
    std::string keystrokes_;
    
    // MainWindow is commented out as it's currently unused
    // Uncomment when needed for implementation
    // rouen::MainWindow m_main_window;
};
