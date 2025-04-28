cmake_minimum_required(VERSION 3.30)

# Set build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

# Debug-specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Add debug symbols and disable optimizations
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
  
  # Enable address sanitizer for debug builds (optional)
  option(ENABLE_SANITIZER "Enable Address Sanitizer in Debug build" OFF)
  if(ENABLE_SANITIZER)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
  endif()
  
  # Add preprocessor definition for debug build
  add_compile_definitions(DEBUG_BUILD)
  
  message(STATUS "Debug build enabled with symbols")
else()
  # Release optimizations
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  add_compile_definitions(NDEBUG)
  
  message(STATUS "Release build enabled with optimizations")
endif()

# Specify the Homebrew-installed LLVM compiler and libraries for better C++23 support
set(CMAKE_C_COMPILER "/opt/homebrew/opt/llvm/bin/clang")
set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++")

# Project name and version
project(rouen VERSION 1.0 LANGUAGES CXX)

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Core warnings that should be enabled immediately
  add_compile_options(
    -Wall                # Enable all common warnings
    -Wextra              # Enable extra warnings
  )

  # Warnings for third-party code can be suppressed
  set(THIRD_PARTY_DIRS
    "${imguicolortextedit_SOURCE_DIR}"
  )

  # Add more strict warnings only for our own code
  function(target_add_strict_warnings target)
    target_compile_options(${target} PRIVATE
      -Werror              # Treat warnings as errors
      -Wpedantic           # Enforce strict ISO C++
      -Wnull-dereference   # Warn about null pointer dereference
      -Wformat=2           # Warn about printf format issues
      -Wimplicit-fallthrough # Warn about fallthrough in switch statements
      -Wunused             # Warn about unused variables/functions

      # Conversion warnings (more aggressive)
      -Wconversion
      -Wsign-conversion
      -Wdouble-promotion

      # Shadow warnings  
      -Wshadow

      # Important structural warnings
      -Wunreachable-code
      -Wself-assign
      -Woverloaded-virtual 

      # More advanced warnings to implement gradually
      # -Wold-style-cast      # Very common in C/C++ interop code
      # -Wcast-align          # Memory alignment issues
      -Wrange-loop-analysis # Range-based for loop issues
      -Wredundant-move     # Unnecessary move operations
      -Wundef              # Undefined macro use in #if
      -Wdeprecated         # Deprecated feature usage
    )
  endfunction()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Similar flags for GCC
  add_compile_options(
    -Wall 
    -Wextra 
    -Wpedantic 
    -Wunused
    -Wnull-dereference 
    -Wformat=2 
    -Wimplicit-fallthrough
    -Werror              # Treat warnings as errors
  )
endif()

find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)  # Add SQLite3

# TinyXML2 handling for macOS (replace the find_package with explicit path handling)
# This works better with Homebrew installations on macOS
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

if(NOT TINYXML2_INCLUDE_DIRS)
  message(FATAL_ERROR "TinyXML2 headers not found")
endif()

if(NOT TINYXML2_LIBRARIES)
  message(FATAL_ERROR "TinyXML2 library not found")
endif()

message(STATUS "Found TinyXML2: ${TINYXML2_LIBRARIES}")
message(STATUS "TinyXML2 include directories: ${TINYXML2_INCLUDE_DIRS}")

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

message(STATUS "Found SDL2: ${SDL2_LIBRARIES} (found version)")
message(STATUS "SDL2 include directories: ${SDL2_INCLUDE_DIRS}")

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

FetchContent_Declare(
  imguicolortextedit
  GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui glaze imguicolortextedit)

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
  ${SDL2_INCLUDE_DIRS}  # Add SDL2 include directories
  ${OPENGL_INCLUDE_DIR} # Add OpenGL include directories
)

target_link_libraries(imgui PUBLIC 
  ${SDL2_LIBRARIES}
  ${OPENGL_LIBRARIES}
)

