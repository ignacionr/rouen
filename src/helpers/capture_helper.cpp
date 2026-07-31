#include "capture_helper.hpp"
#include "../fonts.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "registrar.hpp"
#include "texture_helper.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>

namespace rouen::helpers {

RouenGPUTexture* capture_imgui(
    int width, 
    int height, 
    const std::function<void()>& render_callback,
    SDL_GPUDevice* device
) {
    // Get SDL GPU device if not provided
    if (!device) {
        try {
            auto device_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            device = *device_ptr;
        } catch (const std::runtime_error& e) {
            CAPTURE_ERROR_FMT("Failed to get GPU device from registrar: {}", e.what());
            return nullptr;
        }
    }

    // Create a target GPU texture
    SDL_GPUTextureCreateInfo texture_info = {};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = static_cast<Uint32>(width);
    texture_info.height = static_cast<Uint32>(height);
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;

    SDL_GPUTexture* capture_texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!capture_texture) {
        CAPTURE_ERROR_FMT("Failed to create capture GPU texture: {}", SDL_GetError());
        return nullptr;
    }
    
    // Save the current ImGui context
    ImGuiContext* original_context = ImGui::GetCurrentContext();

    if (!render_callback && original_context) {
        // Capture current active main app window draw data
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data) {
            SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
            if (cmdbuf) {
                Imgui_ImplSDLGPU3_PrepareDrawData(draw_data, cmdbuf);
                SDL_GPUColorTargetInfo color_target = {};
                color_target.texture = capture_texture;
                color_target.clear_color = SDL_FColor{ 40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f };
                color_target.load_op = SDL_GPU_LOADOP_CLEAR;
                color_target.store_op = SDL_GPU_STOREOP_STORE;

                SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, nullptr);
                if (render_pass) {
                    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmdbuf, render_pass);
                    SDL_EndGPURenderPass(render_pass);
                }
                SDL_SubmitGPUCommandBuffer(cmdbuf);
            }
        }
        RouenGPUTexture* rouen_tex = new RouenGPUTexture();
        rouen_tex->binding.texture = capture_texture;
        rouen_tex->binding.sampler = TextureHelper::getDefaultSampler(device);
        rouen_tex->width = width;
        rouen_tex->height = height;
        return rouen_tex;
    }

    // Offscreen component rendering using a secondary ImGui context
    ImFontAtlas* shared_fonts = (original_context && original_context->IO.Fonts) ? original_context->IO.Fonts : nullptr;
    ImGuiContext* capture_context = ImGui::CreateContext(shared_fonts);
    ImGui::SetCurrentContext(capture_context);

    ImGuiIO& io = ImGui::GetIO();
    bool created_backend_data = false;
    if (original_context && original_context->IO.BackendRendererUserData) {
        io.BackendRendererUserData = original_context->IO.BackendRendererUserData;
        io.BackendPlatformUserData = original_context->IO.BackendPlatformUserData;
    } else if (device) {
        ImGui_ImplSDLGPU3_InitInfo init_info = {};
        init_info.GpuDevice = device;
        init_info.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ImGui_ImplSDLGPU3_Init(&init_info);
        created_backend_data = true;
    }
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = 1.0f / 60.0f;

    try {
        for (int frame = 0; frame < 2; ++frame) {
            ImGui_ImplSDLGPU3_NewFrame();
            ImGui::NewFrame();
            
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
            
            if (render_callback) {
                render_callback();
            }
            
            ImGui::PopStyleVar(2);
            ImGui::Render();
        }

        SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        if (cmdbuf) {
            Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdbuf);
            SDL_GPUColorTargetInfo color_target = {};
            color_target.texture = capture_texture;
            color_target.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 0.0f };
            color_target.load_op = SDL_GPU_LOADOP_CLEAR;
            color_target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, nullptr);
            if (render_pass) {
                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmdbuf, render_pass);
                SDL_EndGPURenderPass(render_pass);
            }
            SDL_SubmitGPUCommandBuffer(cmdbuf);
        }
    } catch (const std::exception& e) {
        CAPTURE_ERROR_FMT("Exception during capture: {}", e.what());
    }

    if (created_backend_data) {
        ImGui_ImplSDLGPU3_Shutdown();
    }
    ImGui::DestroyContext(capture_context);
    if (original_context) {
        ImGui::SetCurrentContext(original_context);
    }

    RouenGPUTexture* rouen_tex = new RouenGPUTexture();
    rouen_tex->binding.texture = capture_texture;
    rouen_tex->binding.sampler = TextureHelper::getDefaultSampler(device);
    rouen_tex->width = width;
    rouen_tex->height = height;
    return rouen_tex;
}

