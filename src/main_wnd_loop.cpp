// 1. Standard includes in alphabetic order
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers
#include "cards/interface/card.hpp"

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "fonts.hpp"
#include "helpers/debug.hpp"
#include "helpers/media_player.hpp"
#include "helpers/adlib_engine.hpp"
#include "helpers/capture_helper.hpp"
#include "hosts/video_feed_host.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "main_wnd.hpp"
#include "media_player_item.hpp"
#include "registrar.hpp"
#include "../external/IconsMaterialDesign.h"
#include "texture_helper.hpp"

#ifdef __APPLE__
#include "helpers/mac_menu_helper.hpp"
#endif

namespace {
static std::string s_detached_toast_msg;
static std::chrono::steady_clock::time_point s_detached_toast_expire;
static std::chrono::steady_clock::time_point s_input_boost_until;

static void set_detached_toast(const std::string& msg) {
    s_detached_toast_msg = msg;
    s_detached_toast_expire = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

static bool toggle_overlay_by_index(int target_idx) {
    if (target_idx < 0) return false;
    try {
        auto main_deck = registrar::get<deck>("deck");
        if (main_deck) {
            int overlay_count = 0;
            for (const auto& card_ptr : main_deck->get_cards()) {
                if (card_ptr && card_ptr->has_video_overlay()) {
                    if (overlay_count == target_idx) {
                        card_ptr->video_overlay_visible = !card_ptr->video_overlay_visible;
                        std::string card_name = card_ptr->window_title;
                        auto hash_pos = card_name.find("###");
                        if (hash_pos != std::string::npos) {
                            card_name = card_name.substr(0, hash_pos);
                        }
                        if (card_name.empty()) {
                            card_name = card_ptr->get_uri();
                        }
                        std::string status = card_ptr->video_overlay_visible ? "Visible" : "Hidden";
                        set_detached_toast(std::format("[{}] {}: {}", target_idx + 1, card_name, status));
                        return true;
                    }
                    overlay_count++;
                }
            }
        }
    } catch (...) {
        // Ignore errors checking detached card overlay toggling
    }
    return false;
}

static void render_detached_toast(float win_w, float /*win_h*/) {
    if (!s_detached_toast_msg.empty() && std::chrono::steady_clock::now() < s_detached_toast_expire) {
        ImGui::SetNextWindowPos(ImVec2(win_w * 0.5f, 24.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.12f, 0.20f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.7f, 1.0f, 0.8f));

        if (ImGui::Begin("##DetachedOverlayToast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs)) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%s", s_detached_toast_msg.c_str());
            ImGui::End();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
}
}

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
                if (fs_item && fs_item->checkMediaStatus()) {
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
                    
                    // Save actual scale and override to 1.0 for layout phase
                    auto& io = ImGui::GetIO();
                    float const actual_scale_x = io.DisplayFramebufferScale.x;
                    float const actual_scale_y = io.DisplayFramebufferScale.y;
                    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
                    
                    ImGui::NewFrame();

                    m_card_close_handled_this_frame = false;

                    ImGuiViewport* vp = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(vp->Pos);
                    ImGui::SetNextWindowSize(vp->Size);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));

                    ImGuiWindowFlags const fs_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoBringToFrontOnFocus;

                    if (ImGui::Begin("##FullscreenVideoOverlay", nullptr, fs_flags)) {
                        float const win_w = vp->Size.x;
                        float const win_h = vp->Size.y;

                        float const btn_w = 36.0f;
                        float const btn_h = 36.0f;
                        float const btn_margin = 16.0f;

                        ImVec2 const mouse_pos = ImGui::GetIO().MousePos;
                        bool const on_progress_bar = (mouse_pos.y >= win_h - 24.0f);
                        bool const on_detach_button = (mouse_pos.x >= win_w - (btn_w + btn_margin + 4.0f) && mouse_pos.y <= (btn_h + btn_margin + 4.0f));

                        // Hide mouse cursor when hovering the media player content area
                        if (ImGui::IsWindowHovered() && !on_progress_bar && !on_detach_button) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
                        }

                        // Single click to pause/resume
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            if (ImGui::IsWindowHovered() && !on_progress_bar && !on_detach_button) {
                                fs_item->togglePause();
                            }
                        }

