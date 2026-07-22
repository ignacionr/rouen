#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_camera.h>
#include <iostream>
#include <vector>

TEST(PixelConversionTest, TestNV12SurfaceConversion) {
    int w = 640;
    int h = 480;
    SDL_Surface* nv12_surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_NV12);
    ASSERT_NE(nv12_surf, nullptr);

    std::cout << "[TEST] Created NV12 surface " << w << "x" << h << " (pitch: " << nv12_surf->pitch << ")" << std::endl;

    SDL_Surface* rgba_surf = SDL_ConvertSurface(nv12_surf, SDL_PIXELFORMAT_RGBA32);
    std::cout << "[TEST] SDL_ConvertSurface NV12 -> RGBA32 result: " << (rgba_surf ? "SUCCESS" : "FAILED") << std::endl;
    if (!rgba_surf) {
        std::cout << "[TEST] SDL_ConvertSurface error: " << SDL_GetError() << std::endl;
    } else {
        std::cout << "[TEST] Converted RGBA surface: " << rgba_surf->w << "x" << rgba_surf->h << " pitch: " << rgba_surf->pitch << std::endl;
        SDL_DestroySurface(rgba_surf);
    }
    SDL_DestroySurface(nv12_surf);
}
