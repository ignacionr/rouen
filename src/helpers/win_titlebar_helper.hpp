#pragma once

#if defined(_WIN32)

#include <SDL3/SDL.h>
#include <imgui.h>
#include <format>
#include <functional>
#include <string>
#include <vector>

#include "../cards/interface/menu.hpp"
#include "../helpers/media_player.hpp"
#include "../helpers/platform_utils.hpp"
#include "../registrar.hpp"

namespace rouen::platform {

inline float get_win_titlebar_height() {
    return 40.0f;
}

inline void render_win_titlebar(SDL_Window* window) {
    if (!window) return;

    constexpr float height = 40.0f;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));

    ImGuiWindowFlags const flags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_MenuBar |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));

    if (ImGui::Begin("##CustomTitleBar", nullptr, flags)) {
        if (ImGui::BeginMenuBar()) {
            // App Logo / Name
            ImGui::SetCursorPosY((height - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "  ROUEN");
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(6.0f, 0.0f));
            ImGui::SameLine();

            // File Menu
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Card", "Ctrl+N")) {
                    "create_card"_sfn("menu");
                }
                if (ImGui::MenuItem("Menu Launcher", "Ctrl+Shift+P")) {
                    "create_card"_sfn("menu");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    SDL_Event ev{};
                    ev.type = SDL_EVENT_QUIT;
                    SDL_PushEvent(&ev);
                }
                ImGui::EndMenu();
            }

            // Edit Menu
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Expand to Full Width", "Ctrl+Shift+F")) {
                    auto expand_svc = registrar::get<std::function<void()>>("expand_to_full_width");
                    if (expand_svc) {
                        (*expand_svc)();
                    }
                }
                ImGui::EndMenu();
            }

            // View Menu
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Display Settings")) {
                    "create_card"_sfn("display");
                }
                if (ImGui::MenuItem("Theme Settings")) {
                    "create_card"_sfn("theme");
                }
                if (ImGui::MenuItem("Video Feed & Cast Control")) {
                    "create_card"_sfn("cast-control");
                }
                if (ImGui::MenuItem("Detach Currently Playing Media")) {
                    auto item = media_player::get_currently_playing_item();
                    if (item) {
                        media_player::set_detached_item(item);
                    } else {
                        media_player::set_detached_mode_active(true);
                    }
                }
                ImGui::EndMenu();
            }

            // Tools Menu
            if (ImGui::BeginMenu("Tools")) {
                try {
                    const auto categories = rouen::cards::menu::get_categories();
                    for (const auto& cat : categories) {
                        if (ImGui::BeginMenu(cat.name.c_str())) {
                            for (const auto& item : cat.items) {
                                if (ImGui::MenuItem(item.first.c_str())) {
                                    item.second();
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                } catch (...) {}
                ImGui::EndMenu();
            }

            // Help Menu
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("API Documentation (Swagger)")) {
                    rouen::platform::open_url("http://localhost:8081/swagger");
                }
                if (ImGui::MenuItem("About Rouen")) {
                    "create_card"_sfn("about");
                }
                ImGui::EndMenu();
            }

            // Caption Buttons (Right Aligned)
            constexpr float btn_w = 46.0f;
            float const avail_w = ImGui::GetWindowWidth();
            ImGui::SetCursorPosX(avail_w - (3.0f * btn_w));
            ImGui::SetCursorPosY(0.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

            // Transparent background for caption buttons
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));

            // Minimize
            if (ImGui::Button("-##win_min", ImVec2(btn_w, height))) {
                SDL_MinimizeWindow(window);
            }
            ImGui::SameLine(0.0f, 0.0f);

            // Maximize / Restore
            Uint32 const win_flags = SDL_GetWindowFlags(window);
            bool const is_maximized = (win_flags & SDL_WINDOW_MAXIMIZED) != 0;
            const char* max_icon = is_maximized ? "\xE2\x9D\xB2" : "\xE2\x96\xA1";
            if (ImGui::Button(std::format("{}##win_max", max_icon).c_str(), ImVec2(btn_w, height))) {
                if (is_maximized) {
                    SDL_RestoreWindow(window);
                } else {
                    SDL_MaximizeWindow(window);
                }
            }
            ImGui::SameLine(0.0f, 0.0f);

            ImGui::PopStyleColor(2); // Pop ButtonHovered and ButtonActive

            // Close button with red hover style
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.08f, 0.10f, 1.0f));

            if (ImGui::Button("\xE2\x9C\x95##win_close", ImVec2(btn_w, height))) {
                SDL_Event ev{};
                ev.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&ev);
            }

            ImGui::PopStyleColor(3); // Pop Button, Close ButtonHovered, Close ButtonActive
            ImGui::PopStyleVar(2);   // Pop FrameBorderSize, FrameRounding

            ImGui::EndMenuBar();
        }

        // Window Dragging Logic:
        // If the custom title bar window is hovered and a left-click drag occurs—and no individual menu item or button is actively focused (!ImGui::IsAnyItemActive())—extract mouse delta and update window pos.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 const delta = ImGui::GetIO().MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    int wx = 0, wy = 0;
                    if (SDL_GetWindowPosition(window, &wx, &wy)) {
                        SDL_SetWindowPosition(window, wx + static_cast<int>(delta.x), wy + static_cast<int>(delta.y));
                    }
                }
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                Uint32 const win_flags = SDL_GetWindowFlags(window);
                if (win_flags & SDL_WINDOW_MAXIMIZED) {
                    SDL_RestoreWindow(window);
                } else {
                    SDL_MaximizeWindow(window);
                }
            }
        }

        ImGui::End();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

} // namespace rouen::platform

#else

namespace rouen::platform {
inline float get_win_titlebar_height() { return 0.0f; }
inline void render_win_titlebar(SDL_Window*) {}
}

#endif // _WIN32
