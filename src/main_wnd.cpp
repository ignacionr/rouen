#include "main_wnd.hpp"
#include "cards/deck.hpp"

// Add STB implementation defines
#define STB_RECT_PACK_IMPLEMENTATION
#include <imgui/imstb_rectpack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <imgui/imstb_truetype.h>

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

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        (void)io;

        // Enable keyboard and mouse controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        // Setup ImGui style
        ImGui::StyleColorsDark();

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
                SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
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