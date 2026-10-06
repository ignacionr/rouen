cmake_minimum_required(VERSION 3.25)
project(cpp23_task_queue VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp23_task_queue main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(cpp23_task_queue PRIVATE Threads::Threads)
