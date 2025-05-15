#pragma once

#include <functional>
#include "./imgui_include.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>

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
SDL_Texture* capture_imgui(
    int width, 
    int height, 
    const std::function<void()>& render_callback,
    SDL_Renderer* renderer = nullptr
);

} // namespace rouen::helpers