# ImGui Backend Files

This directory contains ImGui backend implementation files for SDL2 and OpenGL3.

These files are vendored from the official ImGui repository (v1.89.9) to provide 
SDL2 backend support when using system-provided ImGui packages that don't include 
platform-specific backends.

## Files

- `imgui_impl_sdl2.h/cpp` - SDL2 platform backend
- `imgui_impl_opengl3.h/cpp` - OpenGL3 renderer backend  
- `imgui_impl_sdlrenderer2.h/cpp` - SDL2 renderer backend

## Usage

When CMake detects a system ImGui package (e.g., via Nix), these backends are 
compiled separately and linked with the system ImGui library to provide full 
SDL2 support.

For FetchContent builds, the official ImGui backends are used instead.

## Source

These files are derived from: https://github.com/ocornut/imgui/tree/v1.89.9/backends