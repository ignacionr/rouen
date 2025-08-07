// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "helpers/imgui_include.hpp"

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "main_wnd.hpp"

void main_wnd::run() {
    try {
        // Create deck
        deck main_deck(m_renderer);

        // Register a service to visit all cards of a given type by pointer
        registrar::add<std::function<void(std::string const&, std::function<void(card*)>)>>("for_each_card_ptr",
            std::make_shared<std::function<void(std::string const&, std::function<void(card*)>)>>(
                [&main_deck](std::string const& type, std::function<void(card*)> visitor) {
                    for (const auto& c : main_deck.get_cards()) {
                        // Compare type prefix (e.g., "rss" for main RSS card)
                        if (c && c->get_uri().rfind(type, 0) == 0) {
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

                // Display the font character checker tool
                // m_main_window.render_font_check();

                // Render the deck and get requested fps
                try {
                    m_requested_fps = main_deck.render().requested_fps;
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
            } catch (const std::exception& e) {
                DB_ERROR_FMT("Error in main loop: {}", e.what());
                // Continue to next iteration rather than crashing
            } catch (...) {
                DB_ERROR("Unknown error in main loop");
                // Continue to next iteration rather than crashing
            }
        }
    } catch (const std::exception& e) {
        DB_ERROR_FMT("Fatal error in run(): {}", e.what());
    } catch (...) {
        DB_ERROR("Unknown fatal error in run()");
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