                        if (fs_item->has_video.load()) {
                            ImTextureID const tex = fs_item->get_texture_id(m_device);
                            if (tex) {
                                float const aspect = static_cast<float>(media_player_item::kWidth) / static_cast<float>(media_player_item::kHeight);

                                float draw_w = 0.0f;
                                float draw_h = 0.0f;
                                if (win_w / win_h > aspect) {
                                    draw_h = win_h;
                                    draw_w = draw_h * aspect;
                                } else {
                                    draw_w = win_w;
                                    draw_h = draw_w / aspect;
                                }
                                float const draw_x = (win_w - draw_w) * 0.5f;
                                float const draw_y = (win_h - draw_h) * 0.5f;

                                ImGui::SetCursorPos(ImVec2(draw_x, draw_y));
                                ImGui::Image(tex, ImVec2(draw_w, draw_h));
                            }
                        } else {
                            // Render audio background visualization
                            media_player::draw_full_window_audio_visualization(*fs_item, win_w, win_h);

                            // Render VU-meters on center-bottom
                            float const vu_w = std::min(win_w * 0.65f, 460.0f);
                            float const vu_h = 110.0f;
                            float const vu_x = (win_w - vu_w) * 0.5f;
                            float const vu_y = win_h - vu_h - 38.0f;
                            ImGui::SetCursorPos(ImVec2(vu_x, vu_y));
                            media_player::draw_vintage_110_vu_meter(
                                fs_item->get_vu_level_l(), fs_item->get_vu_level_r(),
                                fs_item->get_vu_watermark_l(), fs_item->get_vu_watermark_r(),
                                vu_w, vu_h, /*is_lit=*/true
                            );
                        }

                        if ((ImGui::IsItemHovered() || ImGui::IsWindowHovered()) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            if (ImGui::GetIO().MousePos.y < win_h - 24.0f) {
                                media_player::clear_active_fullscreen_item();
                            }
                        }

