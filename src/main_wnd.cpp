#include "main_wnd.hpp"
#include "cards/interface/deck.hpp"

// Add STB implementation defines
#define STB_RECT_PACK_IMPLEMENTATION
#include <imgui/imstb_rectpack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <imgui/imstb_truetype.h>

#include "fonts.hpp"

main_wnd::main_wnd() 
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_deferred_ops(std::make_shared<deferred_operations>())
    , m_done(false)
    , m_immediate(false)
    , m_requested_fps(1)
{
    // register an extractor for keystrokes
    registrar::add<std::function<std::string()>>(
        "keystrokes", 
        std::make_shared<std::function<std::string()>>(
            [this]() {
                auto result = keystrokes_;
                keystrokes_.clear();
                return result;
            }
        )
    );
}

main_wnd::~main_wnd() {
    // Cleanup
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // Remove renderer from registrar before destroying it
    registrar::remove<SDL_Renderer*>("main_renderer");

    // Remove the keystrokes extractor
    registrar::remove<std::function<std::string()>>("keystrokes");

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    
    IMG_Quit();
    SDL_Quit();
}

bool main_wnd::initialize() {
    try {
        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "Error: " << SDL_GetError() << std::endl;
            return false;
        }
        
        // Initialize SDL_image
        int img_flags = IMG_INIT_JPG | IMG_INIT_PNG;
        if ((IMG_Init(img_flags) & img_flags) != img_flags) {
            std::cerr << "Error initializing SDL_image: " << IMG_GetError() << std::endl;
            SDL_Quit();
            return false;
        }

        // Create window with SDL
        SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        m_window = SDL_CreateWindow(
            "Rouen",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            800, 600,
            window_flags
        );
        if (!m_window) {
            std::cerr << "Error creating window: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return false;
        }

        // Create renderer
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
        if (!m_renderer) {
            std::cerr << "Error creating renderer: " << SDL_GetError() << std::endl;
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            return false;
        }

        // Register renderer in the registrar
        registrar::add<SDL_Renderer*>("main_renderer", std::make_shared<SDL_Renderer*>(m_renderer));
        
        // Register the deferred operations service
        registrar::add<deferred_operations>("deferred_ops", m_deferred_ops);

        // Register exit function
        registrar::add<std::function<bool()>>("exit", std::make_shared<std::function<bool()>>(
            [this]() {
                auto was_exiting {m_done};
                m_done = true;
                return was_exiting;
            }
        ));
        registrar::add<std::function<bool()>>("quitting", std::make_shared<std::function<bool()>>(
            [this]() {
                return m_done;
            }
        ));

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        (void)io;

        // Enable keyboard and mouse controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        rouen::fonts::setup();

        // Setup ImGui style
        setup_dark_theme();

        // Initialize ImGui SDL2 backend
        if (!ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer)) {
            std::cerr << "Failed to initialize ImGui SDL2 backend!" << std::endl;
            return false;
        }

        // Initialize ImGui SDL Renderer backend
        if (!ImGui_ImplSDLRenderer2_Init(m_renderer)) {
            std::cerr << "Failed to initialize ImGui SDL Renderer backend!" << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during initialization: " << e.what() << std::endl;
        
        // Clean up any resources that may have been allocated
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
        
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        
        IMG_Quit();
        SDL_Quit();
        
        return false;
    } catch (...) {
        std::cerr << "Unknown exception during initialization" << std::endl;
        
        // Clean up any resources that may have been allocated
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
        
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        
        IMG_Quit();
        SDL_Quit();
        
        return false;
    }
}

