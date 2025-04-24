cmake_minimum_required(VERSION 3.30)

# Project name and version
project(rouen VERSION 1.0 LANGUAGES CXX)

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall                # Enable all common warnings
    -Wextra              # Enable extra warnings
    -Wpedantic           # Enforce strict ISO C++
    -Wshadow             # Warn when a variable declaration shadows another
    -Wconversion         # Warn on implicit type conversions
    # -Wsign-conversion    # Warn on sign conversions
    # -Wnull-dereference   # Warn about null pointer dereference
    # -Wdouble-promotion   # Warn about implicit doubles from float
    # -Wformat=2           # Warn about printf format issues
    # -Wimplicit-fallthrough # Warn about fallthrough in switch statements
    # -Wunused             # Warn about unused variables/functions
    # -Werror              # Treat warnings as errors
  )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Similar flags for GCC
  add_compile_options(
    -Wall 
    # -Wextra 
    # -Wpedantic 
    # -Wshadow 
    # -Wconversion
    # -Wsign-conversion 
    # -Wnull-dereference 
    # -Wdouble-promotion
    # -Wformat=2 
    # -Wimplicit-fallthrough 
    # -Wunused 
    # -Werror
  )
endif()

# Find the required packages
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)  # Add SQLite3
find_package(TinyXML2 REQUIRED) # Add TinyXML2

include(FetchContent)

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

FetchContent_MakeAvailable(glaze imguicolortextedit)

add_library(imcolortextedit 
  ${imguicolortextedit_SOURCE_DIR}/TextEditor.cpp # Add the TextEditor implementation
)
target_include_directories(imcolortextedit PRIVATE 
  /usr/include/imgui 
  /usr/include/stb
  ${imguicolortextedit_SOURCE_DIR}
)

# Disable sign comparison warnings only for the imcolortextedit library
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(imcolortextedit PRIVATE -Wno-sign-compare -Wno-conversion)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(imcolortextedit PRIVATE -Wno-sign-compare)
endif()

# Add the executable
add_executable(rouen 
  src/rouen.cpp
  src/main_wnd.cpp
  src/fonts.cpp
  )

target_include_directories(rouen PRIVATE 
  /usr/include/imgui 
  /usr/include/stb
  ${imguicolortextedit_SOURCE_DIR}
  ${CURL_INCLUDE_DIRS}
  ${SQLite3_INCLUDE_DIRS}   # Add SQLite3 include dirs
  ${TINYXML2_INCLUDE_DIRS}  # Add TinyXML2 include dirs
)
target_link_libraries(rouen PRIVATE 
  imgui 
  glfw 
  GL 
  SDL2 
  SDL2_image 
  glaze::glaze 
  ${CURL_LIBRARIES}
  ${SQLite3_LIBRARIES}    # Add SQLite3 libraries
  tinyxml2                # Add TinyXML2 library
  imcolortextedit
)
