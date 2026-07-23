// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "helpers/imgui_include.hpp"

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "helpers/media_player.hpp"
#include "hosts/video_feed_host.hpp"
#include "main_wnd.hpp"
#include "registrar.hpp"
#include "../external/IconsMaterialDesign.h"

#ifdef __APPLE__
#include "helpers/mac_menu_helper.hpp"
#endif

void main_wnd::run() {
    try {
        // Create deck (m_device is passed as the renderer context, mapped via macro/typedef in imgui_include.hpp)
        auto main_deck = std::make_shared<deck>(m_device);

        // Register the deck as a service for window close handling
        registrar::add<deck>("deck", main_deck);

        // Register a service to signal when a card close was handled this frame
        registrar::add<std::function<void()>>("signal_card_close_handled",
            std::make_shared<std::function<void()>>(
                [this]() { m_card_close_handled_this_frame = true; }
            )
        );

        // Register a service to visit all cards of a given type by pointer
        registrar::add<std::function<void(std::string const&, std::function<void(card*)>)>>("for_each_card_ptr",
            std::make_shared<std::function<void(std::string const&, std::function<void(card*)>)>>(
                [main_deck](std::string const& type, const std::function<void(card*)>& visitor) {
                    for (const auto& c : main_deck->get_cards()) {
                        // Compare type prefix (e.g., "rss" for main RSS card)
                        if (c && c->get_uri().starts_with(type)) {
                            visitor(c.get());
                        }
                    }
                }
            )
        );

        while (!m_done) {
            try {
                auto fs_item = media_player::get_active_fullscreen_item();
                if (fs_item && fs_item->checkMediaStatus() && fs_item->has_video) {
                    m_requested_fps = 60;
                    m_immediate = true;

                    if (!process_events()) {
                        break;
                    }

                    if (!media_player::has_active_fullscreen_item()) {
                        continue;
                    }

                    ImGui_ImplSDLGPU3_NewFrame();
                    ImGui_ImplSDL3_NewFrame();
                    ImGui::NewFrame();

                    m_card_close_handled_this_frame = false;

                    ImGuiViewport* vp = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(vp->Pos);
                    ImGui::SetNextWindowSize(vp->Size);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));

                    ImGuiWindowFlags fs_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoBringToFrontOnFocus;

                    if (ImGui::Begin("##FullscreenVideoOverlay", nullptr, fs_flags)) {
                        ImTextureID tex = fs_item->get_texture_id(m_device);
                        if (tex) {
                            float win_w = vp->Size.x;
                            float win_h = vp->Size.y;
                            float aspect = static_cast<float>(media_player_item::kWidth) / static_cast<float>(media_player_item::kHeight);

                            float draw_w, draw_h;
                            if (win_w / win_h > aspect) {
                                draw_h = win_h;
                                draw_w = draw_h * aspect;
                            } else {
                                draw_w = win_w;
                                draw_h = draw_w / aspect;
                            }
                            float draw_x = (win_w - draw_w) * 0.5f;
                            float draw_y = (win_h - draw_h) * 0.5f;

                            ImGui::SetCursorPos(ImVec2(draw_x, draw_y));
                            ImGui::Image(tex, ImVec2(draw_w, draw_h));

                            if ((ImGui::IsItemHovered() || ImGui::IsWindowHovered()) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (ImGui::GetIO().MousePos.y < win_h - 24.0f) {
                                    media_player::clear_active_fullscreen_item();
                                }
                            }

                            // Seeking in full-window media mode via Left and Right arrow keys
                            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                                double current = fs_item->position.load();
                                fs_item->seekTo(std::max(0.0, current - 5.0));
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                                double current = fs_item->position.load();
                                double dur = fs_item->duration.load();
                                double target_limit = (dur > 0.0) ? dur : (current + 5.0);
                                fs_item->seekTo(std::min(target_limit, current + 5.0));
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                                fs_item->togglePause();
                            }

                            // Small icon button to detach media player into a separate OS window
                            float btn_w = 36.0f;
                            float btn_h = 36.0f;
                            float btn_margin = 16.0f;
                            ImGui::SetCursorPos(ImVec2(win_w - btn_w - btn_margin, btn_margin));
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 1.0f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.8f, 0.9f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                            if (ImGui::Button(ICON_MD_OPEN_IN_NEW "##DetachMediaBtn", ImVec2(btn_w, btn_h))) {
                                media_player::set_detached_item(fs_item);
                                media_player::clear_active_fullscreen_item();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Detach media player into separate OS window");
                            }
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor(4);

                            // Render progress line for full-window media player overlay
                            media_player::draw_full_window_progress_line(*fs_item, win_w, win_h);
                        }
                    }
                    ImGui::End();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);

                    // Render video-output UI overlay of all cards in the deck (e.g. notifications card)
                    try {
                        for (const auto& card_ptr : main_deck->get_cards()) {
                            if (card_ptr) {
                                card_ptr->render_video_ui();
                            }
                        }
                    } catch (...) {}

                    ImGui::Render();

                    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
                    if (cmdbuf) {
                        Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdbuf);
                        SDL_GPUColorTargetInfo color_target = {};
                        SDL_AcquireGPUSwapchainTexture(cmdbuf, m_window, &color_target.texture, nullptr, nullptr);
                        if (color_target.texture) {
                            color_target.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 1.0f };
                            color_target.load_op = SDL_GPU_LOADOP_CLEAR;
                            color_target.store_op = SDL_GPU_STOREOP_STORE;

                            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, nullptr);
                            if (render_pass) {
                                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmdbuf, render_pass);
                                SDL_EndGPURenderPass(render_pass);
                            }
                        }
                        SDL_SubmitGPUCommandBuffer(cmdbuf);
                    }

                    process_deferred_operations();
                    process_detached_window();
                    continue;
                } else if (fs_item) {
                    media_player::clear_active_fullscreen_item();
                }

                auto video_feed = rouen::hosts::VideoFeedHost::get_host();
                bool is_casting = (video_feed && video_feed->is_running());
                if (is_casting) {
                    m_requested_fps = std::max(m_requested_fps, 30);
                }

                // Process events
                if (!process_events()) {
                    break;
                }

                // Start a new ImGui frame
                ImGui_ImplSDLGPU3_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();

                // Reset the card close handled flag for this frame
                m_card_close_handled_this_frame = false;

                // Render the deck and get requested fps
                try {
                    m_requested_fps = main_deck->render().requested_fps;
                } catch (const std::exception& e) {
                    DB_ERROR_FMT("Error during deck rendering: {}", e.what());
                } catch (...) {
                    DB_ERROR("Unknown error during deck rendering");
                }

                if (media_player::is_any_playing_non_cast() || media_player::has_detached_item()) {
                    m_requested_fps = std::max(m_requested_fps, 60);
                }

                if (is_casting) {
                    m_requested_fps = std::max(m_requested_fps, 30);
                }

                // Render ImGui
                ImGui::Render();

                bool should_draw_main = true;
                if (is_casting) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_main_render_time).count();
                    if (elapsed_ms < 1000) {
                        should_draw_main = false;
                    } else {
                        m_last_main_render_time = now;
                    }
                }

                if (should_draw_main) {
                    // Acquire command buffer and swapchain texture to render onto the window
                    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
                    if (cmdbuf) {
                        // Prepare draw data (required in SDL_GPU backend before render pass)
                        Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdbuf);
                        SDL_GPUColorTargetInfo color_target = {};
                        SDL_AcquireGPUSwapchainTexture(cmdbuf, m_window, &color_target.texture, nullptr, nullptr);
                        
                        if (color_target.texture) {
                            color_target.clear_color = SDL_FColor{ 40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f }; // Dark gray background
                            color_target.load_op = SDL_GPU_LOADOP_CLEAR;
                            color_target.store_op = SDL_GPU_STOREOP_STORE;

                            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, nullptr);
                            if (render_pass) {
                                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmdbuf, render_pass);
                                SDL_EndGPURenderPass(render_pass);
                            }
                        }
                        SDL_SubmitGPUCommandBuffer(cmdbuf);
                    }
                }
                
                // Process any deferred operations
                process_deferred_operations();

                // Process detached media player window rendering
                process_detached_window();

                // Render offscreen ImGui pass for video feed host if active
                try {
                    if (video_feed && video_feed->is_running()) {
                        m_requested_fps = std::max(m_requested_fps, 30);
                        video_feed->render_video_frame(m_device);
                    }
                } catch (...) {}
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error in main loop: {}", e.what());
            } catch (...) {
                DB_ERROR("Unknown error in main loop");
            }
        }

        // Stop all active media players and video feed host before SDL destruction
        try {
            media_player::stopAll();
            auto video_feed = rouen::hosts::VideoFeedHost::get_host();
            if (video_feed) {
                video_feed->stop();
            }
        } catch (...) {}
        
        // Cleanup - unregister services
        try {
            registrar::remove<deck>("deck");
            registrar::remove<std::function<void(std::string const&, std::function<void(card*)>)>>("for_each_card_ptr");
        } catch (const std::exception& e) {
            DB_WARN_FMT("Error during service cleanup: {}", e.what());
        }
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Fatal error in run(): {}", e.what());
    } catch (...) {
        DB_ERROR("Unknown fatal error in run()");
    }
}

