#include "capture_helper.hpp"
#include <cstring>

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
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;

    SDL_GPUTexture* capture_texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!capture_texture) {
        CAPTURE_ERROR_FMT("Failed to create capture GPU texture: {}", SDL_GetError());
        return nullptr;
    }
    
    // Save the current ImGui context
    ImGuiContext* original_context = ImGui::GetCurrentContext();
    
    // Create a dummy window for SDL3 backend
    SDL_Window* dummy_window = SDL_CreateWindow(
        "Capture Helper Dummy Window", 
        width, height, 
        SDL_WINDOW_HIDDEN
    );
    if (!dummy_window) {
        CAPTURE_ERROR_FMT("Failed to create dummy window: {}", SDL_GetError());
        SDL_ReleaseGPUTexture(device, capture_texture);
        return nullptr;
    }
    
    // Create a new ImGui context
    ImGuiContext* capture_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(capture_context);
    
    // Initialize ImGui IO parameters
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = 1.0f / 60.0f;
    
    // Initialize ImGui backends for this context
    ImGui_ImplSDL3_InitForSDLGPU(dummy_window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.GpuDevice = device;
    init_info.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&init_info);
    
    try {
        // Start a new ImGui frame
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
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

        // Acquire command buffer and begin render pass onto our texture
        SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        if (cmdbuf) {
            // Prepare the ImGui draw data
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
    } 
    catch (const std::exception& e) {
        CAPTURE_ERROR_FMT("Exception during capture: {}", e.what());
    }
    
    // Cleanup the temporary ImGui context
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(capture_context);
    
    // Restore the original ImGui context
    ImGui::SetCurrentContext(original_context);
    
    // Destroy the dummy window
    SDL_DestroyWindow(dummy_window);
    
    // Return the texture with the captured content wrapped in RouenGPUTexture
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
    int height
) {
    if (!device || !texture) return nullptr;
    
    SDL_GPUTransferBufferCreateInfo transferInfo = {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = width * height * 4;
    SDL_GPUTransferBuffer* download_buffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!download_buffer) return nullptr;
    
    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    if (cmdbuf) {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdbuf);
        if (copyPass) {
            SDL_GPUTextureRegion sourceRegion = {};
            sourceRegion.texture = texture->binding.texture;
            sourceRegion.w = width;
            sourceRegion.h = height;
            sourceRegion.d = 1;

            SDL_GPUTextureTransferInfo destInfo = {};
            destInfo.transfer_buffer = download_buffer;
            destInfo.offset = 0;
            destInfo.pixels_per_row = width;
            destInfo.rows_per_layer = height;

            SDL_DownloadFromGPUTexture(copyPass, &sourceRegion, &destInfo);
            SDL_EndGPUCopyPass(copyPass);
        }
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
        if (fence) {
            SDL_WaitForGPUFences(device, true, &fence, 1);
            SDL_ReleaseGPUFence(device, fence);
        }
    }
    
    SDL_Surface* surface = nullptr;
    void* map = SDL_MapGPUTransferBuffer(device, download_buffer, false);
    if (map) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            std::memcpy(surface->pixels, map, width * height * 4);
        }
        SDL_UnmapGPUTransferBuffer(device, download_buffer);
    }
    
    SDL_ReleaseGPUTransferBuffer(device, download_buffer);
    return surface;
}

} // namespace rouen::helpers
