#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_sdlrenderer2.h>
#include <SDL2/SDL_image.h> // Add this include

#define STB_RECT_PACK_IMPLEMENTATION
#include <imgui/imstb_rectpack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <imgui/imstb_truetype.h>
#include <SDL2/SDL.h>
#include <iostream>

#include "cards/deck.hpp"
#include "registrar.hpp" // Add include for registrar
#include "helpers/deferred_operations.hpp" // Add include for deferred operations

int main() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }
    
    // Initialize SDL_image - Add this
    int img_flags = IMG_INIT_JPG | IMG_INIT_PNG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        std::cerr << "Error initializing SDL_image: " << IMG_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // Create window with SDL
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow(
        "Rouen",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        window_flags
    );
    if (!window) {
        std::cerr << "Error creating window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Error creating renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Register renderer in the registrar
    registrar::add<SDL_Renderer*>("main_renderer", std::make_shared<SDL_Renderer*>(renderer));
    
    // Create and register the deferred operations service
    auto deferred_ops = std::make_shared<deferred_operations>();
    registrar::add<deferred_operations>("deferred_ops", deferred_ops);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    // Enable keyboard and mouse controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable gamepad controls (optional)

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Initialize ImGui SDL2 backend
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        std::cerr << "Failed to initialize ImGui SDL2 backend!" << std::endl;
        return -1;
    }

    // Initialize ImGui SDL Renderer backend
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend!" << std::endl;
        return -1;
    }

    // Pass the renderer to your deck
    deck deck(renderer);

    // Main loop
    bool done {false};
    bool immediate {true};
    while (!done) {
        // Poll events
        SDL_Event event;
        if (immediate) {
            SDL_WaitEvent(nullptr);
            immediate = false;
        }
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                done = true;
            }
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && 
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
            // run shortcut key handlers
            else if (event.type == SDL_KEYDOWN) {
                // F11 toggles fullscreen
                if (event.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                        SDL_SetWindowFullscreen(window, 0);
                    } else {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                }
                // ctrl+q exits the application
                else if (event.key.keysym.sym == SDLK_q && (event.key.keysym.mod & KMOD_CTRL)) {
                    done = true;
                }
            }
        }


        // Start a new ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        deck.render();

        // Render ImGui
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        
        // Process any deferred operations after the main frame is complete
        if (deferred_ops->has_operations()) {
            deferred_ops->process_queue(renderer);
            immediate = true;
        }
        
        SDL_RenderPresent(renderer);
    }

    // Cleanup
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // Remove renderer from registrar before destroying it
    registrar::remove<SDL_Renderer*>("main_renderer");

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit(); // Add this before SDL_Quit()
    SDL_Quit();

    return 0;
}
