cmake_minimum_required(VERSION 3.30)

# Set build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

# Debug-specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Add debug symbols and disable optimizations
  if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od")
  else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
  endif()
  
  # Enable address sanitizer for debug builds (optional)
  option(ENABLE_SANITIZER "Enable Address Sanitizer in Debug build" OFF)
  if(ENABLE_SANITIZER AND NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
  endif()
  
  # Add preprocessor definition for debug build
  add_compile_definitions(DEBUG_BUILD)
  
  message(STATUS "Debug build enabled with symbols")
else()
  # Release optimizations - handle differently for MSVC and GCC/Clang
  if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
  else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  endif()
  add_compile_definitions(NDEBUG)
  
  message(STATUS "Release build enabled with optimizations")
endif()

# For MSVC, ensure no GCC/Clang flags leak in
if(MSVC)
  # Remove any GCC/Clang-specific flags that might be set by dependencies
  string(REPLACE "-O3" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "-g" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-g" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "-g" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# Project name and version
project(rouen VERSION 1.0 LANGUAGES CXX)

# Platform-specific compiler settings
if(APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
elseif(UNIX AND NOT APPLE)
  # Use clang on Linux if available for better C++23 support
  find_program(CLANG_EXECUTABLE clang)
  find_program(CLANGPP_EXECUTABLE clang++)
  
  if(CLANG_EXECUTABLE AND CLANGPP_EXECUTABLE)
    set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}")
    set(CMAKE_CXX_COMPILER "${CLANGPP_EXECUTABLE}")
    message(STATUS "Using Clang/Clang++ for Linux build")
  else()
    message(STATUS "Clang not found, using default system compiler")
  endif()
endif()

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable export of compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find vcpkg dependencies
find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)

# Fetch ImGui using FetchContent
include(FetchContent)
message(STATUS "About to fetch ImGui...")

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.89.9
  GIT_SHALLOW TRUE
)

message(STATUS "ImGui declared, making available...")
FetchContent_MakeAvailable(imgui)
message(STATUS "ImGui available! Source dir: ${imgui_SOURCE_DIR}")

# Create ImGui library
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

# Link ImGui with vcpkg-provided SDL2 and OpenGL
target_link_libraries(imgui PUBLIC 
  $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
  ${OPENGL_LIBRARIES}
)

# Set up ImColorTextEdit library using local files
add_library(imcolortextedit 
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit/TextEditor.cpp
)
target_include_directories(imcolortextedit PUBLIC 
  ${imgui_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
  ${CMAKE_SOURCE_DIR}/src/helpers
)

# Link imcolortextedit to imgui
target_link_libraries(imcolortextedit PUBLIC imgui)

# Include modular cmake configuration files
include(cmake/warnings.cmake)

# Add the executable
add_executable(${PROJECT_NAME} 
  src/rouen.cpp
  src/main_wnd.cpp
  src/fonts.cpp
  src/helpers/capture_helper.cpp
  src/helpers/config_service.cpp
  src/cards/development/github_registrar.cpp
  src/cards/productivity/jira_registrar.cpp
  src/cards/information/bybit_registrar.cpp
  src/models/jira_model.cpp
  src/cards/media/chess_com_integration.cpp
  # RSS model refactor additions
  src/models/rss/feed_item.cpp
  src/models/rss/rss_date_parser.cpp
  src/models/rss/feed_xml_parser.cpp
  src/models/rss/rss_item_repo.cpp
  # Mail model refactor additions
  src/models/mail/metadata_serialization.cpp
  # Git model refactor additions
  src/models/git_process_helper.cpp
  src/models/git_scanner.cpp
  # Terminal refactor additions
  src/cards/system/terminal.cpp
  src/cards/system/terminal_output.cpp
  src/cards/system/terminal_bash.cpp
  src/cards/system/terminal_commands.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE 
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
)

# Platform-specific include directories
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CURL_INCLUDE_DIRS}
  ${SQLite3_INCLUDE_DIRS}
  ${TINYXML2_INCLUDE_DIRS}
)

# Link with vcpkg-provided dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE 
  imgui
  imcolortextedit
  glaze::glaze 
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
  $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
  $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
  $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
  tinyxml2::tinyxml2
  unofficial::sqlite3::sqlite3
)

# Apply stricter warnings only to our code, not third-party libraries
# We use -Werror for our code but not for third-party libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
  target_add_strict_warnings(${PROJECT_NAME})
  message(STATUS "Applied strict warnings to local code for ${CMAKE_CXX_COMPILER_ID}")
endif()

# Platform-specific configuration
if(APPLE)
  include(cmake/macos.cmake)
elseif(UNIX AND NOT APPLE)
  include(cmake/linux.cmake)
elseif(WIN32)
  include(cmake/windows.cmake)
endif()

# Enable CTest only if tests are requested
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Tests disabled. Use -DBUILD_TESTS=ON to enable tests.")
endif()
