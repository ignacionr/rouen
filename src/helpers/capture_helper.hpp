#pragma once

#include <functional>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_sdlrenderer2.h>
#include <SDL2/SDL.h>

#include "../registrar.hpp"
#include "debug.hpp"

// Define capture-specific logging macros
#define CAPTURE_ERROR(message) LOG_COMPONENT("CAPTURE", LOG_LEVEL_ERROR, message)
#define CAPTURE_ERROR_FMT(fmt, ...) CAPTURE_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define CAPTURE_INFO(message) LOG_COMPONENT("CAPTURE", LOG_LEVEL_INFO, message)
#define CAPTURE_INFO_FMT(fmt, ...) CAPTURE_INFO(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::helpers {

/**
 * Takes a snapshot of ImGui rendering operations and returns them as a texture
 * 
 * @param width Width of the capture texture
 * @param height Height of the capture texture
 * @param render_callback Callable that performs ImGui rendering operations
 * @param renderer Optional SDL renderer (if not provided, the main renderer will be used)
 * @return SDL_Texture* containing the captured rendering, or nullptr on failure
 *         Note: The caller is responsible for destroying this texture with SDL_DestroyTexture
 */
inline SDL_Texture* capture_imgui(
    int width, 
    int height, 
    const std::function<void()>& render_callback,
    SDL_Renderer* renderer = nullptr
) {
    // Get SDL renderer if not provided
    if (!renderer) {
        try {
            auto renderer_ptr = registrar::get<SDL_Renderer*>("main_renderer");
            renderer = *renderer_ptr;
        } catch (const std::runtime_error& e) {
            CAPTURE_ERROR_FMT("Failed to get renderer from registrar: {}", e.what());
            return nullptr;
        }
    }

    // Create a target texture
    SDL_Texture* capture_texture = SDL_CreateTexture(
        renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_TARGET, 
        width, 
        height
    );
    
    if (!capture_texture) {
        CAPTURE_ERROR_FMT("Failed to create capture texture: {}", SDL_GetError());
        return nullptr;
    }
    
    // Set blend mode for the texture
    SDL_SetTextureBlendMode(capture_texture, SDL_BLENDMODE_BLEND);
    
    // Save the current render target
    SDL_Texture* original_target = SDL_GetRenderTarget(renderer);
    
    // Save the current ImGui context
    ImGuiContext* original_context = ImGui::GetCurrentContext();
    
    // Create a dummy window for SDL2 backend
    SDL_Window* dummy_window = SDL_CreateWindow(
        "Capture Helper Dummy Window", 
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height, 
        SDL_WINDOW_HIDDEN
    );
    
    // Create a new ImGui context
    ImGuiContext* capture_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(capture_context);
    
    // Initialize ImGui IO parameters
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = 1.0f / 60.0f;
    
    // Set the render target to our texture
    SDL_SetRenderTarget(renderer, capture_texture);
    
    // Clear the texture with a transparent background
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    
    // Initialize ImGui backends
    ImGui_ImplSDL2_InitForSDLRenderer(dummy_window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    
    try {
        // Start a new ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        // Set up a window with the exact size and no decorations
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
        
        // Execute the render callback
        render_callback();
        
        ImGui::PopStyleVar(2);
        
        // Render ImGui
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    } 
    catch (const std::exception& e) {
        CAPTURE_ERROR_FMT("Exception during capture: {}", e.what());
    }
    
    // Cleanup the temporary ImGui context
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(capture_context);
    
    // Restore the original ImGui context
    ImGui::SetCurrentContext(original_context);
    
    // Restore the original render target
    SDL_SetRenderTarget(renderer, original_target);
    
    // Destroy the dummy window
    SDL_DestroyWindow(dummy_window);
    
    // Return the texture with the captured content
    return capture_texture;
}

} // namespace rouen::helpers