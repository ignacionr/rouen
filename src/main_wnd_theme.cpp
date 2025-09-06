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

void main_wnd::setup_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Set rounded corners for windows and other elements
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    
    // Set window padding
    style.WindowPadding = ImVec2(10, 10);
    
    // Adjust spacing
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(4, 4);
    
    // Start with dark style
    ImGui::StyleColorsDark();
    
    // Now customize specific colors
    ImVec4* colors = style.Colors;
    
    // Main colors
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);  // Darker window background
    colors[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.29f, 0.50f);    // Subtle border
    
    // Text
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);      // Slightly off-white for better readability
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    
    // Headers (title bars)
    colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.17f, 0.75f);
    
    // Menu bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    
    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.35f, 0.45f, 1.00f);    // Changed: More blue-oriented accent color
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.45f, 0.60f, 1.00f); // Changed: Lighter blue when hovered
    colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.55f, 0.70f, 1.00f);  // Changed: Brighter blue when active
    
    // Checkboxes, radio buttons
    colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.53f, 0.71f, 1.00f);  // Slightly adjusted to match new accent color
    
    // Sliders, drag controls
    colors[ImGuiCol_SliderGrab] = ImVec4(0.37f, 0.53f, 0.71f, 1.00f);  // Slightly adjusted to match new accent color
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.57f, 0.67f, 0.86f, 1.00f);
    
    // Frame backgrounds (checkbox, radio, slider, input fields)
    colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.31f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.32f, 0.38f, 1.00f);
    
    // Text editor and input areas - ensure high contrast for text
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.53f, 0.71f, 0.50f);  // Blue tint for selected text
}

void main_wnd::configure_highdpi_settings() {
    auto& io = ImGui::GetIO();
    
    // Get the window size and drawable size to calculate DPI scale
    int window_w, window_h;
    int drawable_w, drawable_h;
    
    SDL_GetWindowSize(m_window, &window_w, &window_h);
    SDL_GetRendererOutputSize(m_renderer, &drawable_w, &drawable_h);
    
    float scale_x = window_w > 0 ? static_cast<float>(drawable_w) / static_cast<float>(window_w) : 1.0f;
    float scale_y = window_h > 0 ? static_cast<float>(drawable_h) / static_cast<float>(window_h) : 1.0f;
    
    std::cout << "Configuring high-DPI settings:" << std::endl;
    std::cout << "  Window size: " << window_w << "x" << window_h << std::endl;
    std::cout << "  Drawable size: " << drawable_w << "x" << drawable_h << std::endl;
    std::cout << "  Scale factors: " << scale_x << " x " << scale_y << std::endl;
    
    // Set the display scale and framebuffer scale
    io.DisplaySize = ImVec2(static_cast<float>(window_w), static_cast<float>(window_h));
    io.DisplayFramebufferScale = ImVec2(scale_x, scale_y);
    
    std::cout << "  ImGui DisplaySize set to: " << io.DisplaySize.x << "x" << io.DisplaySize.y << std::endl;
    std::cout << "  ImGui DisplayFramebufferScale set to: " << io.DisplayFramebufferScale.x << "x" << io.DisplayFramebufferScale.y << std::endl;
}

void main_wnd::update_imgui_display_settings() {
    auto& io = ImGui::GetIO();
    
    // Get current window and drawable sizes
    int window_w, window_h;
    int drawable_w, drawable_h;
    
    SDL_GetWindowSize(m_window, &window_w, &window_h);
    SDL_GetRendererOutputSize(m_renderer, &drawable_w, &drawable_h);
    
    // Update display size to logical window size
    io.DisplaySize.x = static_cast<float>(window_w);
    io.DisplaySize.y = static_cast<float>(window_h);
    
    // Update framebuffer scale
    float scale_x = window_w > 0 ? static_cast<float>(drawable_w) / static_cast<float>(window_w) : 1.0f;
    float scale_y = window_h > 0 ? static_cast<float>(drawable_h) / static_cast<float>(window_h) : 1.0f;
    
    io.DisplayFramebufferScale.x = scale_x;
    io.DisplayFramebufferScale.y = scale_y;
}
