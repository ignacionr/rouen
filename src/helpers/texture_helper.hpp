#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>

namespace TextureHelper {
    /**
     * Load a texture from a file using SDL
     * 
     * @param renderer The SDL renderer to use for creating the texture
     * @param filename Path to the image file
     * @param image_width Output parameter to store the image width
     * @param image_height Output parameter to store the image height
     * @return SDL_Texture* pointer if successful, nullptr if failed
     */
    inline SDL_Texture* loadTextureFromFile(
        SDL_Renderer* renderer,
        const char* filename,
        int& image_width,
        int& image_height
    ) {
        // Load the image
        SDL_Surface* surface = IMG_Load(filename);
        if (!surface) {
            std::cerr << "Failed to load image: " << IMG_GetError() << std::endl;
            return nullptr;
        }
        
        // Convert to texture using SDL_Renderer
        SDL_Texture* image_texture = SDL_CreateTextureFromSurface(renderer, surface);
        
        // Save dimensions
        image_width = surface->w;
        image_height = surface->h;
        
        // Free surface
        SDL_FreeSurface(surface);
        
        if (!image_texture) {
            std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
            return nullptr;
        }
        
        return image_texture;
    }
}