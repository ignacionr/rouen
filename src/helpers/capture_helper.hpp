#pragma once

#include <functional>
#include "./imgui_include.hpp"
#include "sdl_compat.hpp"

#include "../registrar.hpp"
#include "debug.hpp"

// Define capture-specific logging macros
#define CAPTURE_ERROR(message) LOG_COMPONENT("CAPTURE", LOG_LEVEL_ERROR, message)
#define CAPTURE_ERROR_FMT(fmt, ...) CAPTURE_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define CAPTURE_INFO(message) LOG_COMPONENT("CAPTURE", LOG_LEVEL_INFO, message)
#define CAPTURE_INFO_FMT(fmt, ...) CAPTURE_INFO(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::helpers {

/**
 * Takes a snapshot of ImGui rendering operations and returns them as a GPU texture
 * 
 * @param width Width of the capture texture
 * @param height Height of the capture texture
 * @param render_callback Callable that performs ImGui rendering operations
 * @param device Optional SDL GPU device (if not provided, the main device will be used)
 * @return RouenGPUTexture* containing the captured rendering, or nullptr on failure
 *         Note: The caller is responsible for destroying this texture using TextureHelper::destroyTexture
 */
RouenGPUTexture* capture_imgui(
    int width, 
    int height, 
    const std::function<void()>& render_callback,
    SDL_GPUDevice* device = nullptr
);

SDL_Surface* download_gpu_texture(
    SDL_GPUDevice* device,
    RouenGPUTexture* texture,
    int width,
    int height
);

} // namespace rouen::helpers
