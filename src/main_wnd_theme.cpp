// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// Include ImGui wrapper first which handles all ImGui related headers

// 3. All other includes
#include "main_wnd.hpp"
#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <iostream>

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
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.10f, 0.09f, 1.00f);  // Dark warm amber-tinted background
    colors[ImGuiCol_Border] = ImVec4(0.35f, 0.22f, 0.10f, 0.50f);    // Amber border
    
    // Text
    colors[ImGuiCol_Text] = ImVec4(0.96f, 0.93f, 0.88f, 1.00f);      // Warm off-white
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.50f, 0.45f, 1.00f);
    
    // Headers (title bars)
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.07f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.14f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.12f, 0.09f, 0.07f, 0.75f);
    
    // Menu bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.09f, 0.08f, 1.00f);
    
    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.38f, 0.22f, 0.08f, 1.00f);    // Amber accent color
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.52f, 0.30f, 0.09f, 1.00f); // Bright amber when hovered
    colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.44f, 0.12f, 1.00f);  // Glowing amber when active
    
    // Checkboxes, radio buttons
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.58f, 0.10f, 1.00f);  // Amber checkmark
    
    // Sliders, drag controls
    colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.58f, 0.10f, 1.00f);  // Amber slider grab
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.70f, 0.20f, 1.00f);
    
    // Frame backgrounds (checkbox, radio, slider, input fields)
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.14f, 0.11f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.19f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.24f, 0.17f, 1.00f);
    
    // Text editor and input areas
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.80f, 0.48f, 0.10f, 0.40f);  // Amber tint for selected text
}

void main_wnd::configure_highdpi_settings() {
    auto& io = ImGui::GetIO();
    
    // Get the window size and drawable size to calculate DPI scale
    int window_w = 0;
    int window_h = 0;
    int drawable_w = 0;
    int drawable_h = 0;
    
    SDL_GetWindowSize(m_window, &window_w, &window_h);
    SDL_GetWindowSizeInPixels(m_window, &drawable_w, &drawable_h);
    
    float const scale_x = window_w > 0 ? static_cast<float>(drawable_w) / static_cast<float>(window_w) : 1.0f;
    float const scale_y = window_h > 0 ? static_cast<float>(drawable_h) / static_cast<float>(window_h) : 1.0f;
    
    std::cout << "Configuring high-DPI settings:" << '\n';
    std::cout << "  Window size: " << window_w << "x" << window_h << '\n';
    std::cout << "  Drawable size: " << drawable_w << "x" << drawable_h << '\n';
    std::cout << "  Scale factors: " << scale_x << " x " << scale_y << '\n';
    
    // Set the display scale and framebuffer scale
    io.DisplaySize = ImVec2(static_cast<float>(window_w), static_cast<float>(window_h));
    io.DisplayFramebufferScale = ImVec2(scale_x, scale_y);
    
    std::cout << "  ImGui DisplaySize set to: " << io.DisplaySize.x << "x" << io.DisplaySize.y << '\n';
    std::cout << "  ImGui DisplayFramebufferScale set to: " << io.DisplayFramebufferScale.x << "x" << io.DisplayFramebufferScale.y << '\n';
}

void main_wnd::update_imgui_display_settings() {
    auto& io = ImGui::GetIO();
    
    // Get current window and drawable sizes
    int window_w = 0;
    int window_h = 0;
    int drawable_w = 0;
    int drawable_h = 0;
    
    SDL_GetWindowSize(m_window, &window_w, &window_h);
    SDL_GetWindowSizeInPixels(m_window, &drawable_w, &drawable_h);
    
    // Update display size to logical window size
    io.DisplaySize.x = static_cast<float>(window_w);
    io.DisplaySize.y = static_cast<float>(window_h);
    
    // Update framebuffer scale
    float const scale_x = window_w > 0 ? static_cast<float>(drawable_w) / static_cast<float>(window_w) : 1.0f;
    float const scale_y = window_h > 0 ? static_cast<float>(drawable_h) / static_cast<float>(window_h) : 1.0f;
    
    io.DisplayFramebufferScale.x = scale_x;
    io.DisplayFramebufferScale.y = scale_y;
}
