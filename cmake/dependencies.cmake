# External dependencies configuration

# Find required packages
find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# Platform-specific dependencies
if(UNIX AND NOT APPLE)
  # Additional dependencies for Linux
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
  
  # Try pkg-config for SDL2 first (more reliable on Linux)
  pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
  pkg_check_modules(SDL2_IMAGE IMPORTED_TARGET SDL2_image)
endif()

# TinyXML2 handling - improved for all platforms
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
    /usr/lib/x86_64-linux-gnu  # Common path on Debian/Ubuntu
    /opt/local/lib
    /opt/homebrew/lib  # Important for M1/M2 Macs with Homebrew
  )

  if(TINYXML2_INCLUDE_DIRS AND TINYXML2_LIBRARIES)
    set(TINYXML2_FOUND TRUE)
  endif()
endif()

if(NOT TINYXML2_FOUND)
  message(FATAL_ERROR "TinyXML2 not found. Please install it using your system package manager (apt-get install libtinyxml2-dev on Debian/Ubuntu, brew install tinyxml2 on macOS)")
else()
  message(STATUS "Found TinyXML2: ${TINYXML2_LIBRARIES}")
  message(STATUS "TinyXML2 include directories: ${TINYXML2_INCLUDE_DIRS}")
endif()

# SDL2 handling
if(NOT SDL2_FOUND)
  find_path(SDL2_INCLUDE_DIRS SDL.h
    PATH_SUFFIXES SDL2 include/SDL2 include
    PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /usr/local/include/SDL2
    /usr/include/SDL2
    /usr/local/include
    /usr/include
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
    /usr/lib/x86_64-linux-gnu
    /opt/local/lib
    /opt/homebrew/lib
  )

  if(SDL2_INCLUDE_DIRS AND SDL2_LIBRARIES)
    set(SDL2_FOUND TRUE)
  endif()
endif()

if(NOT SDL2_FOUND)
  message(FATAL_ERROR "SDL2 not found. Please install it using your system package manager (apt-get install libsdl2-dev on Debian/Ubuntu, brew install sdl2 on macOS)")
endif()

message(STATUS "Found SDL2: ${SDL2_LIBRARIES}")
message(STATUS "SDL2 include directories: ${SDL2_INCLUDE_DIRS}")

# SDL2_image handling
if(NOT SDL2_IMAGE_FOUND)
  find_library(SDL2_IMAGE_LIBRARIES
    NAMES SDL2_image
    PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /usr/local/lib
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /opt/local/lib
    /opt/homebrew/lib
  )

  if(SDL2_IMAGE_LIBRARIES)
    set(SDL2_IMAGE_FOUND TRUE)
  endif()
endif()

if(NOT SDL2_IMAGE_LIBRARIES)
  message(FATAL_ERROR "SDL2_image not found. Please install it using your system package manager (apt-get install libsdl2-image-dev on Debian/Ubuntu, brew install sdl2_image on macOS)")
endif()

message(STATUS "Found SDL2_image: ${SDL2_IMAGE_LIBRARIES}")

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

# Disable specific warnings for ImGui to prevent -Wnontrivial-memcall warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(imgui PRIVATE -Wno-nontrivial-memcall)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC equivalent if needed
  target_compile_options(imgui PRIVATE -Wno-class-memaccess)
endif()

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