#pragma once

#include <functional>
#include <iostream>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_sdlrenderer2.h>
#include <SDL2/SDL.h>
#include "../registrar.hpp"
#include "../helpers/deferred_operations.hpp"

namespace rouen::animation {
    using renderer_t = std::function<void()>;
    using transition_t = std::function<void()>;
    struct base {
        base(const ImVec2& size, const renderer_t& frame, const transition_t& transition, SDL_Renderer* renderer = nullptr)
            : size(size), from_texture(nullptr), to_texture(nullptr), owns_renderer(false) {
                // Get the current SDL renderer - use the provided one if available
                sdl_renderer = renderer;
                
                if (!sdl_renderer) {
                    try {
                        // Try to get the renderer from the registrar
                        auto renderer_ptr = registrar::get<SDL_Renderer*>("main_renderer");
                        sdl_renderer = *renderer_ptr;
                    } catch (const std::runtime_error& e) {
                        std::cerr << "Failed to get renderer from registrar: " << e.what() << std::endl;
                        return;
                    }
                }

                // Create target textures
                from_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, 
                                                SDL_TEXTUREACCESS_TARGET, 
                                                static_cast<int>(size.x), static_cast<int>(size.y));
                to_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, 
                                              SDL_TEXTUREACCESS_TARGET, 
                                              static_cast<int>(size.x), static_cast<int>(size.y));
                
                if (!from_texture || !to_texture) {
                    cleanup();
                    return;
                }

                // Set blend mode for the textures
                SDL_SetTextureBlendMode(from_texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureBlendMode(to_texture, SDL_BLENDMODE_BLEND);

                // Queue the texture rendering operations to be executed after the frame
                try {
                    auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
                    
                    // Queue the "from" state rendering
                    deferred_ops->queue([this, frame, transition]() {
                        // Save the current render target
                        SDL_Texture* originalTarget = SDL_GetRenderTarget(this->sdl_renderer);
                        
                        // Set the render target to the "from" texture
                        SDL_SetRenderTarget(this->sdl_renderer, this->from_texture);
                        SDL_SetRenderDrawColor(this->sdl_renderer, 0, 0, 0, 0);
                        SDL_RenderClear(this->sdl_renderer);
                        
                        // Start a new ImGui frame for this operation
                        ImGui_ImplSDLRenderer2_NewFrame();
                        ImGui_ImplSDL2_NewFrame();
                        ImGui::NewFrame();
                        // Set up a window for the content
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                        
                        ImGui::SetNextWindowSize(this->size);
                        ImGui::SetNextWindowPos(ImVec2(0, 0));
                        // Render "from" content
                        frame();
                        ImGui::PopStyleVar(2);
                        ImGui::Render();
                        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
                        
                        
                        // make the transition
                        transition();

                        // Set the render target to the "to" texture
                        SDL_SetRenderTarget(this->sdl_renderer, this->to_texture);
                        SDL_SetRenderDrawColor(this->sdl_renderer, 0, 0, 0, 0);
                        SDL_RenderClear(this->sdl_renderer);
                        
                        // Start a new ImGui frame for this operation
                        ImGui_ImplSDLRenderer2_NewFrame();
                        ImGui_ImplSDL2_NewFrame();
                        ImGui::NewFrame();

                        // Set up a window for the content
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                        
                        ImGui::SetNextWindowSize(this->size);
                        ImGui::SetNextWindowPos(ImVec2(0, 0));
                        
                        // Render "to" content
                        frame();
                        ImGui::PopStyleVar(2);
                        ImGui::Render();
                        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
                        
                        
                        // Restore the original render target
                        SDL_SetRenderTarget(this->sdl_renderer, originalTarget);
                    });
                    
                } catch (const std::runtime_error& e) {
                    std::cerr << "Failed to get deferred operations service: " << e.what() << std::endl;
                }
            }

        ~base() {
            cleanup();
        }

        virtual bool render() = 0;

        void cleanup() {
            if (from_texture) {
                SDL_DestroyTexture(from_texture);
                from_texture = nullptr;
            }
            if (to_texture) {
                SDL_DestroyTexture(to_texture);
                to_texture = nullptr;
            }
            if (owns_renderer) {
                SDL_DestroyRenderer(sdl_renderer);
                sdl_renderer = nullptr;
            }
        }

    protected:
        ImVec2 size;
        SDL_Texture* from_texture;
        SDL_Texture* to_texture;
        SDL_Renderer* sdl_renderer;
        bool owns_renderer;
    };
}