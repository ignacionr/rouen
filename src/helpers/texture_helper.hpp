#pragma once

#include <SDL.h>
#include <SDL_image.h>
#include "debug.hpp"

// Add texture-specific logging macros
#define TEXTURE_ERROR(message) LOG_COMPONENT("TEXTURE", LOG_LEVEL_ERROR, message)
#define TEXTURE_ERROR_FMT(fmt, ...) TEXTURE_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define TEXTURE_WARN(message) LOG_COMPONENT("TEXTURE", LOG_LEVEL_WARN, message)
#define TEXTURE_WARN_FMT(fmt, ...) TEXTURE_WARN(debug::format_log(fmt, __VA_ARGS__))
#define TEXTURE_INFO(message) LOG_COMPONENT("TEXTURE", LOG_LEVEL_INFO, message)
#define TEXTURE_INFO_FMT(fmt, ...) TEXTURE_INFO(debug::format_log(fmt, __VA_ARGS__))
#define TEXTURE_DEBUG(message) LOG_COMPONENT("TEXTURE", LOG_LEVEL_DEBUG, message)
#define TEXTURE_DEBUG_FMT(fmt, ...) TEXTURE_DEBUG(debug::format_log(fmt, __VA_ARGS__))

namespace TextureHelper {
    // Function to load a texture from a file path
    inline SDL_Texture* loadTextureFromFile(SDL_Renderer* renderer, const char* filepath, int& width, int& height) {
        if (!renderer) {
            TEXTURE_ERROR("Cannot load texture: renderer is null");
            return nullptr;
        }

        // Load image using SDL_image
        SDL_Surface* surface = IMG_Load(filepath);
        if (!surface) {
            TEXTURE_ERROR_FMT("Failed to load image {}: {}", filepath, IMG_GetError());
            return nullptr;
        }

        // Store the dimensions
        width = surface->w;
        height = surface->h;

        // Create texture from surface
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture) {
            TEXTURE_ERROR_FMT("Failed to create texture from {}: {}", filepath, SDL_GetError());
            SDL_FreeSurface(surface);
            return nullptr;
        }

        // Free the surface as it's no longer needed
        SDL_FreeSurface(surface);
        
        TEXTURE_INFO_FMT("Successfully loaded texture from {} ({}x{})", filepath, width, height);
        return texture;
    }

    // Function to create a solid color texture
    inline SDL_Texture* createSolidColorTexture(SDL_Renderer* renderer, int width, int height, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
        if (!renderer) {
            TEXTURE_ERROR("Cannot create texture: renderer is null");
            return nullptr;
        }

        // Create a texture with the given dimensions
        SDL_Texture* texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET,
            width,
            height
        );

        if (!texture) {
            TEXTURE_ERROR_FMT("Failed to create texture: {}", SDL_GetError());
            return nullptr;
        }

        // Set the texture as the render target
        SDL_SetRenderTarget(renderer, texture);
        
        // Set blend mode to support alpha
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

        // Clear with the specified color
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderClear(renderer);

        // Reset the render target
        SDL_SetRenderTarget(renderer, nullptr);

        TEXTURE_INFO_FMT("Created solid color texture ({}x{}) with color ({},{},{},{})", 
                      width, height, r, g, b, a);
        return texture;
    }

    // Function to safely destroy a texture
    inline void destroyTexture(SDL_Texture*& texture) {
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
            TEXTURE_DEBUG("Texture destroyed");
        }
    }
}