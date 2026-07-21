#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "texture_helper.hpp"

// Compatibility macros redirecting SDL2 rendering constructs to SDL3 GPU equivalents
#define SDL_Renderer SDL_GPUDevice
#define SDL_Texture RouenGPUTexture
#define SDL_DestroyTexture(tex) TextureHelper::destroyTexture(tex)
