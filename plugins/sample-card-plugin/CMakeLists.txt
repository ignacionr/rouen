# Standalone build for the sample "hello" card plugin. This is deliberately
# NOT added as a subdirectory of the main Rouen build: a real plugin lives
# outside the Rouen source tree, built and versioned on its own. See
# docs/PLUGINS.md for the full walkthrough this project accompanies.
#
# Build (from this directory):
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release -- /m:2
#
# Then copy build/bin/Release/hello_card_plugin.dll into either:
#   - <rouen.exe directory>/plugins/, or
#   - %APPDATA%/Rouen/plugins/
# and start Rouen. Open the card with the "Hello Plugin" entry in the
# application menu, or the "hello:<name>" URI via the REST API.

cmake_minimum_required(VERSION 3.30)
project(rouen_sample_card_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(WARNING
    "This sample has only been built and verified on Windows. The plugin "
    "ABI itself (plugin-sdk/rouen_plugin_api.hpp) is not Windows-specific, "
    "but Rouen's plugin_host loader has only shipped a Windows sample so "
    "far - see docs/PLUGINS.md.")
endif()

# Pinned to the exact ImGui tag the main Rouen build uses (see the
# FetchContent_Declare(imgui ...) block in the repository root
# CMakeLists.txt). A plugin MUST match the host's ImGui version exactly:
# ImGuiContext's layout is only guaranteed identical when both sides are
# built from the same source. If you bump this, bump the host's tag too.
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.7
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Only the core library - a plugin draws with ImGui:: calls but never
# owns a window/renderer backend, so the SDL3/GPU backend sources the
# host compiles are neither needed nor linkable here.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

add_library(hello_card_plugin SHARED
  src/plugin_entry.cpp
)

target_include_directories(hello_card_plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../plugin-sdk
)

target_link_libraries(hello_card_plugin PRIVATE imgui)

set_target_properties(hello_card_plugin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)

if(MSVC)
  # Dynamic CRT (/MD, /MDd), matching Rouen's own windows.cmake setting -
  # required so heap-allocated objects can safely cross the host/plugin
  # boundary. See docs/PLUGINS.md.
  set_property(TARGET hello_card_plugin imgui PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  )
  target_compile_options(hello_card_plugin PRIVATE /W4 /permissive- /utf-8)
endif()
