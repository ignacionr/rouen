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
#include "../../external/IconsMaterialDesign.h"

namespace rouen::platform {

inline float get_win_titlebar_height() {
    return 36.0f;
}

inline void render_win_titlebar(SDL_Window* window = nullptr) {
    float const height = get_win_titlebar_height();
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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, (height - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));

    if (ImGui::Begin("##CustomTitleBar", nullptr, flags)) {
        if (ImGui::BeginMenuBar()) {
            // App Logo / Name
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "  ROUEN");
            ImGui::Dummy(ImVec2(4.0f, 0.0f));
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
                if (ImGui::MenuItem("Scroll Left", "Alt+Left")) {
                    // Handled in deck
                }
                if (ImGui::MenuItem("Scroll Right", "Alt+Right")) {
                    // Handled in deck
                }
                ImGui::EndMenu();
            }

            // Cards Menu
            if (ImGui::BeginMenu("Cards")) {
                if (ImGui::MenuItem("Close Card", "Ctrl+W")) {
                    auto close_fn = registrar::try_get<std::function<bool()>>("close_focused_card");
                    if (close_fn) {
                        (*close_fn)();
                    }
                }
                ImGui::EndMenu();
            }

            // Help Menu
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Objectives")) {
                    "create_card"_sfn("objectives");
                }
                if (ImGui::MenuItem("About Rouen")) {
                    "create_card"_sfn("about");
                }
                ImGui::EndMenu();
            }

            // Title text centered in remaining title bar space
            std::string window_title = "Rouen";
            auto get_deck_status_func = registrar::get<std::function<std::string()>>("get_deck_status");
            if (get_deck_status_func) {
                // Keep minimal title
            }

            // Caption Control Buttons (Minimize, Maximize/Restore, Close)
            float const btn_w = 46.0f;
            float const caption_area_w = btn_w * 3.0f;
            float const avail_x = ImGui::GetContentRegionAvail().x;
            if (avail_x > caption_area_w) {
                ImGui::Dummy(ImVec2(avail_x - caption_area_w, 0.0f));
                ImGui::SameLine(0.0f, 0.0f);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

            // Transparent background for caption buttons
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));

            // Minimize
            if (ImGui::Button(ICON_MD_REMOVE "##win_min", ImVec2(btn_w, height))) {
                if (window) SDL_MinimizeWindow(window);
            }
            ImGui::SameLine(0.0f, 0.0f);

            // Maximize / Restore
            Uint32 const win_flags = window ? SDL_GetWindowFlags(window) : 0;
            bool const is_maximized = (win_flags & SDL_WINDOW_MAXIMIZED) != 0;
            const char* max_icon = is_maximized ? ICON_MD_FILTER_NONE : ICON_MD_CROP_SQUARE;
            if (ImGui::Button(std::format("{}##win_max", max_icon).c_str(), ImVec2(btn_w, height))) {
                if (window) {
                    if (is_maximized) {
                        SDL_RestoreWindow(window);
                    } else {
                        SDL_MaximizeWindow(window);
                    }
                }
            }
            ImGui::SameLine(0.0f, 0.0f);

            ImGui::PopStyleColor(2); // Pop ButtonHovered and ButtonActive

            // Close button with red hover style
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.08f, 0.10f, 1.0f));

            if (ImGui::Button(ICON_MD_CLOSE "##win_close", ImVec2(btn_w, height))) {
                SDL_Event ev{};
                ev.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&ev);
            }

            ImGui::PopStyleColor(3); // Pop Button, Close ButtonHovered, Close ButtonActive
            ImGui::PopStyleVar(3);   // Pop FrameBorderSize, FrameRounding, FramePadding

            ImGui::EndMenuBar();
        }

        // Window Dragging Logic:
        // If the custom title bar window is hovered and a left-click drag occurs—and no individual menu item or button is actively focused (!ImGui::IsAnyItemActive())—extract mouse delta and update window pos.
        if (window && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemActive()) {
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
