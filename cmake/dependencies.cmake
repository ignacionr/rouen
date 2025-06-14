# dependencies.cmake - vcpkg-only dependency management for cross-platform builds
# This file handles all dependency discovery using vcpkg exclusively

message(STATUS "Loading dependencies.cmake - vcpkg-only configuration")

# Ensure we're using vcpkg
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    message(FATAL_ERROR "This project requires vcpkg. Please configure with -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg.cmake")
endif()

# Set minimum required versions
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Core vcpkg dependencies - find them all
message(STATUS "Finding vcpkg dependencies...")

# HTTP/SSL libraries
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Database
find_package(unofficial-sqlite3 REQUIRED)

# Graphics and UI
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# JSON processing
find_package(glaze REQUIRED)

# Testing framework
find_package(GTest REQUIRED)

# Platform-specific system dependencies that vcpkg doesn't provide
if(UNIX AND NOT APPLE)
    # Linux-specific
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
elseif(APPLE)
    # macOS-specific
    find_package(Threads REQUIRED)
elseif(WIN32)
    # Windows-specific - most handled by vcpkg
    find_package(Threads REQUIRED)
endif()

# ImGui setup via FetchContent (since vcpkg version may not include all backends we need)
include(FetchContent)

message(STATUS "Setting up ImGui via FetchContent...")
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.9
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui)

# Create ImGui library target
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2-static>
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2>
    OpenGL::GL
)

# ImColorTextEdit setup
message(STATUS "Setting up ImColorTextEdit...")
add_library(imcolortextedit STATIC
    ${CMAKE_SOURCE_DIR}/external/imguicolortextedit/TextEditor.cpp
)

target_include_directories(imcolortextedit PUBLIC
    ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
    ${imgui_SOURCE_DIR}
)

target_link_libraries(imcolortextedit PUBLIC imgui)

message(STATUS "All dependencies configured successfully")