bool main_wnd::process_events() {
#ifdef __APPLE__
    rouen::platform::disable_mac_cmd_w_menu_item();
#endif

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
                ImGui_ImplSDL3_ProcessEvent(&event);
                
                if (event.type == SDL_EVENT_QUIT) {
                    m_done = true;
                    return false;
                }
                
                // SDL3 Window close event
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    if (m_detached_window && event.window.windowID == SDL_GetWindowID(m_detached_window)) {
                        media_player::clear_detached_item();
                        SDL_DestroyWindow(m_detached_window);
                        m_detached_window = nullptr;
                        continue;
                    }

                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        // Check if this close event was triggered by Cmd+W or Ctrl+W shortcut
                        SDL_Keymod mod = SDL_GetModState();
                        bool modifier_down = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
                        
                        if (modifier_down) {
                            // Do not close the app; clear the editor instead
                            try {
                                auto clear_editor_func = registrar::get<std::function<void()>>("clear_editor");
                                if (clear_editor_func) {
                                    (*clear_editor_func)();
                                }
                            } catch (...) {
                                (void)0;
                            }
                            continue;
                        }
                        
                        m_done = true;
                        return false;
                    }
                }
                
                // Handle window resize/move/pixel size change to refresh DPI settings
                if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                    event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                    event.type == SDL_EVENT_WINDOW_MOVED) {
                    
                    std::cout << "Window event detected, updating display settings..." << '\n';
                    
                    // Update ImGui display settings immediately
                    update_imgui_display_settings();
                    
                    // Check if we moved to a different display with different DPI
                    rouen::fonts::refresh_dpi();
                    
                    // Only rebuild fonts if there's a significant DPI change
                    if (rouen::fonts::needs_font_rebuild()) {
                        std::cout << "Significant DPI change detected, rebuilding fonts..." << '\n';
                        // Clear current fonts and rebuild
                        ImGui::GetIO().Fonts->Clear();
                        rouen::fonts::setup();
                        
                        // Clear the rebuild flag
                        rouen::fonts::clear_font_rebuild_flag();
                        
                        // Font atlas is managed automatically in SDL3 GPU backend
                    }
                }
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (m_detached_window && event.key.windowID == SDL_GetWindowID(m_detached_window)) {
                        auto detached_item = media_player::get_detached_item();
                        if (detached_item) {
                            if (event.key.key == SDLK_ESCAPE) {
                                media_player::clear_detached_item();
                                SDL_DestroyWindow(m_detached_window);
                                m_detached_window = nullptr;
                            } else if (event.key.key == SDLK_SPACE) {
                                detached_item->togglePause();
                            } else if (event.key.key == SDLK_LEFT) {
                                double current = detached_item->position.load();
                                detached_item->seekTo(std::max(0.0, current - 5.0));
                            } else if (event.key.key == SDLK_RIGHT) {
                                double current = detached_item->position.load();
                                double dur = detached_item->duration.load();
                                double target_limit = (dur > 0.0) ? dur : (current + 5.0);
                                detached_item->seekTo(std::min(target_limit, current + 5.0));
                            }
                        }
                        continue;
                    }

                    if (event.key.key == SDLK_ESCAPE) {
                        if (media_player::has_active_fullscreen_item()) {
                            media_player::clear_active_fullscreen_item();
                        }
                    }
                    // F11 toggles fullscreen
                    else if (event.key.key == SDLK_F11) {
                        Uint64 flags = SDL_GetWindowFlags(m_window);
                        if (flags & SDL_WINDOW_FULLSCREEN) {
                            SDL_SetWindowFullscreen(m_window, false);
                        } else {
                            SDL_SetWindowFullscreen(m_window, true);
                        }
                    }
                    // ctrl+shift+q (or cmd+shift+q on macOS) exits the application
                    else if (event.key.key == SDLK_Q && 
                        event.key.mod & SDL_KMOD_SHIFT &&
                        (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                        m_done = true;
                        return false;
                    }
                    // ctrl+w (or cmd+w on macOS) clears/closes the editor
                    else if (event.key.key == SDLK_W && 
                        (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                        try {
                            auto clear_editor_func = registrar::get<std::function<void()>>("clear_editor");
                            if (clear_editor_func) {
                                (*clear_editor_func)();
                            }
                        } catch (...) {
                            (void)0;
                        }
                    }
                    else {
                        // Store keystrokes
                        if (event.key.key < 256) {
                            keystrokes_ += static_cast<char>(event.key.key);
                        }
                    }
                }
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error processing event: {}", e.what());
            } catch (...) {
                DB_ERROR("Unknown error processing event");
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Error in process_events: {}", e.what());
        return true;
    } catch (...) {
        DB_ERROR("Unknown error in process_events");
        return true;
    }
}