void main_wnd::run() {
    try {
        // Create deck
        deck main_deck(m_renderer);

        while (!m_done) {
            try {
                // Process events
                if (!process_events()) {
                    break;
                }

                // Start a new ImGui frame
                ImGui_ImplSDLRenderer2_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();

                // Render the deck and get requested fps
                try {
                    m_requested_fps = main_deck.render().requested_fps;
                } catch (const std::exception& e) {
                    std::cerr << "Error during deck rendering: " << e.what() << std::endl;
                    // Continue execution rather than crashing
                } catch (...) {
                    std::cerr << "Unknown error during deck rendering" << std::endl;
                    // Continue execution rather than crashing
                }

                // Render ImGui
                ImGui::Render();
                SDL_SetRenderDrawColor(m_renderer, 40, 40, 40, 255);  // Changed to dark gray background
                SDL_RenderClear(m_renderer);
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
                
                // Process any deferred operations
                process_deferred_operations();
                
                SDL_RenderPresent(m_renderer);
            } catch (const std::exception& e) {
                std::cerr << "Error in main loop: " << e.what() << std::endl;
                // Continue to next iteration rather than crashing
            } catch (...) {
                std::cerr << "Unknown error in main loop" << std::endl;
                // Continue to next iteration rather than crashing
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error in run(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown fatal error in run()" << std::endl;
    }
}

bool main_wnd::process_events() {
    try {
        // clear the keyboard buffer
        keystrokes_.clear();

        // Poll events
        SDL_Event event;
        if (!m_immediate) {
            SDL_WaitEventTimeout(nullptr, 1000/m_requested_fps);
        }
        else {
            m_immediate = false;
        }
        
        while (SDL_PollEvent(&event)) {
            try {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) {
                    m_done = true;
                    return false;
                }
                else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && 
                        event.window.windowID == SDL_GetWindowID(m_window)) {
                    m_done = true;
                    return false;
                }
                // run shortcut key handlers
                else if (event.type == SDL_KEYDOWN) {
                    // F11 toggles fullscreen
                    if (event.key.keysym.sym == SDLK_F11) {
                        Uint32 flags = SDL_GetWindowFlags(m_window);
                        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                            SDL_SetWindowFullscreen(m_window, 0);
                        } else {
                            SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                        }
                    }
                    // ctrl+shift+q exits the application
                    else if (event.key.keysym.sym == SDLK_q && 
                        (event.key.keysym.mod & KMOD_CTRL) &&
                        (event.key.keysym.mod & KMOD_SHIFT)) {
                        m_done = true;
                        return false;
                    }
                    else {
                        // Store keystrokes
                        if (event.key.keysym.sym < 256) {
                            keystrokes_ += static_cast<char>(event.key.keysym.sym);
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error processing event: " << e.what() << std::endl;
                // Continue to the next event rather than crashing
            } catch (...) {
                std::cerr << "Unknown error processing event" << std::endl;
                // Continue to the next event rather than crashing
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in process_events: " << e.what() << std::endl;
        return true; // Continue execution rather than stopping
    } catch (...) {
        std::cerr << "Unknown error in process_events" << std::endl;
        return true; // Continue execution rather than stopping
    }
}

void main_wnd::process_deferred_operations() {
    if (m_deferred_ops->has_operations()) {
        m_deferred_ops->process_queue(m_renderer);
        m_immediate = true;
    }
}

void main_wnd::setup_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Set rounded corners for windows and other elements
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    
    // Set window padding
    style.WindowPadding = ImVec2(10, 10);
    
    // Adjust spacing
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(4, 4);
    
    // Start with dark style
    ImGui::StyleColorsDark();
    
    // Now customize specific colors
    ImVec4* colors = style.Colors;
    
    // Main colors
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);  // Darker window background
    colors[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.29f, 0.50f);    // Subtle border
    
    // Text
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);      // Slightly off-white for better readability
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    
    // Headers (title bars)
    colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.17f, 0.75f);
    
    // Menu bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    
    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.35f, 0.45f, 1.00f);    // Changed: More blue-oriented accent color
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.45f, 0.60f, 1.00f); // Changed: Lighter blue when hovered
    colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.55f, 0.70f, 1.00f);  // Changed: Brighter blue when active
    
    // Checkboxes, radio buttons
    colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.53f, 0.71f, 1.00f);  // Slightly adjusted to match new accent color
    
    // Sliders, drag controls
    colors[ImGuiCol_SliderGrab] = ImVec4(0.37f, 0.53f, 0.71f, 1.00f);  // Slightly adjusted to match new accent color
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.57f, 0.67f, 0.86f, 1.00f);
    
    // Frame backgrounds (checkbox, radio, slider, input fields)
    colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.31f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.32f, 0.38f, 1.00f);
    
    // Text editor and input areas - ensure high contrast for text
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.53f, 0.71f, 0.50f);  // Blue tint for selected text
}