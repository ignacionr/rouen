// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "helpers/imgui_include.hpp"

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "hosts/video_feed_host.hpp"
#include "main_wnd.hpp"
#include "registrar.hpp"

#ifdef __APPLE__
#include "helpers/mac_menu_helper.hpp"
#endif

void main_wnd::run() {
    try {
        // Create deck
        auto main_deck = std::make_shared<deck>(m_renderer);

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
                // Process events
                if (!process_events()) {
                    break;
                }

                // Start a new ImGui frame
                ImGui_ImplSDLRenderer2_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();

                // Reset the card close handled flag for this frame
                m_card_close_handled_this_frame = false;

                // Display the font character checker tool
                // m_main_window.render_font_check();

                // Render the deck and get requested fps
                try {
                    m_requested_fps = main_deck->render().requested_fps;
                } catch (const std::exception& e) {
                    DB_ERROR_FMT("Error during deck rendering: {}", e.what());
                    // Continue execution rather than crashing
                } catch (...) {
                    DB_ERROR("Unknown error during deck rendering");
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

                // Render offscreen ImGui pass for video feed host if active
                try {
                    auto video_feed = rouen::hosts::VideoFeedHost::get_host();
                    if (video_feed && video_feed->is_running()) {
                        m_requested_fps = std::max(m_requested_fps, 30);
                        video_feed->render_video_frame(m_renderer);
                    }
                } catch (...) {}
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error in main loop: {}", e.what());
                // Continue to next iteration rather than crashing
            } catch (...) {
                DB_ERROR("Unknown error in main loop");
                // Continue to next iteration rather than crashing
            }
        }

        // Stop all active media players and video feed host before SDL_Renderer destruction
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
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) {
                    m_done = true;
                    return false;
                }
                if (event.type == SDL_WINDOWEVENT) {
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE && 
                        event.window.windowID == SDL_GetWindowID(m_window)) {
                        // Check if this close event was triggered by Cmd+W or Ctrl+W shortcut
                        SDL_Keymod mod = SDL_GetModState();
                        bool modifier_down = (mod & (KMOD_CTRL | KMOD_GUI));
                        
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
                    
                    // Handle window resize to refresh DPI settings
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        event.window.event == SDL_WINDOWEVENT_MOVED) {
                        
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
                            
                            // Rebuild ImGui font atlas
                            ImGui_ImplSDLRenderer2_DestroyFontsTexture();
                            ImGui_ImplSDLRenderer2_CreateFontsTexture();
                        }
                    }
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
                    // ctrl+shift+q (or cmd+shift+q on macOS) exits the application
                    else if (event.key.keysym.sym == SDLK_q && 
                        event.key.keysym.mod & KMOD_SHIFT &&
                        (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
                        m_done = true;
                        return false;
                    }
                    // ctrl+w (or cmd+w on macOS) clears/closes the editor
                    else if (event.key.keysym.sym == SDLK_w && 
                        (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
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
                        if (event.key.keysym.sym < 256) {
                            keystrokes_ += static_cast<char>(event.key.keysym.sym);
                        }
                    }
                }
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error processing event: {}", e.what());
                // Continue to the next event rather than crashing
            } catch (...) {
                DB_ERROR("Unknown error processing event");
                // Continue to the next event rather than crashing
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Error in process_events: {}", e.what());
        return true; // Continue execution rather than stopping
    } catch (...) {
        DB_ERROR("Unknown error in process_events");
        return true; // Continue execution rather than stopping
    }
}
