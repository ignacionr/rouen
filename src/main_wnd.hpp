#pragma once

// 1. Standard includes in alphabetic order
#include <functional>
#include <iostream>
#include <memory>
#include <string>

// 2. Libraries used in the project, in alphabetic order
#include "helpers/imgui_include.hpp"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

// 3. All other includes
#include "helpers/deferred_operations.hpp"
#include "helpers/api_server.hpp"
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
    SDL_GPUDevice* get_device() const { return m_device; }
    SDL_Window* get_window() const { return m_window; }

    static void setup_dark_theme();

    // Configure high-DPI settings for ImGui
    void configure_highdpi_settings();

    // Update ImGui display settings each frame
    void update_imgui_display_settings();

    // Window resizing functionality
    void resize_window(int width, int height);

private:
    bool process_events();
    void process_deferred_operations();

    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
    std::shared_ptr<deferred_operations> m_deferred_ops;
    bool m_done = false;
    bool m_immediate = false;
    int m_requested_fps = 1;
    std::string keystrokes_;
    std::chrono::steady_clock::time_point m_last_main_render_time{};
    
    // Flag to track if Cmd+W was handled at card level this frame
    bool m_card_close_handled_this_frame = false;
    
    // ImGui initialization state tracking
    bool m_imgui_sdl_initialized = false;
    bool m_imgui_renderer_initialized = false;
    bool m_imgui_context_created = false;
    
    // API server for command and control
    std::unique_ptr<rouen::helpers::api_server> m_api_server;
    
    // MainWindow is commented out as it's currently unused
    // Uncomment when needed for implementation
    // rouen::MainWindow m_main_window;
};