                        // Seeking in full-window media mode via Left and Right arrow keys
                        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                            double const current = fs_item->position.load();
                            fs_item->seekTo(std::max(0.0, current - 5.0));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                            double const current = fs_item->position.load();
                            double const dur = fs_item->duration.load();
                            double const target_limit = (dur > 0.0) ? dur : (current + 5.0);
                            fs_item->seekTo(std::min(target_limit, current + 5.0));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                            fs_item->togglePause();
                        }

                        // Small icon button to detach media player into a separate OS window
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
                    ImGui::End();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);

                    // Render video-output UI overlay of all cards in the deck (e.g. notifications card)
                    try {
                        for (const auto& card_ptr : main_deck->get_cards()) {
                            if (card_ptr && card_ptr->video_overlay_visible && card_ptr->has_video_overlay()) {
                                card_ptr->render_video_ui();
                            }
                        }
                    } catch (...) {
                        // Ignore video overlay render errors for detached card
                    }
                    render_detached_toast(io.DisplaySize.x, io.DisplaySize.y);

                    // Restore actual scale before rendering so backends work with correct physical coordinates
                    io.DisplayFramebufferScale = ImVec2(actual_scale_x, actual_scale_y);
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
                }
                if (fs_item) {
                    media_player::clear_active_fullscreen_item();
                }

                auto video_feed = rouen::hosts::VideoFeedHost::get_host();
                bool const is_casting = (video_feed && video_feed->is_running());
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
                
                // Save actual scale and override to 1.0 for layout phase
                auto& io = ImGui::GetIO();
                float const actual_scale_x = io.DisplayFramebufferScale.x;
                float const actual_scale_y = io.DisplayFramebufferScale.y;
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
                
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

                bool const is_detached_active = (m_detached_window != nullptr) || media_player::is_detached_mode_active() || media_player::has_detached_item();

                if (is_detached_active) {
                    m_requested_fps = std::max(m_requested_fps, 60);
                } else if (media_player::is_any_playing_non_cast()) {
                    m_requested_fps = std::max(m_requested_fps, 60);
                }

                if (is_casting) {
                    m_requested_fps = std::max(m_requested_fps, 60);
                }

                // Update texture for all active playing media items on every 60 FPS frame
                try {
                    std::lock_guard<std::recursive_mutex> const lock(media_player::items_mutex());
                    for (auto& [id, item_ptr] : media_player::items()) {
                        if (item_ptr && item_ptr->is_playing && !item_ptr->is_paused.load()) {
                            item_ptr->get_texture_id(m_device);
                        }
                    }
                } catch (...) {
                    // Ignore texture pre-fetch errors
                }

                // Restore actual scale before rendering so backends work with correct physical coordinates
                io.DisplayFramebufferScale = ImVec2(actual_scale_x, actual_scale_y);
                
                // Render ImGui
                ImGui::Render();

                bool should_draw_main = true;
                if (is_detached_active) {
                    auto now = std::chrono::steady_clock::now();
                    bool const user_active = (now < s_input_boost_until);
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_main_render_time).count();
                    
                    if (!user_active && elapsed_ms < 1000) {
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
                } catch (...) {
                    // Ignore video feed render frame exceptions
                }
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
        } catch (...) {
            // Ignore cleanup errors on shutdown
        }
        
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
        bool const is_detached_active = (m_detached_window != nullptr) || media_player::is_detached_mode_active() || media_player::has_detached_item();
        auto now_time = std::chrono::steady_clock::now();
        bool const user_recently_active = (now_time < s_input_boost_until);

        if (!m_immediate) {
            bool const is_media_playing = media_player::is_any_playing_non_cast() || media_player_item::is_cast_active.load();
            int effective_fps = 60;
            if (is_detached_active && !user_recently_active && !is_media_playing) {
                effective_fps = 1;
            } else {
                effective_fps = is_media_playing ? 60 : std::max(1, m_requested_fps);
            }
            SDL_WaitEventTimeout(nullptr, 1000 / effective_fps);
        }
        else {
            m_immediate = false;
        }
        
        static bool s_swallowed_escape_down = false;
        while (SDL_PollEvent(&event)) {
            try {
                bool is_main_window_event = true;
                if (m_detached_window) {
                    Uint32 const detached_id = SDL_GetWindowID(m_detached_window);
                    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
                        if (event.window.windowID == detached_id) is_main_window_event = false;
                    } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                        if (event.key.windowID == detached_id) is_main_window_event = false;
                    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                        if (event.motion.windowID == detached_id) is_main_window_event = false;
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                        if (event.button.windowID == detached_id) is_main_window_event = false;
                    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                        if (event.wheel.windowID == detached_id) is_main_window_event = false;
                    }
                }

                if (is_main_window_event) {
                    switch (event.type) {
                        case SDL_EVENT_MOUSE_MOTION:
                        case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        case SDL_EVENT_MOUSE_BUTTON_UP:
                        case SDL_EVENT_MOUSE_WHEEL:
                        case SDL_EVENT_KEY_DOWN:
                        case SDL_EVENT_KEY_UP:
                        case SDL_EVENT_TEXT_INPUT:
                        case SDL_EVENT_DROP_FILE:
                        case SDL_EVENT_DROP_TEXT:
                        case SDL_EVENT_WINDOW_MOUSE_ENTER:
                            s_input_boost_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                            m_immediate = true;
                            break;
                        default:
                            break;
                    }
                }
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                    if (media_player::has_active_fullscreen_item()) {
                        media_player::clear_active_fullscreen_item();
                        s_swallowed_escape_down = true;
                        continue;
                    }
                }
                if (event.type == SDL_EVENT_KEY_UP && event.key.key == SDLK_ESCAPE) {
                    if (s_swallowed_escape_down) {
                        s_swallowed_escape_down = false;
                        continue;
                    }
                }

                ImGui_ImplSDL3_ProcessEvent(&event);
                
                if (event.type == SDL_EVENT_QUIT) {
                    if (!rouen::helpers::AdLibEngine::instance().is_recording()) {
                        m_done = true;
                        return false;
                    }
                }
                
                // SDL3 Window close event
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    if (m_detached_window && event.window.windowID == SDL_GetWindowID(m_detached_window)) {
                        media_player::close_detached_window();
                        if (m_detached_imgui_ctx) {
                            ImGuiContext* main_ctx = ImGui::GetCurrentContext();
                            if (main_ctx == m_detached_imgui_ctx) main_ctx = nullptr;
                            ImGui::SetCurrentContext(m_detached_imgui_ctx);
                            ImGui::DestroyContext(m_detached_imgui_ctx);
                            m_detached_imgui_ctx = nullptr;
                            if (main_ctx) ImGui::SetCurrentContext(main_ctx);
                        }
                        SDL_DestroyWindow(m_detached_window);
                        m_detached_window = nullptr;
                        continue;
                    }

                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        // Check if this close event was triggered by Cmd+W or Ctrl+W shortcut
                        SDL_Keymod const mod = SDL_GetModState();
                        bool const modifier_down = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
                        
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
                    bool const is_adlib_active = rouen::helpers::AdLibEngine::instance().is_active();
                    bool const is_fullscreen_active = media_player::has_active_fullscreen_item();

                    if (m_detached_window && event.key.windowID == SDL_GetWindowID(m_detached_window)) {
                        auto detached_item = media_player::get_detached_item();
                        if (event.key.key == SDLK_ESCAPE) {
                            if (is_adlib_active) {
                                rouen::helpers::AdLibEngine::instance().stop();
                            } else {
                                media_player::close_detached_window();
                                if (m_detached_imgui_ctx) {
                                    ImGuiContext* main_ctx = ImGui::GetCurrentContext();
                                    if (main_ctx == m_detached_imgui_ctx) main_ctx = nullptr;
                                    ImGui::SetCurrentContext(m_detached_imgui_ctx);
                                    ImGui::DestroyContext(m_detached_imgui_ctx);
                                    m_detached_imgui_ctx = nullptr;
                                    if (main_ctx) ImGui::SetCurrentContext(main_ctx);
                                }
                                SDL_DestroyWindow(m_detached_window);
                                m_detached_window = nullptr;
                            }
                        } else if (event.key.key == SDLK_SPACE) {
                            if (is_adlib_active) {
                                rouen::helpers::AdLibEngine::instance().toggle_pause();
                            } else if (detached_item) {
                                detached_item->togglePause();
                            }
                        } else if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_N) {
                            if (is_adlib_active) {
                                rouen::helpers::AdLibEngine::instance().next_stage();
                            } else if (detached_item) {
                                double const current = detached_item->position.load();
                                double const dur = detached_item->duration.load();
                                double const target_limit = (dur > 0.0) ? dur : (current + 5.0);
                                detached_item->seekTo(std::min(target_limit, current + 5.0));
                            }
                        } else if (event.key.key == SDLK_LEFT && detached_item) {
                            double const current = detached_item->position.load();
                            detached_item->seekTo(std::max(0.0, current - 5.0));
                        } else {
                            int overlay_target_idx = -1;
                            if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                                overlay_target_idx = static_cast<int>(event.key.key - SDLK_1);
                            } else if (event.key.key >= SDLK_KP_1 && event.key.key <= SDLK_KP_9) {
                                overlay_target_idx = static_cast<int>(event.key.key - SDLK_KP_1);
                            }

                            if (overlay_target_idx >= 0) {
                                toggle_overlay_by_index(overlay_target_idx);
                            }
                        }
                        continue;
                    }

                    // Key overlay toggling on main window when detached window or fullscreen media is active
                    if ((is_detached_active || is_fullscreen_active) && !ImGui::GetIO().WantCaptureKeyboard) {
                        bool const no_mods = !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT));
                        if (no_mods) {
                            int overlay_target_idx = -1;
                            if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                                overlay_target_idx = static_cast<int>(event.key.key - SDLK_1);
                            } else if (event.key.key >= SDLK_KP_1 && event.key.key <= SDLK_KP_9) {
                                overlay_target_idx = static_cast<int>(event.key.key - SDLK_KP_1);
                            }

                            if (overlay_target_idx >= 0) {
                                toggle_overlay_by_index(overlay_target_idx);
                                continue;
                            }
                        }
                    }

                    if (event.key.key == SDLK_ESCAPE) {
                        if (media_player::has_active_fullscreen_item()) {
                            media_player::clear_active_fullscreen_item();
                        }
                    }
                    // F11 toggles fullscreen
                    else if (event.key.key == SDLK_F11) {
                        Uint64 const flags = SDL_GetWindowFlags(m_window);
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
    bool const is_adlib_active = rouen::helpers::AdLibEngine::instance().is_active();
    bool const is_detached_mode = media_player::is_detached_mode_active();

    if (!is_detached_mode && !detached_item && !is_adlib_active) {
        if (m_detached_window) {
            if (m_detached_imgui_ctx) {
                ImGuiContext* main_ctx = ImGui::GetCurrentContext();
                if (main_ctx == m_detached_imgui_ctx) main_ctx = nullptr;
                ImGui::SetCurrentContext(m_detached_imgui_ctx);
                ImGui::DestroyContext(m_detached_imgui_ctx);
                m_detached_imgui_ctx = nullptr;
                if (main_ctx) ImGui::SetCurrentContext(main_ctx);
            }
            SDL_DestroyWindow(m_detached_window);
            m_detached_window = nullptr;
        }
        return;
    }

    // Check media status when a standalone media item is attached (non-adlib)
    if (detached_item && !is_adlib_active && !detached_item->checkMediaStatus()) {
        media_player::clear_detached_item();
        detached_item = nullptr;
    }

    // Create detached OS Window if not created yet
    if (!m_detached_window) {
        int const default_w = 960;
        int const default_h = 540;
        std::string const title = (detached_item && !detached_item->item_title.empty()) ? detached_item->item_title : "Rouen Presentation";
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
                media_player::close_detached_window();
                return;
            }
            media_player::set_detached_mode_active(true);
        } else {
            DB_ERROR_FMT("Error creating detached window: {}", SDL_GetError());
            media_player::close_detached_window();
            return;
        }
    } else {
        static std::string s_last_title;
        std::string const current_title = (detached_item && !detached_item->item_title.empty()) ? detached_item->item_title : "Rouen Presentation";
        if (current_title != s_last_title) {
            SDL_SetWindowTitle(m_detached_window, current_title.c_str());
            s_last_title = current_title;
        }
    }

    // High-precision frame pacing for detached window (~60 FPS / ~15ms min interval)
    static auto last_detached_render_time = std::chrono::steady_clock::now();
    auto now_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_time - last_detached_render_time).count();
    if (elapsed_ms < 15) {
        return;
    }

    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmdbuf) return;

    last_detached_render_time = now_time;

    SDL_GPUColorTargetInfo color_target = {};
    SDL_AcquireGPUSwapchainTexture(cmdbuf, m_detached_window, &color_target.texture, nullptr, nullptr);
    if (color_target.texture) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_detached_window, &w, &h);
        float const win_w = static_cast<float>(w > 0 ? w : 960);
        float const win_h = static_cast<float>(h > 0 ? h : 540);

        ImTextureID const tex = detached_item ? detached_item->get_texture_id(m_device, cmdbuf) : ImTextureID{0};
        RouenGPUTexture* rtex = tex ? reinterpret_cast<RouenGPUTexture*>(static_cast<uintptr_t>(tex)) : nullptr;
        if (!rtex && is_adlib_active) {
            rtex = rouen::helpers::AdLibEngine::instance().get_background_texture(m_device, cmdbuf);
        }

        if (rtex && rtex->binding.texture) {
            float const aspect = static_cast<float>(media_player_item::kWidth) / static_cast<float>(media_player_item::kHeight);
            float draw_w, draw_h;
            if (win_w / win_h > aspect) {
                draw_h = win_h;
                draw_w = draw_h * aspect;
            } else {
                draw_w = win_w;
                draw_h = draw_w / aspect;
            }
            float const draw_x = (win_w - draw_w) * 0.5f;
            float const draw_y = (win_h - draw_h) * 0.5f;

            // 1. Blit video / background texture onto swapchain
            SDL_GPUBlitInfo blit_info = {};
            blit_info.source.texture = rtex->binding.texture;
            blit_info.source.w = static_cast<Uint32>(rtex->width > 0 ? rtex->width : media_player_item::kWidth);
            blit_info.source.h = static_cast<Uint32>(rtex->height > 0 ? rtex->height : media_player_item::kHeight);
            blit_info.destination.texture = color_target.texture;
            blit_info.destination.x = static_cast<Uint32>(std::max(0.0f, draw_x));
            blit_info.destination.y = static_cast<Uint32>(std::max(0.0f, draw_y));
            blit_info.destination.w = static_cast<Uint32>(std::max(0.0f, draw_w));
            blit_info.destination.h = static_cast<Uint32>(std::max(0.0f, draw_h));
            blit_info.load_op = SDL_GPU_LOADOP_CLEAR;
            blit_info.clear_color = SDL_FColor{ 0.12f, 0.10f, 0.18f, 1.0f };
            blit_info.filter = SDL_GPU_FILTER_LINEAR;

            SDL_BlitGPUTexture(cmdbuf, &blit_info);
        }

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
                m_detached_imgui_ctx->Style = main_ctx->Style;

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
                float const scale_x = (win_pt_w > 0) ? (win_w / static_cast<float>(win_pt_w)) : 1.0f;
                float const scale_y = (win_pt_h > 0) ? (win_h / static_cast<float>(win_pt_h)) : 1.0f;

                SDL_Window* mouse_focus = SDL_GetMouseFocus();
                if (mouse_focus == m_detached_window) {
                    float raw_mx = 0.0f, raw_my = 0.0f;
                    Uint32 const mouse_buttons = SDL_GetMouseState(&raw_mx, &raw_my);
                    io.AddMousePosEvent(raw_mx * scale_x, raw_my * scale_y);
                    io.AddMouseButtonEvent(0, (mouse_buttons & SDL_BUTTON_LMASK) != 0);
                } else {
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    io.AddMouseButtonEvent(0, false);
                }

                ImGui::NewFrame();

                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(win_w, win_h));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

                ImGuiWindowFlags const detached_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

                if (ImGui::Begin("##DetachedWindowOverlay", nullptr, detached_flags)) {
                    if (detached_item && !detached_item->has_video.load()) {
                        media_player::draw_full_window_audio_visualization(*detached_item, win_w, win_h);

                        float const vu_w = std::min(win_w * 0.65f, 460.0f);
                        float const vu_h = 110.0f;
                        float const vu_x = (win_w - vu_w) * 0.5f;
                        float const vu_y = win_h - vu_h - 38.0f;
                        ImGui::SetCursorPos(ImVec2(vu_x, vu_y));
                        media_player::draw_vintage_110_vu_meter(
                            detached_item->get_vu_level_l(), detached_item->get_vu_level_r(),
                            detached_item->get_vu_watermark_l(), detached_item->get_vu_watermark_r(),
                            vu_w, vu_h, /*is_lit=*/true
                        );
                    } else if (!detached_item && !is_adlib_active) {
                        const char* idle_title = ICON_MD_DESKTOP_WINDOWS " Rouen Detached Window";
                        const char* idle_sub = "Ready for media playback...";
                        ImVec2 const title_sz = ImGui::CalcTextSize(idle_title);
                        ImVec2 const sub_sz = ImGui::CalcTextSize(idle_sub);

                        ImGui::SetCursorPos(ImVec2((win_w - title_sz.x) * 0.5f, (win_h - title_sz.y - sub_sz.y - 8.0f) * 0.5f));
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 0.8f), "%s", idle_title);

                        ImGui::SetCursorPos(ImVec2((win_w - sub_sz.x) * 0.5f, (win_h - title_sz.y - sub_sz.y - 8.0f) * 0.5f + title_sz.y + 8.0f));
                        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.6f, 0.6f), "%s", idle_sub);
                    }

                    // Keyboard hotkeys for AdLib in detached window
                    if (is_adlib_active) {
                        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                            rouen::helpers::AdLibEngine::instance().toggle_pause();
                        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_N)) {
                            rouen::helpers::AdLibEngine::instance().next_stage();
                        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_S)) {
                            rouen::helpers::AdLibEngine::instance().stop();
                        }
                    }

                    // Allow 1-9 key overlay toggling in detached window
                    for (int k = ImGuiKey_1; k <= ImGuiKey_9; ++k) {
                        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) {
                            toggle_overlay_by_index(k - ImGuiKey_1);
                        }
                    }

                    bool allow_overlays = true;
                    if (is_adlib_active) {
                        auto adlib_stage = rouen::helpers::AdLibEngine::instance().get_stage();
                        if (adlib_stage == rouen::helpers::AdLibStage::Intro || adlib_stage == rouen::helpers::AdLibStage::Outro) {
                            allow_overlays = false;
                        }
                    }

                    if (allow_overlays) {
                        try {
                            auto main_deck = registrar::get<deck>("deck");
                            if (main_deck) {
                                for (const auto& card_ptr : main_deck->get_cards()) {
                                    if (card_ptr && card_ptr->video_overlay_visible && card_ptr->has_video_overlay()) {
                                        card_ptr->render_video_ui();
                                    }
                                }
                            }
                        } catch (...) {}
                    }

                    render_detached_toast(win_w, win_h);

                    // Double clicking detached window re-attaches to full-window mode
                    if (detached_item && mouse_focus == m_detached_window && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (io.MousePos.y < win_h - 24.0f) {
                            media_player::set_active_fullscreen_item(detached_item);
                        }
                    }

                    // Render progress line for detached window
                    if (detached_item) {
                        media_player::draw_full_window_progress_line(*detached_item, win_w, win_h);
                    }

                    ImGui::End();
                }
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);

                ImGui::Render();

                Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdbuf);

                SDL_GPUColorTargetInfo ui_target = {};
                ui_target.texture = color_target.texture;
                ui_target.load_op = (rtex && rtex->binding.texture) ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
                ui_target.clear_color = SDL_FColor{ 0.12f, 0.10f, 0.18f, 1.0f };
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

    if (is_adlib_active && m_detached_window && rouen::helpers::AdLibEngine::instance().is_recording()) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_detached_window, &w, &h);
        int const cap_w = (w > 0) ? w : 1280;
        int const cap_h = (h > 0) ? h : 720;

        RouenGPUTexture target_tex;
        target_tex.binding.texture = color_target.texture;
        target_tex.binding.sampler = nullptr;
        target_tex.width = cap_w;
        target_tex.height = cap_h;

        RouenGPUTexture* rec_rtex = &target_tex;
        if (!rec_rtex->binding.texture) {
            ImTextureID const detached_tex = detached_item ? detached_item->get_texture_id(m_device, cmdbuf) : ImTextureID{0};
            rec_rtex = detached_tex ? reinterpret_cast<RouenGPUTexture*>(static_cast<uintptr_t>(detached_tex)) : nullptr;
            if (!rec_rtex) {
                rec_rtex = rouen::helpers::AdLibEngine::instance().get_background_texture(m_device, cmdbuf);
            }
        }

        if (rec_rtex && rec_rtex->binding.texture) {
            int const final_w = (rec_rtex->width > 0) ? rec_rtex->width : cap_w;
            int const final_h = (rec_rtex->height > 0) ? rec_rtex->height : cap_h;
            SDL_Surface* surf = rouen::helpers::download_gpu_texture(m_device, rec_rtex, final_w, final_h, cmdbuf);
            if (surf) {
                rouen::helpers::AdLibEngine::instance().on_detached_frame_rendered(
                    static_cast<const uint8_t*>(surf->pixels),
                    surf->w,
                    surf->h,
                    surf->pitch
                );
                SDL_DestroySurface(surf);
            }
        }
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
}
