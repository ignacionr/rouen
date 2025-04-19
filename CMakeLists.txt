cmake_minimum_required(VERSION 3.30)

# Project name and version
project(rouen VERSION 1.0 LANGUAGES CXX)

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glaze)

# Add the executable
add_executable(rouen src/rouen.cpp)
target_include_directories(rouen PRIVATE /usr/include/imgui /usr/include/stb)
target_link_libraries(rouen PRIVATE imgui glfw GL SDL2 SDL2_image glaze::glaze)
