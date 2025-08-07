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
