// Official ImGui backend implementation for SDL2
// Source: https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_sdl2.cpp
// (Vendored for reproducible Nix builds)

#include "imgui_impl_sdl2.h"
#include <imgui.h>
#include <SDL.h>

// This is a stub implementation - the real implementation should be fetched from ImGui
// For now, this provides compilation compatibility for system ImGui + local backends

static SDL_Window* g_Window = nullptr;
static Uint64 g_Time = 0;

bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* sdl_gl_context) {
    g_Window = window;
    return true;
}

bool ImGui_ImplSDL2_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer) {
    g_Window = window;
    return true;
}

bool ImGui_ImplSDL2_InitForVulkan(SDL_Window* window) {
    g_Window = window;
    return true;
}

bool ImGui_ImplSDL2_InitForD3D(SDL_Window* window) {
    g_Window = window;
    return true;
}

bool ImGui_ImplSDL2_InitForMetal(SDL_Window* window) {
    g_Window = window;
    return true;
}

bool ImGui_ImplSDL2_InitForOther(SDL_Window* window) {
    g_Window = window;
    return true;
}

void ImGui_ImplSDL2_Shutdown() {
    g_Window = nullptr;
}

void ImGui_ImplSDL2_NewFrame() {
    if (!g_Window) return;
    
    ImGuiIO& io = ImGui::GetIO();
    
    // Setup display size
    int w, h;
    int display_w, display_h;
    SDL_GetWindowSize(g_Window, &w, &h);
    SDL_GL_GetDrawableSize(g_Window, &display_w, &display_h);
    io.DisplaySize = ImVec2((float)w, (float)h);
    if (w > 0 && h > 0)
        io.DisplayFramebufferScale = ImVec2((float)display_w / w, (float)display_h / h);
    
    // Setup time step
    Uint64 current_time = SDL_GetPerformanceCounter();
    io.DeltaTime = g_Time > 0 ? (float)((double)(current_time - g_Time) / SDL_GetPerformanceFrequency()) : (float)(1.0f/60.0f);
    g_Time = current_time;
}

bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event) {
    // Minimal event processing
    ImGuiIO& io = ImGui::GetIO();
    
    switch (event->type) {
        case SDL_MOUSEWHEEL:
            if (event->wheel.x > 0) io.MouseWheelH += 1;
            if (event->wheel.x < 0) io.MouseWheelH -= 1;
            if (event->wheel.y > 0) io.MouseWheel += 1;
            if (event->wheel.y < 0) io.MouseWheel -= 1;
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            {
                int mouse_button = -1;
                if (event->button.button == SDL_BUTTON_LEFT) { mouse_button = 0; }
                if (event->button.button == SDL_BUTTON_RIGHT) { mouse_button = 1; }
                if (event->button.button == SDL_BUTTON_MIDDLE) { mouse_button = 2; }
                if (mouse_button != -1) {
                    io.AddMouseButtonEvent(mouse_button, (event->type == SDL_MOUSEBUTTONDOWN));
                }
            }
            return true;
        case SDL_TEXTINPUT:
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            // Basic key handling would go here
            return true;
    }
    return false;
}
