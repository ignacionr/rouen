#pragma once

#include <memory>

struct card {
    using ptr = std::shared_ptr<card>;
    virtual ~card() = default;
    virtual bool render() = 0;

    bool run_focused_handlers() {
        if (is_focused = ImGui::IsWindowFocused(), is_focused) {
            // check for ctrl+w
            if (ImGui::IsKeyPressed(ImGuiKey_W) && ImGui::GetIO().KeyCtrl) {
                return false;
            }
        }
        return true;
    }

    bool render_window(std::function<void()> render_func) {
        bool is_open = true;
        if (ImGui::Begin(window_title.c_str(), &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
            is_open &= run_focused_handlers();
            render_func();
        }
        ImGui::End();
        return is_open;
    }

    void name(std::string_view name) {
        window_title = std::format("{}##{}", name, static_cast<void*>(this));
    }

    ImVec4 first_color {0.0f, 0.0, 0.0f, 1.0f};
    ImVec4 second_color {0.0f, 0.0f, 0.0f, 0.5f};
    ImVec2 size {300.0f, 250.0f};
    bool is_focused{false};
    std::string window_title;
};
