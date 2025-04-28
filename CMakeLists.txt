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

# Include modular cmake configuration files
include(cmake/warnings.cmake)
include(cmake/dependencies.cmake)

# Add the executable
add_executable(${PROJECT_NAME} 
  src/rouen.cpp
  src/main_wnd.cpp
  src/fonts.cpp
  src/helpers/capture_helper.cpp
  src/cards/development/github_registrar.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE 
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  /usr/include/stb
  ${imguicolortextedit_SOURCE_DIR}
  ${CURL_INCLUDE_DIRS}
  ${SQLite3_INCLUDE_DIRS}
  ${TINYXML2_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
  imgui
  imcolortextedit
  glaze::glaze 
  ${CURL_LIBRARIES}
  ${SQLite3_LIBRARIES}
  ${TINYXML2_LIBRARIES}
  ${SDL2_LIBRARIES}
  ${SDL2_IMAGE_LIBRARIES}
)

# Apply stricter warnings only to our code, not third-party libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_add_strict_warnings(${PROJECT_NAME})
endif()

# Platform-specific configuration
if(APPLE)
  include(cmake/macos.cmake)
endif()

# If we add Windows/Linux support, we can include them conditionally:
# if(WIN32)
#   include(cmake/windows.cmake)
# elseif(UNIX AND NOT APPLE)
#   include(cmake/linux.cmake)
# endif()
