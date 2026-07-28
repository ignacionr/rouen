#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>
#include <mutex>
#include <vector>
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

struct RouenGPUTexture {
    SDL_GPUTextureSamplerBinding binding;
    int width = 0;
    int height = 0;
};

struct TextureCleanupItem {
    RouenGPUTexture* tex_ptr{nullptr};
    SDL_GPUTexture* raw_texture{nullptr};
};

namespace TextureHelper {
    inline SDL_GPUDevice* g_gpu_device = nullptr;
    inline SDL_GPUSampler* g_default_sampler = nullptr;
    inline std::vector<TextureCleanupItem> g_textures_to_cleanup;
    inline std::mutex g_cleanup_mutex;

    inline SDL_GPUSampler* getDefaultSampler(SDL_GPUDevice* device) {
        if (!device) device = g_gpu_device;
        if (!device) return nullptr;
        if (!g_default_sampler) {
            SDL_GPUSamplerCreateInfo sampler_info = {};
            sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
            sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
            sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            g_default_sampler = SDL_CreateGPUSampler(device, &sampler_info);
        }
        return g_default_sampler;
    }

    inline void cleanupFrame() {
        std::lock_guard<std::mutex> lock(g_cleanup_mutex);
        for (const auto& item : g_textures_to_cleanup) {
            if (g_gpu_device && item.raw_texture) {
                SDL_ReleaseGPUTexture(g_gpu_device, item.raw_texture);
            }
            delete item.tex_ptr;
        }
        g_textures_to_cleanup.clear();
    }

    inline void shutdown() {
        cleanupFrame();
        if (g_default_sampler && g_gpu_device) {
            SDL_ReleaseGPUSampler(g_gpu_device, g_default_sampler);
            g_default_sampler = nullptr;
        }
    }

    // Function to load a texture from a file path using SDL3 GPU API
    inline RouenGPUTexture* loadTextureFromFile(SDL_GPUDevice* device, const char* filepath, int& width, int& height) {
        if (!device) {
            TEXTURE_ERROR("Cannot load texture: device is null")
            return nullptr;
        }

        // Load image using SDL_image
        SDL_Surface* surface = IMG_Load(filepath);
        if (!surface) {
            TEXTURE_ERROR_FMT("Failed to load image {}: {}", filepath, SDL_GetError())
            return nullptr;
        }

        // Ensure surface format is RGBA32
        SDL_Surface* rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (!rgba_surface) {
            TEXTURE_ERROR("Failed to convert surface to RGBA32")
            return nullptr;
        }

        width = rgba_surface->w;
        height = rgba_surface->h;

        // 1. Create the GPU Texture
        SDL_GPUTextureCreateInfo texture_info = {};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = static_cast<Uint32>(width);
        texture_info.height = static_cast<Uint32>(height);
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texture_info);
        if (!texture) {
            TEXTURE_ERROR_FMT("Failed to create GPU texture: {}", SDL_GetError())
            SDL_DestroySurface(rgba_surface);
            return nullptr;
        }

        // 2. Create a Transfer Buffer
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<Uint32>(height * rgba_surface->pitch);
        
        SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (!transfer_buffer) {
            TEXTURE_ERROR_FMT("Failed to create transfer buffer: {}", SDL_GetError())
            SDL_ReleaseGPUTexture(device, texture);
            SDL_DestroySurface(rgba_surface);
            return nullptr;
        }

