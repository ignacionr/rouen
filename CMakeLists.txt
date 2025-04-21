cmake_minimum_required(VERSION 3.30)

# Project name and version
project(rouen VERSION 1.0 LANGUAGES CXX)

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat warnings as errors
add_compile_options(-Werror)

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

# Add the executable
add_executable(rouen 
  src/rouen.cpp
  src/main_wnd.cpp
  src/fonts.cpp
  ${imguicolortextedit_SOURCE_DIR}/TextEditor.cpp # Add the TextEditor implementation
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
)
