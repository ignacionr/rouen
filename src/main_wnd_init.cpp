// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "helpers/imgui_include.hpp"

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "helpers/mcp_service.hpp"
#include "helpers/platform_utils.hpp"
#include "main_wnd.hpp"

#ifdef __APPLE__
#include "helpers/mac_menu_helper.hpp"
#endif

bool main_wnd::initialize() {
    try {
        std::cout << "DEBUG: Starting main_wnd::initialize()" << '\n';
        
        // Initialize SDL
        std::cout << "DEBUG: Initializing SDL..." << '\n';
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
            DB_ERROR_FMT("SDL initialization error: {}", SDL_GetError());
            return false;
        }
        std::cout << "DEBUG: SDL initialized successfully" << '\n';
        
        // Initialize SDL_image
        std::cout << "DEBUG: Initializing SDL_image..." << '\n';
        int img_flags = IMG_INIT_JPG | IMG_INIT_PNG;
        int img_init_result = IMG_Init(img_flags);
        
        // Check if at least PNG support is available (minimum requirement)
        if (!(img_init_result & IMG_INIT_PNG)) {
            DB_ERROR_FMT("Error initializing SDL_image PNG support: {}", IMG_GetError());
            SDL_Quit();
            return false;
        }
        std::cout << "DEBUG: SDL_image initialized successfully" << '\n';
        
        // Log warning if JPEG support is not available
        if (!(img_init_result & IMG_INIT_JPG)) {
            DB_WARN("JPEG support not available in SDL_image - some features may be limited");
        }
        
        DB_INFO_FMT("SDL_image initialized successfully with flags: 0x{:X}", img_init_result);

#ifdef __APPLE__
        // On macOS, prevent system from intercepting Cmd+W and other shortcuts
        SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "0");
        // This hint prevents macOS from treating Cmd+key combinations as system shortcuts
        SDL_SetHint("SDL_MAC_NO_SANDBOX", "1");
#endif

        // Create window with SDL - properly configure for high DPI
        std::cout << "DEBUG: Creating SDL window..." << '\n';
        SDL_WindowFlags window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        
        m_window = SDL_CreateWindow(
            "Rouen",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            800, 600,
            window_flags
        );
        if (!m_window) {
            DB_ERROR_FMT("Error creating window: {}", SDL_GetError());
            SDL_Quit();
            return false;
        }
        std::cout << "DEBUG: SDL window created successfully" << '\n';

#ifdef __APPLE__
        rouen::platform::disable_mac_cmd_w_menu_item();
#endif

        // Create renderer
        std::cout << "DEBUG: Creating SDL renderer..." << '\n';
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
        if (!m_renderer) {
            DB_ERROR_FMT("Error creating renderer: {}", SDL_GetError());
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            return false;
        }
        std::cout << "DEBUG: SDL renderer created successfully" << '\n';

        // Register renderer in the registrar
        std::cout << "DEBUG: Registering renderer and services..." << '\n';
        registrar::add<SDL_Renderer*>("main_renderer", std::make_shared<SDL_Renderer*>(m_renderer));
        
        // Register the deferred operations service
        registrar::add<deferred_operations>("deferred_ops", m_deferred_ops);
        
        // Register the MCP service for function calling
        registrar::add<rouen::helpers::mcp_service>("mcp_service", 
            std::make_shared<rouen::helpers::mcp_service>());

        // Initialize and start the API server
        if (m_api_server->initialize()) {
            m_api_server->start("http://127.0.0.1:8081");
            std::cout << "API server initialized and started on port 8081" << '\n';
        } else {
            std::cerr << "Failed to initialize API server" << '\n';
        }

        // Register window services for fit-to-width feature
        registrar::add<std::function<void(int, int)>>("resize_window", 
            std::make_shared<std::function<void(int, int)>>(
                [this](int width, int height) {
                    resize_window(width, height);
                }
            )
        );
        
        registrar::add<std::function<SDL_Window*()>>("get_window", 
            std::make_shared<std::function<SDL_Window*()>>(
                [this]() {
                    return m_window;
                }
            )
        );

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
        std::cout << "DEBUG: Initializing ImGui..." << '\n';
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_imgui_context_created = true;
        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        
        // Disable automatic saving/restoring of window layouts (imgui.ini)
        io.IniFilename = nullptr;
        
        std::cout << "DEBUG: ImGui context created" << '\n';

        // Enable keyboard and mouse controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

#ifdef __APPLE__
        // Enable macOS-specific behavior for shortcuts (Cmd instead of Ctrl)
        io.ConfigMacOSXBehaviors = true;
#endif

        // Setup ImGui style
        setup_dark_theme();

        // Initialize ImGui SDL2 backend
        if (!ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer)) {
            DB_ERROR("Failed to initialize ImGui SDL2 backend!");
            return false;
        }
        m_imgui_sdl_initialized = true;

        // Initialize ImGui SDL Renderer backend
        if (!ImGui_ImplSDLRenderer2_Init(m_renderer)) {
            DB_ERROR("Failed to initialize ImGui SDL Renderer backend!");
            return false;
        }
        m_imgui_renderer_initialized = true;

        // Configure ImGui for high-DPI display handling BEFORE setting up fonts
        configure_highdpi_settings();

        // Now that backends are initialized, setup fonts with proper DPI scaling
        // This must be done after the renderer and backends are fully initialized
        rouen::fonts::setup();

        return true;
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Exception during initialization: {}", e.what());
        
        // Clean up ImGui components that were initialized
        if (m_imgui_renderer_initialized) {
            ImGui_ImplSDLRenderer2_Shutdown();
            m_imgui_renderer_initialized = false;
        }
        
        if (m_imgui_sdl_initialized) {
            ImGui_ImplSDL2_Shutdown();
            m_imgui_sdl_initialized = false;
        }
        
        if (m_imgui_context_created) {
            ImGui::DestroyContext();
            m_imgui_context_created = false;
        }
        
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
        DB_ERROR("Unknown exception during initialization");
        
        // Clean up ImGui components that were initialized
        if (m_imgui_renderer_initialized) {
            ImGui_ImplSDLRenderer2_Shutdown();
            m_imgui_renderer_initialized = false;
        }
        
        if (m_imgui_sdl_initialized) {
            ImGui_ImplSDL2_Shutdown();
            m_imgui_sdl_initialized = false;
        }
        
        if (m_imgui_context_created) {
            ImGui::DestroyContext();
            m_imgui_context_created = false;
        }
        
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
