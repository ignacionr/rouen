#pragma once

// 1. Standard includes in alphabetic order
#include <array>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/sdl_compat.hpp"

// 3. All other includes
#include "../../registrar.hpp"
#include "../../helpers/ui_context.hpp"
#include "../../helpers/theme_manager.hpp"
// Forward declarations to avoid circular dependencies
namespace rouen::hosts {
    class mcp_host;
}
namespace rouen::helpers {
    using mcp_service = ::rouen::hosts::mcp_host;
}

struct card {
    using ptr = std::shared_ptr<card>;
    
    // Use a fixed-size array instead of a vector to avoid dynamic memory management issues
    static constexpr size_t MAX_COLORS = 16;
    static constexpr float min_card_height = 200.0f;
    using color_array = std::array<ImVec4, MAX_COLORS>;
    
    // Constructor - initialize all colors to default values
    card() {
        // Initialize default colors
        colors[0] = ImVec4{0.0f, 0.0f, 0.0f, 1.0f}; // first_color (index 0)
        colors[1] = ImVec4{0.0f, 0.0f, 0.0f, 0.5f}; // second_color (index 1)
        // The rest will be initialized to {0,0,0,0} by std::array default
    }
    
    // Virtual destructor - ensure clean destruction
    // Note: MCP cleanup will be handled by deck before destruction
    // to avoid calling virtual methods during destruction
    virtual ~card() = default;
    
    // MCP Function definition structure (forward declare to avoid includes)
    struct mcp_function {
        std::string name;
        std::string description;
        std::string schema; // JSON schema for parameters
        std::function<std::string(const std::string& params)> handler;
        
        mcp_function(std::string func_name, std::string func_description, std::string func_schema,
                    std::function<std::string(const std::string&)> func_handler)
            : name(std::move(func_name)), description(std::move(func_description))
            , schema(std::move(func_schema)), handler(std::move(func_handler)) {}
    };
    
    // Virtual method for cards to expose MCP functions
    virtual std::vector<mcp_function> get_mcp_functions() const {
        return {}; // Default: no functions
    }
    
    // Register this card's MCP functions (called automatically by deck)
    void register_mcp_functions();
    
    // Unregister this card's MCP functions (called automatically by deck)  
    void unregister_mcp_functions();

    virtual bool render() { return false; }
    virtual bool render(rouen::ui::ui_context& /*ui*/) { return render(); }
    virtual void on_close() {}

    virtual std::string get_uri() const = 0;

    virtual bool matches_uri(std::string_view uri) const {
        return get_uri() == uri;
    }

    virtual void handle_uri(std::string_view /*uri*/) {}

    bool video_overlay_visible = true;

    /// Optional virtual method for cards to paint themselves onto the video feed surface
    virtual void paint_video_surface(SDL_Surface* /*surface*/, int /*surface_w*/, int /*surface_h*/) {}

    /// Returns true if this card provides a video overlay UI
    virtual bool has_video_overlay() const { return true; }

    /// Optional virtual method for cards to render rich ImGui UI onto the video feed
    virtual void render_video_ui() {
        if (!has_video_overlay()) return;
        render();
    }

    bool run_focused_handlers() {
        is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        if (is_focused) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                // Signal that a card close was handled this frame
                auto signal_service = registrar::get<std::function<void()>>("signal_card_close_handled");
                if (signal_service) {
                    (*signal_service)();
                }
                return false;
            }
        }
        return true;
    }

    bool render_window(const std::function<void()>& render_func) {
        bool is_open = true;
        if (window_title.empty()) {
            name("Unnamed Card");
        }
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
        
        try {
            auto get_card_count = registrar::get<std::function<size_t()>>("get_card_count");
            if (get_card_count && (*get_card_count)() == 1) {
                flags |= ImGuiWindowFlags_NoTitleBar;
                flags |= ImGuiWindowFlags_NoScrollWithMouse;
            }
        } catch (...) {
            // Intentionally ignored: service may not be registered in all contexts
            static_cast<void>(0);
        }

        // Apply active theme to the card's color array
        rouen::theme::theme_manager::get().apply_theme_to_card(this);

        if (ImGui::Begin(window_title.c_str(), &is_open, flags)) {
            is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            if (!is_visible_in_viewport && !is_focused && !grab_focus && requested_fps <= 1) {
                ImGui::End();
                return is_open;
            }
            is_open &= run_focused_handlers();
            render_func();
        }
        ImGui::End();
        return is_open;
    }

    void name(std::string_view name) {
        window_title = std::format("{}###{}", name, static_cast<void*>(this));
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
    bool is_visible_in_viewport{true};  // Viewport visibility state
    std::string window_title;
    int requested_fps{1};
    bool mcp_functions_registered{false}; // Track MCP registration state
};