SDL_Surface* download_gpu_texture(
    SDL_GPUDevice* device,
    RouenGPUTexture* texture,
    int width,
    int height,
    SDL_GPUCommandBuffer* cmdbuf
) {
    if (!device || !texture || !texture->binding.texture) return nullptr;

    static SDL_GPUTransferBuffer* s_download_buffer = nullptr;
    static Uint32 s_buffer_capacity = 0;

    Uint32 const required_size = static_cast<Uint32>(width * height * 4);
    if (!s_download_buffer || s_buffer_capacity < required_size) {
        if (s_download_buffer) {
            SDL_ReleaseGPUTransferBuffer(device, s_download_buffer);
            s_download_buffer = nullptr;
        }
        SDL_GPUTransferBufferCreateInfo transferInfo = {};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = required_size;
        s_download_buffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        s_buffer_capacity = required_size;
        if (!s_download_buffer) return nullptr;
    }

    bool own_cmdbuf = false;
    if (!cmdbuf) {
        cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        own_cmdbuf = true;
    }

    if (cmdbuf) {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdbuf);
        if (copyPass) {
            SDL_GPUTextureRegion sourceRegion = {};
            sourceRegion.texture = texture->binding.texture;
            sourceRegion.w = static_cast<Uint32>(width);
            sourceRegion.h = static_cast<Uint32>(height);
            sourceRegion.d = 1;

            SDL_GPUTextureTransferInfo destInfo = {};
            destInfo.transfer_buffer = s_download_buffer;
            destInfo.offset = 0;
            destInfo.pixels_per_row = static_cast<Uint32>(width);
            destInfo.rows_per_layer = static_cast<Uint32>(height);

            SDL_DownloadFromGPUTexture(copyPass, &sourceRegion, &destInfo);
            SDL_EndGPUCopyPass(copyPass);
        }
        if (own_cmdbuf) {
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
            if (fence) {
                SDL_WaitForGPUFences(device, true, &fence, 1);
                SDL_ReleaseGPUFence(device, fence);
            }
        }
    }

    SDL_Surface* surface = nullptr;
    void* map = SDL_MapGPUTransferBuffer(device, s_download_buffer, false);
    if (map) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            uint8_t* dst = static_cast<uint8_t*>(surface->pixels);
            const uint8_t* src = static_cast<const uint8_t*>(map);
            if (surface->pitch == width * 4) {
                std::memcpy(dst, src, static_cast<size_t>(width * height * 4));
            } else {
                for (int y = 0; y < height; ++y) {
                    std::memcpy(dst + y * surface->pitch, src + y * (width * 4), static_cast<size_t>(width * 4));
                }
            }
        }
        SDL_UnmapGPUTransferBuffer(device, s_download_buffer);
    }

    return surface;
}

SDL_Surface* trim_surface_bottom_padding(SDL_Surface* surface) {
    if (!surface || surface->h <= 20 || surface->w <= 0 || !surface->pixels) {
        return surface;
    }

    int const w = surface->w;
    int const h = surface->h;
    uint8_t* pixels = static_cast<uint8_t*>(surface->pixels);
    int const pitch = surface->pitch;

    // Read background pixel color from the bottom-left corner (0, h-1)
    int bg_y = h - 1;
    uint8_t bg_r = pixels[bg_y * pitch + 0];
    uint8_t bg_g = pixels[bg_y * pitch + 1];
    uint8_t bg_b = pixels[bg_y * pitch + 2];

    int last_content_y = 0;
    for (int y = h - 1; y >= 0; --y) {
        bool row_has_content = false;
        const uint8_t* row = pixels + y * pitch;
        for (int x = 0; x < w; ++x) {
            uint8_t r = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t b = row[x * 4 + 2];
            uint8_t a = row[x * 4 + 3];

            if (a > 0) {
                int dr = std::abs(static_cast<int>(r) - static_cast<int>(bg_r));
                int dg = std::abs(static_cast<int>(g) - static_cast<int>(bg_g));
                int db = std::abs(static_cast<int>(b) - static_cast<int>(bg_b));

                if (dr > 4 || dg > 4 || db > 4) {
                    row_has_content = true;
                    break;
                }
            }
        }
        if (row_has_content) {
            last_content_y = y;
            break;
        }
    }

    int cropped_h = std::min(h, std::max(last_content_y + 8, 100)); // Add 8px padding at bottom
    if (cropped_h >= h) {
        return surface;
    }

    SDL_Surface* cropped = SDL_CreateSurface(w, cropped_h, surface->format);
    if (!cropped) {
        return surface;
    }

    for (int y = 0; y < cropped_h; ++y) {
        std::memcpy(
            static_cast<uint8_t*>(cropped->pixels) + y * cropped->pitch,
            static_cast<uint8_t*>(surface->pixels) + y * surface->pitch,
            static_cast<size_t>(w * 4)
        );
    }

    SDL_DestroySurface(surface);
    return cropped;
}

} // namespace rouen::helpers