# Set up ImColorTextEdit library with reference to the fetched ImGui
add_library(imcolortextedit 
  ${imguicolortextedit_SOURCE_DIR}/TextEditor.cpp # Add the TextEditor implementation
)
target_include_directories(imcolortextedit PRIVATE 
  ${imgui_SOURCE_DIR}
  ${imguicolortextedit_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/helpers  # Add our helpers directory for the imgui_compat adapter
)

# Disable sign comparison warnings only for the imcolortextedit library
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(imcolortextedit PRIVATE -Wno-sign-compare -Wno-conversion -Wno-double-promotion)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(imcolortextedit PRIVATE -Wno-sign-compare)
endif()

# Add the executable
add_executable(rouen 
  src/rouen.cpp
  src/main_wnd.cpp
  src/fonts.cpp
  src/helpers/capture_helper.cpp  # Added new source file for proper function implementation
  src/cards/development/github_registrar.cpp  # GitHub card registration
  )

target_include_directories(rouen PRIVATE 
  ${imgui_SOURCE_DIR}  # Use the fetched ImGui headers
  ${imgui_SOURCE_DIR}/backends
  /usr/include/stb
  ${imguicolortextedit_SOURCE_DIR}
  ${CURL_INCLUDE_DIRS}
  ${SQLite3_INCLUDE_DIRS}   # Add SQLite3 include dirs
  ${TINYXML2_INCLUDE_DIRS}  # Add TinyXML2 include dirs
)
target_link_libraries(rouen PRIVATE 
  imgui  # This now references our fetched ImGui library
  imcolortextedit
  glaze::glaze 
  ${CURL_LIBRARIES}
  ${SQLite3_LIBRARIES}
  ${TINYXML2_LIBRARIES}
  ${SDL2_LIBRARIES}  # Use the full path variable instead of literal "SDL2"
  ${SDL2_IMAGE_LIBRARIES}  # Use the full path variable instead of literal "SDL2_image"
)

# Apply stricter warnings only to our code, not third-party libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_add_strict_warnings(rouen)
endif()

# macOS-specific configuration
if(APPLE)
  # Disable problematic warnings as errors for macOS builds
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=sign-conversion -Wno-error=double-promotion -Wno-error=implicit-fallthrough -Wno-error=nontrivial-memcall -Wno-error=implicit-int-float-conversion")
  
  # Add macOS-specific debug flags
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Enable DWARF with dSYM file generation
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-4")
    
    # Disable inlining in debug mode for better debugging experience
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-inline")
    
    # Set LLDB debugging helper macros
    add_compile_definitions(
      _LIBCPP_DEBUG=1           # Enable libcxx debug mode for container checks
      _GLIBCXX_DEBUG=1          # Enable debug mode for standard containers
      DEBUG_ROUEN=1             # Custom macro for conditional debug code
    )
    
    message(STATUS "macOS debug mode configured with enhanced symbols")
  endif()
  
  # Handle ARM64 (Apple Silicon) architecture specifically
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(STATUS "Configuring for Apple Silicon (ARM64)")
    
    # Add Homebrew paths for Apple Silicon
    include_directories(SYSTEM /opt/homebrew/include)
    link_directories(/opt/homebrew/lib)
  endif()
  
  # Special handling for GL on macOS
  target_compile_definitions(rouen PRIVATE GL_SILENCE_DEPRECATION)
  
  # Find required macOS frameworks
  find_library(COCOA_LIBRARY Cocoa REQUIRED)
  find_library(IOKIT_LIBRARY IOKit REQUIRED)
  find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
  
  # Configure GLFW specifically for macOS
  find_package(glfw3 QUIET)
  if(NOT glfw3_FOUND)
    find_library(GLFW_LIBRARY
      NAMES glfw glfw3
      PATHS /opt/homebrew/lib /usr/local/lib
      REQUIRED
    )
    message(STATUS "Found GLFW: ${GLFW_LIBRARY}")
  endif()
  
  # Update target link libraries for macOS
  target_link_libraries(rouen PRIVATE 
    ${COCOA_LIBRARY}
    ${IOKIT_LIBRARY}
    ${COREVIDEO_LIBRARY}
    "-framework OpenGL"
    ${GLFW_LIBRARY}
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
  )
  
  # Add GL library path directly for macOS
  find_library(OPENGL_LIBRARY OpenGL REQUIRED)
  target_link_libraries(rouen PRIVATE ${OPENGL_LIBRARY})
  
  # Remove problematic libraries that are handled differently on macOS
  get_target_property(CURRENT_LINK_LIBRARIES rouen LINK_LIBRARIES)
  if(CURRENT_LINK_LIBRARIES)
    list(REMOVE_ITEM CURRENT_LINK_LIBRARIES "glfw" "GL")
    set_target_properties(rouen PROPERTIES LINK_LIBRARIES "${CURRENT_LINK_LIBRARIES}")
  endif()
endif()