        // 3. Copy pixel data to the Transfer Buffer
        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer_buffer, false));
        if (map) {
            std::memcpy(map, rgba_surface->pixels, static_cast<size_t>(height) * static_cast<size_t>(rgba_surface->pitch));
            SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        } else {
            TEXTURE_ERROR_FMT("Failed to map transfer buffer: {}", SDL_GetError())
        }

        // 4. Upload to GPU
        SDL_GPUCommandBuffer* cmd_buf = SDL_AcquireGPUCommandBuffer(device);
        if (cmd_buf) {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);
            if (copy_pass) {
                SDL_GPUTextureTransferInfo transfer_info_gpu = {};
                transfer_info_gpu.transfer_buffer = transfer_buffer;
                transfer_info_gpu.offset = 0;
                transfer_info_gpu.pixels_per_row = static_cast<Uint32>(width);
                transfer_info_gpu.rows_per_layer = static_cast<Uint32>(height);

                SDL_GPUTextureRegion region = {};
                region.texture = texture;
                region.w = static_cast<Uint32>(width);
                region.h = static_cast<Uint32>(height);
                region.d = 1;

                SDL_UploadToGPUTexture(copy_pass, &transfer_info_gpu, &region, false);
                SDL_EndGPUCopyPass(copy_pass);
            }
            SDL_SubmitGPUCommandBuffer(cmd_buf);
        }

        // Clean up staging resources
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        SDL_DestroySurface(rgba_surface);

        TEXTURE_INFO_FMT("Successfully loaded GPU texture from {} ({}x{})", filepath, width, height)
        RouenGPUTexture* rouen_tex = new RouenGPUTexture();
        rouen_tex->binding.texture = texture;
        rouen_tex->binding.sampler = getDefaultSampler(device);
        rouen_tex->width = width;
        rouen_tex->height = height;
        return rouen_tex;
    }

    // Function to create a solid color texture using SDL3 GPU API
    inline RouenGPUTexture* createSolidColorTexture(SDL_GPUDevice* device, int width, int height, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
        if (!device) {
            TEXTURE_ERROR("Cannot create texture: device is null")
            return nullptr;
        }

        // Create a temporary surface and fill it with the solid color
        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (!surface) {
            TEXTURE_ERROR_FMT("Failed to create surface: {}", SDL_GetError())
            return nullptr;
        }

        // Fill with color
        Uint32 color_val = SDL_MapSurfaceRGBA(surface, r, g, b, a);
        SDL_FillSurfaceRect(surface, nullptr, color_val);

        // Create GPU texture
        SDL_GPUTextureCreateInfo texture_info = {};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = static_cast<Uint32>(width);
        texture_info.height = static_cast<Uint32>(height);
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;

        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texture_info);
        if (!texture) {
            TEXTURE_ERROR_FMT("Failed to create GPU texture: {}", SDL_GetError())
            SDL_DestroySurface(surface);
            return nullptr;
        }

        // Upload to GPU
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<Uint32>(height * surface->pitch);

        SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (!transfer_buffer) {
            SDL_ReleaseGPUTexture(device, texture);
            SDL_DestroySurface(surface);
            return nullptr;
        }

        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer_buffer, false));
        if (map) {
            std::memcpy(map, surface->pixels, static_cast<size_t>(height) * static_cast<size_t>(surface->pitch));
            SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        }

        SDL_GPUCommandBuffer* cmd_buf = SDL_AcquireGPUCommandBuffer(device);
        if (cmd_buf) {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);
            if (copy_pass) {
                SDL_GPUTextureTransferInfo transfer_info_gpu = {};
                transfer_info_gpu.transfer_buffer = transfer_buffer;
                transfer_info_gpu.offset = 0;
                transfer_info_gpu.pixels_per_row = static_cast<Uint32>(width);
                transfer_info_gpu.rows_per_layer = static_cast<Uint32>(height);

                SDL_GPUTextureRegion region = {};
                region.texture = texture;
                region.w = static_cast<Uint32>(width);
                region.h = static_cast<Uint32>(height);
                region.d = 1;

                SDL_UploadToGPUTexture(copy_pass, &transfer_info_gpu, &region, false);
                SDL_EndGPUCopyPass(copy_pass);
            }
            SDL_SubmitGPUCommandBuffer(cmd_buf);
        }

        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        SDL_DestroySurface(surface);

        TEXTURE_INFO_FMT("Created solid color GPU texture ({}x{})", width, height)
        RouenGPUTexture* rouen_tex = new RouenGPUTexture();
        rouen_tex->binding.texture = texture;
        rouen_tex->binding.sampler = getDefaultSampler(device);
        rouen_tex->width = width;
        rouen_tex->height = height;
        return rouen_tex;
    }

    // Function to create a GPU texture from an SDL_Surface
    inline RouenGPUTexture* createTextureFromSurface(SDL_GPUDevice* device, SDL_Surface* surface) {
        if (!device || !surface) {
            TEXTURE_ERROR("Cannot create texture from surface: device or surface is null");
            return nullptr;
        }

        SDL_Surface* rgba_surface = (surface->format == SDL_PIXELFORMAT_RGBA32) 
            ? surface 
            : SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);

        if (!rgba_surface) {
            TEXTURE_ERROR("Failed to convert surface to RGBA32");
            return nullptr;
        }

        int width = rgba_surface->w;
        int height = rgba_surface->h;

        SDL_GPUTextureCreateInfo texture_info = {};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = static_cast<Uint32>(width);
        texture_info.height = static_cast<Uint32>(height);
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;

        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texture_info);
        if (!texture) {
            TEXTURE_ERROR_FMT("Failed to create GPU texture: {}", SDL_GetError());
            if (rgba_surface != surface) SDL_DestroySurface(rgba_surface);
            return nullptr;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<Uint32>(height * rgba_surface->pitch);

        SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (!transfer_buffer) {
            SDL_ReleaseGPUTexture(device, texture);
            if (rgba_surface != surface) SDL_DestroySurface(rgba_surface);
            return nullptr;
        }

        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer_buffer, false));
        if (map) {
            std::memcpy(map, rgba_surface->pixels, static_cast<size_t>(height) * static_cast<size_t>(rgba_surface->pitch));
            SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        }

        SDL_GPUCommandBuffer* cmd_buf = SDL_AcquireGPUCommandBuffer(device);
        if (cmd_buf) {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);
            if (copy_pass) {
                SDL_GPUTextureTransferInfo transfer_info_gpu = {};
                transfer_info_gpu.transfer_buffer = transfer_buffer;
                transfer_info_gpu.offset = 0;
                transfer_info_gpu.pixels_per_row = static_cast<Uint32>(width);
                transfer_info_gpu.rows_per_layer = static_cast<Uint32>(height);

                SDL_GPUTextureRegion region = {};
                region.texture = texture;
                region.w = static_cast<Uint32>(width);
                region.h = static_cast<Uint32>(height);
                region.d = 1;

                SDL_UploadToGPUTexture(copy_pass, &transfer_info_gpu, &region, false);
                SDL_EndGPUCopyPass(copy_pass);
            }
            SDL_SubmitGPUCommandBuffer(cmd_buf);
        }

        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        if (rgba_surface != surface) SDL_DestroySurface(rgba_surface);

        RouenGPUTexture* rouen_tex = new RouenGPUTexture();
        rouen_tex->binding.texture = texture;
        rouen_tex->binding.sampler = getDefaultSampler(device);
        rouen_tex->width = width;
        rouen_tex->height = height;
        return rouen_tex;
    }

    // Function to safely destroy a GPU texture (defers RouenGPUTexture structure and raw GPU texture cleanup)
    inline void destroyTexture(RouenGPUTexture*& texture) {
        if (texture) {
            {
                std::lock_guard<std::mutex> lock(g_cleanup_mutex);
                g_textures_to_cleanup.push_back({texture, texture->binding.texture});
            }
            texture = nullptr;
            TEXTURE_DEBUG("GPU Texture scheduled for deferred destruction");
        }
    }
}
