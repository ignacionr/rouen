#pragma once

#include <string>
#include <vector>
#include <cstring>

#include <SDL3/SDL.h>

#include "texture_helper.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #pragma comment(lib, "Shell32.lib")
#endif

namespace rouen::helpers {

    // Extracts an icon representing the given path (an executable on Windows, an
    // app bundle or arbitrary file on macOS) as an RGBA32 SDL_Surface. Caller owns
    // the returned surface (SDL_DestroySurface). Returns nullptr if unavailable.
#if defined(_WIN32)
    inline SDL_Surface* extract_icon_surface(const std::string& path) {
        SHFILEINFOA shfi{};
        SHGetFileInfoA(path.c_str(), 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_LARGEICON);
        HICON hicon = shfi.hIcon;
        if (!hicon) return nullptr;

        ICONINFO icon_info{};
        if (!GetIconInfo(hicon, &icon_info)) {
            DestroyIcon(hicon);
            return nullptr;
        }

        BITMAP bmp_color{};
        GetObjectA(icon_info.hbmColor, sizeof(bmp_color), &bmp_color);
        int width = bmp_color.bmWidth;
        int height = bmp_color.bmHeight;

        SDL_Surface* surface = nullptr;
        if (width > 0 && height > 0) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height; // request top-down rows
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            HDC hdc = GetDC(nullptr);
            int rows_copied = GetDIBits(hdc, icon_info.hbmColor, 0, static_cast<UINT>(height),
                                        pixels.data(), &bmi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, hdc);

            if (rows_copied > 0) {
                // GetDIBits yields BGRA; swap to RGBA for SDL_PIXELFORMAT_RGBA32.
                for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
                    std::swap(pixels[i], pixels[i + 2]);
                }

                surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
                if (surface) {
                    for (int y = 0; y < height; ++y) {
                        std::memcpy(static_cast<uint8_t*>(surface->pixels) + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch),
                                    pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4,
                                    static_cast<size_t>(width) * 4);
                    }
                }
            }
        }

        DeleteObject(icon_info.hbmColor);
        DeleteObject(icon_info.hbmMask);
        DestroyIcon(hicon);
        return surface;
    }
#elif defined(__APPLE__)
    // Implemented in app_icon.mm using NSWorkspace.
    SDL_Surface* extract_icon_surface(const std::string& path);
#else
    inline SDL_Surface* extract_icon_surface(const std::string& /*path*/) { return nullptr; }
#endif

    // Convenience wrapper: extracts an icon and uploads it as a GPU texture in one call.
    inline RouenGPUTexture* extract_icon_texture(SDL_GPUDevice* device, const std::string& path, int& width, int& height) {
        SDL_Surface* surface = extract_icon_surface(path);
        if (!surface) return nullptr;
        width = surface->w;
        height = surface->h;
        RouenGPUTexture* tex = TextureHelper::createTextureFromSurface(device, surface);
        SDL_DestroySurface(surface);
        return tex;
    }

} // namespace rouen::helpers