void main_wnd::process_detached_window() {
    auto detached_item = media_player::get_detached_item();
    if (!detached_item) {
        if (m_detached_window) {
            if (m_detached_imgui_ctx) {
                ImGui::DestroyContext(m_detached_imgui_ctx);
                m_detached_imgui_ctx = nullptr;
            }
            SDL_DestroyWindow(m_detached_window);
            m_detached_window = nullptr;
        }
        return;
    }

    // Check media status: close second window when media ends or is stopped
    // (Note: is_paused leaves checkMediaStatus() true, so pausing will not close window)
    if (!detached_item->checkMediaStatus() || !detached_item->has_video) {
        media_player::clear_detached_item();
        if (m_detached_window) {
            if (m_detached_imgui_ctx) {
                ImGui::DestroyContext(m_detached_imgui_ctx);
                m_detached_imgui_ctx = nullptr;
            }
            SDL_DestroyWindow(m_detached_window);
            m_detached_window = nullptr;
        }
        return;
    }

    // Create detached OS Window if not created yet
    if (!m_detached_window) {
        int default_w = 960;
        int default_h = 540;
        std::string title = detached_item->item_title.empty() ? "Rouen Media Player" : detached_item->item_title;
        m_detached_window = SDL_CreateWindow(
            title.c_str(),
            default_w, default_h,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
        );
        if (m_detached_window) {
            if (!SDL_ClaimWindowForGPUDevice(m_device, m_detached_window)) {
                DB_ERROR_FMT("Error claiming detached window for GPU device: {}", SDL_GetError());
                SDL_DestroyWindow(m_detached_window);
                m_detached_window = nullptr;
                media_player::clear_detached_item();
                return;
            }
        } else {
            DB_ERROR_FMT("Error creating detached window: {}", SDL_GetError());
            media_player::clear_detached_item();
            return;
        }
    }

    ImTextureID tex = detached_item->get_texture_id(m_device);
    if (!tex) return;

    RouenGPUTexture* rtex = reinterpret_cast<RouenGPUTexture*>(static_cast<uintptr_t>(tex));
    if (!rtex || !rtex->binding.texture) return;

    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (cmdbuf) {
        SDL_GPUColorTargetInfo color_target = {};
        SDL_AcquireGPUSwapchainTexture(cmdbuf, m_detached_window, &color_target.texture, nullptr, nullptr);
        if (color_target.texture) {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(m_detached_window, &w, &h);
            float win_w = static_cast<float>(w > 0 ? w : 960);
            float win_h = static_cast<float>(h > 0 ? h : 540);
            float aspect = static_cast<float>(media_player_item::kWidth) / static_cast<float>(media_player_item::kHeight);

            float draw_w, draw_h;
            if (win_w / win_h > aspect) {
                draw_h = win_h;
                draw_w = draw_h * aspect;
            } else {
                draw_w = win_w;
                draw_h = draw_w / aspect;
            }
            float draw_x = (win_w - draw_w) * 0.5f;
            float draw_y = (win_h - draw_h) * 0.5f;

            // 1. Blit video frame texture onto swapchain
            SDL_GPUBlitInfo blit_info = {};
            blit_info.source.texture = rtex->binding.texture;
            blit_info.source.w = media_player_item::kWidth;
            blit_info.source.h = media_player_item::kHeight;
            blit_info.destination.texture = color_target.texture;
            blit_info.destination.x = static_cast<Uint32>(std::max(0.0f, draw_x));
            blit_info.destination.y = static_cast<Uint32>(std::max(0.0f, draw_y));
            blit_info.destination.w = static_cast<Uint32>(std::max(0.0f, draw_w));
            blit_info.destination.h = static_cast<Uint32>(std::max(0.0f, draw_h));
            blit_info.load_op = SDL_GPU_LOADOP_CLEAR;
            blit_info.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 1.0f };
            blit_info.filter = SDL_GPU_FILTER_LINEAR;

            SDL_BlitGPUTexture(cmdbuf, &blit_info);

            // 2. Render Overlay Cards UI onto m_detached_window using secondary ImGui context
            ImGuiContext* main_ctx = ImGui::GetCurrentContext();
            if (main_ctx) {
                if (!m_detached_imgui_ctx) {
                    ImFontAtlas* shared_fonts = main_ctx->IO.Fonts;
                    m_detached_imgui_ctx = ImGui::CreateContext(shared_fonts);
                }

                if (m_detached_imgui_ctx) {
                    m_detached_imgui_ctx->IO.BackendRendererUserData = main_ctx->IO.BackendRendererUserData;
                    m_detached_imgui_ctx->IO.BackendPlatformUserData = main_ctx->IO.BackendPlatformUserData;

                    ImGui::SetCurrentContext(m_detached_imgui_ctx);

                    ImGuiIO& io = ImGui::GetIO();
                    io.DisplaySize = ImVec2(win_w, win_h);
                    io.DeltaTime = 1.0f / 60.0f;

                    if (main_ctx->IO.Fonts && main_ctx->IO.Fonts->TexID) {
                        io.Fonts->TexID = main_ctx->IO.Fonts->TexID;
                    }

                    // Feed mouse events for detached window into secondary ImGui context
                    int win_pt_w = 0, win_pt_h = 0;
                    SDL_GetWindowSize(m_detached_window, &win_pt_w, &win_pt_h);
                    float scale_x = (win_pt_w > 0) ? (win_w / static_cast<float>(win_pt_w)) : 1.0f;
                    float scale_y = (win_pt_h > 0) ? (win_h / static_cast<float>(win_pt_h)) : 1.0f;

                    SDL_Window* mouse_focus = SDL_GetMouseFocus();
                    if (mouse_focus == m_detached_window) {
                        float raw_mx = 0.0f, raw_my = 0.0f;
                        Uint32 mouse_buttons = SDL_GetMouseState(&raw_mx, &raw_my);
                        io.AddMousePosEvent(raw_mx * scale_x, raw_my * scale_y);
                        io.AddMouseButtonEvent(0, (mouse_buttons & SDL_BUTTON_LMASK) != 0);
                    } else {
                        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                        io.AddMouseButtonEvent(0, false);
                    }

                    ImGui::NewFrame();

                    try {
                        auto main_deck = registrar::get<deck>("deck");
                        if (main_deck) {
                            for (const auto& card_ptr : main_deck->get_cards()) {
                                if (card_ptr) {
                                    card_ptr->render_video_ui();
                                }
                            }
                        }
                    } catch (...) {}

                    // Double clicking detached window re-attaches to full-window mode
                    if (mouse_focus == m_detached_window && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (io.MousePos.y < win_h - 24.0f) {
                            media_player::set_active_fullscreen_item(detached_item);
                        }
                    }

                    // Render progress line for detached window
                    media_player::draw_full_window_progress_line(*detached_item, win_w, win_h);

                    ImGui::Render();

                    Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdbuf);

                    SDL_GPUColorTargetInfo ui_target = {};
                    ui_target.texture = color_target.texture;
                    ui_target.load_op = SDL_GPU_LOADOP_LOAD;
                    ui_target.store_op = SDL_GPU_STOREOP_STORE;

                    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &ui_target, 1, nullptr);
                    if (render_pass) {
                        ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmdbuf, render_pass);
                        SDL_EndGPURenderPass(render_pass);
                    }

                    ImGui::SetCurrentContext(main_ctx);
                }
            }
        }
        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }
}
