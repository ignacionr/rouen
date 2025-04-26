#pragma once

#include <memory>
#include <array>
#include <functional>
#include <format>
#include <imgui/imgui.h>

struct card {
    using ptr = std::shared_ptr<card>;
    
    // Use a fixed-size array instead of a vector to avoid dynamic memory management issues
    static constexpr size_t MAX_COLORS = 16;
    using color_array = std::array<ImVec4, MAX_COLORS>;
    
    // Constructor - initialize all colors to default values
    card() {
        // Initialize default colors
        colors[0] = ImVec4{0.0f, 0.0f, 0.0f, 1.0f}; // first_color (index 0)
        colors[1] = ImVec4{0.0f, 0.0f, 0.0f, 0.5f}; // second_color (index 1)
        // The rest will be initialized to {0,0,0,0} by std::array default
    }
    
    // Virtual destructor - no need to clear colors as it's now a fixed-size array
    virtual ~card() {}
    
    virtual bool render() = 0;

    virtual std::string get_uri() const = 0;

    bool run_focused_handlers() {
        if (is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows), is_focused) {
            // check for ctrl+w
            if (ImGui::IsKeyPressed(ImGuiKey_W) && ImGui::GetIO().KeyCtrl) {
                return false;
            }
        }
        return true;
    }

    bool render_window(std::function<void()> render_func) {
        bool is_open = true;
        if (window_title.empty()) {
            name("Unnamed Card");
        }
        if (ImGui::Begin(window_title.c_str(), &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
            is_open &= run_focused_handlers();
            render_func();
        }
        ImGui::End();
        return is_open;
    }

    void name(std::string_view name) {
        window_title = std::format("{}##{}", name, static_cast<void*>(this));
    }

    // Get a color by index, bounds-checked against the fixed array size
    ImVec4& get_color(size_t index, const ImVec4& default_color = ImVec4{0.0f, 0.0f, 0.0f, 1.0f}) {
        if (index < MAX_COLORS) {
            if (index >= colors_used) {
                colors[index] = default_color;
                colors_used = std::max(colors_used, index + 1);
            }
            return colors[index];
        }
        // If out of bounds, return the last color (safeguard)
        static ImVec4 fallback_color = default_color;
        return fallback_color;
    }

    color_array colors;                 // Fixed-size array of colors
    size_t colors_used = 2;             // Number of colors actually in use
    float width {300.0f};
    bool is_focused{false};
    bool grab_focus{false};
    std::string window_title;
    int requested_fps{1};
};
