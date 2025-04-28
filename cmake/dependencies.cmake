# External dependencies configuration

# Find required packages
find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# TinyXML2 handling - improved for macOS (especially for Homebrew installations)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(TINYXML2 QUIET tinyxml2)
endif()

if(NOT TINYXML2_FOUND)
  find_path(TINYXML2_INCLUDE_DIRS tinyxml2.h
    PATH_SUFFIXES include
    PATHS
    /usr/local/include
    /usr/include
    /opt/local/include
    /opt/homebrew/include  # Important for M1/M2 Macs with Homebrew
  )

  find_library(TINYXML2_LIBRARIES
    NAMES tinyxml2
    PATHS
    /usr/local/lib
    /usr/lib
    /opt/local/lib
    /opt/homebrew/lib  # Important for M1/M2 Macs with Homebrew
  )

  if(TINYXML2_INCLUDE_DIRS AND TINYXML2_LIBRARIES)
    set(TINYXML2_FOUND TRUE)
  endif()
endif()

if(NOT TINYXML2_FOUND)
  message(FATAL_ERROR "TinyXML2 not found. Please install it using 'brew install tinyxml2' or the appropriate package manager for your system.")
else()
  message(STATUS "Found TinyXML2: ${TINYXML2_LIBRARIES}")
  message(STATUS "TinyXML2 include directories: ${TINYXML2_INCLUDE_DIRS}")
endif()

# SDL2 handling without requiring SDL2Config.cmake
find_path(SDL2_INCLUDE_DIRS SDL.h
  PATH_SUFFIXES SDL2 include/SDL2 include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include/SDL2
  /usr/include/SDL2
  /opt/local/include/SDL2
  /opt/homebrew/include/SDL2
)

find_library(SDL2_LIBRARIES
  NAMES SDL2
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/lib
  /usr/lib
  /opt/local/lib
  /opt/homebrew/lib
)

find_library(SDL2_IMAGE_LIBRARIES
  NAMES SDL2_image
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/lib
  /usr/lib
  /opt/local/lib
  /opt/homebrew/lib
)

if(NOT SDL2_INCLUDE_DIRS)
  message(FATAL_ERROR "SDL2 headers not found")
endif()

if(NOT SDL2_LIBRARIES)
  message(FATAL_ERROR "SDL2 library not found")
endif()

message(STATUS "Found SDL2: ${SDL2_LIBRARIES}")
message(STATUS "SDL2 include directories: ${SDL2_INCLUDE_DIRS}")

# Fetch dependencies
include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.89.9  # Using a specific tag instead of master for stability
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

# Removed FetchContent_Declare for imguicolortextedit since we're using local files

FetchContent_MakeAvailable(imgui glaze)

# Create ImGui library
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp  # Add SDL2 renderer implementation
)

target_include_directories(imgui PUBLIC 
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${SDL2_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIR}
)

target_link_libraries(imgui PUBLIC 
  ${SDL2_LIBRARIES}
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

# Disable sign comparison warnings only for the imcolortextedit library
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(imcolortextedit PRIVATE -Wno-sign-compare -Wno-conversion -Wno-double-promotion)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(imcolortextedit PRIVATE -Wno-sign-compare)
endif()