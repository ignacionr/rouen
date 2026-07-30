// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "deferred_operations.hpp"

// 3. All other includes
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "main_wnd.hpp"
#include "helpers/theme_manager.hpp"
#include "mcp_host.hpp"
#include "registrar.hpp"
#include "texture_helper.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <exception>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <memory>

#ifdef __APPLE__
#include "helpers/mac_menu_helper.hpp"
#endif

bool main_wnd::initialize() {
    try {
        std::cout << "DEBUG: Starting main_wnd::initialize()" << '\n';
        
        // Initialize SDL
        std::cout << "DEBUG: Initializing SDL..." << '\n';
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
            DB_ERROR_FMT("SDL initialization error: {}", SDL_GetError());
            return false;
        }
        std::cout << "DEBUG: SDL initialized successfully" << '\n';

#ifdef __APPLE__
        // On macOS, prevent system from intercepting Cmd+W and other shortcuts
        SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "0");
        // This hint prevents macOS from treating Cmd+key combinations as system shortcuts
        SDL_SetHint("SDL_MAC_NO_SANDBOX", "1");
#endif

        // Create window with SDL - properly configure for high DPI
        std::cout << "DEBUG: Creating SDL window..." << '\n';
        const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        
        m_window = SDL_CreateWindow(
            "Rouen",
            800, 600,
            window_flags
        );
        if (!m_window) {
            DB_ERROR_FMT("Error creating window: {}", SDL_GetError());
            SDL_Quit();
            return false;
        }
        std::cout << "DEBUG: SDL window created successfully" << '\n';
        SDL_StartTextInput(m_window);


#ifdef __APPLE__
        rouen::platform::disable_mac_cmd_w_menu_item();
#endif

        // Create GPU device
        std::cout << "DEBUG: Creating SDL GPU device..." << '\n';
        m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
        if (!m_device) {
            DB_ERROR_FMT("Error creating GPU device: {}", SDL_GetError());
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            return false;
        }
        std::cout << "DEBUG: SDL GPU device created successfully" << '\n';

        // Claim window for GPU device
        if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
            DB_ERROR_FMT("Error claiming window for GPU device: {}", SDL_GetError());
            SDL_DestroyGPUDevice(m_device);
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            return false;
        }
        std::cout << "DEBUG: Claimed window for GPU device successfully" << '\n';

        // Register GPU device and services
        std::cout << "DEBUG: Registering GPU device and services..." << '\n';
        registrar::add<SDL_GPUDevice*>("main_gpu_device", std::make_shared<SDL_GPUDevice*>(m_device));
        registrar::add<SDL_GPUDevice*>("main_renderer", std::make_shared<SDL_GPUDevice*>(m_device));
        TextureHelper::g_gpu_device = m_device;
        
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

        registrar::add<std::function<void()>>("expand_to_full_width", 
            std::make_shared<std::function<void()>>(
                [this]() {
                    expand_to_full_width();
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
        rouen::theme::theme_manager::get().apply_theme_to_imgui();

        // Initialize ImGui SDL3 backend
        if (!ImGui_ImplSDL3_InitForSDLGPU(m_window)) {
            DB_ERROR("Failed to initialize ImGui SDL3 backend!");
            return false;
        }
        m_imgui_sdl_initialized = true;

        // Initialize ImGui SDL GPU3 backend
        SDL_GPUTextureFormat swapchain_format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window);
        ImGui_ImplSDLGPU3_InitInfo init_info = {};
        init_info.GpuDevice = m_device;
        init_info.ColorTargetFormat = swapchain_format;
        init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&init_info)) {
            DB_ERROR("Failed to initialize ImGui SDL GPU3 backend!");
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
            ImGui_ImplSDLGPU3_Shutdown();
            m_imgui_renderer_initialized = false;
        }
        
        if (m_imgui_sdl_initialized) {
            ImGui_ImplSDL3_Shutdown();
            m_imgui_sdl_initialized = false;
        }
        
        if (m_imgui_context_created) {
            ImGui::DestroyContext();
            m_imgui_context_created = false;
        }
        
        // Clean up any resources that may have been allocated
        if (m_device) {
            SDL_DestroyGPUDevice(m_device);
            m_device = nullptr;
        }
        
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        
        SDL_Quit();
        
        return false;
    } catch (...) {
        DB_ERROR("Unknown exception during initialization");
        
        // Clean up ImGui components that were initialized
        if (m_imgui_renderer_initialized) {
            ImGui_ImplSDLGPU3_Shutdown();
            m_imgui_renderer_initialized = false;
        }
        
        if (m_imgui_sdl_initialized) {
            ImGui_ImplSDL3_Shutdown();
            m_imgui_sdl_initialized = false;
        }
        
        if (m_imgui_context_created) {
            ImGui::DestroyContext();
            m_imgui_context_created = false;
        }
        
        // Clean up any resources that may have been allocated
        if (m_device) {
            SDL_DestroyGPUDevice(m_device);
            m_device = nullptr;
        }
        
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        
        SDL_Quit();
        
        return false;
    }
}
