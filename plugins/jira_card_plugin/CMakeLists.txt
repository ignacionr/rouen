cmake_minimum_required(VERSION 3.30)
project(rouen_jira_card_plugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Determine path to Rouen root directory
get_filename_component(ROUEN_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Pinned to exact ImGui tag matching main Rouen build
if(NOT TARGET imgui)
  find_package(imgui CONFIG QUIET)
  if(NOT TARGET imgui AND NOT TARGET imgui::imgui)
    include(FetchContent)
    FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.91.7
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
  elseif(TARGET imgui::imgui AND NOT TARGET imgui)
    add_library(imgui ALIAS imgui::imgui)
  endif()
endif()

# Find CURL for jira_model network calls
find_package(CURL REQUIRED)

# Find Glaze for JSON serialization
find_package(glaze CONFIG QUIET)
if(NOT glaze_FOUND AND NOT TARGET glaze AND NOT TARGET glaze::glaze)
  find_package(Glaze CONFIG QUIET)
  if(NOT Glaze_FOUND AND NOT TARGET glaze::glaze AND NOT TARGET Glaze::Glaze)
    include(FetchContent)
    FetchContent_Declare(
      glaze
      GIT_REPOSITORY https://github.com/stephenberry/glaze.git
      GIT_TAG v1.9.3
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glaze)
  endif()
endif()

add_library(jira_card_plugin SHARED
  src/plugin_entry.cpp
  src/jira_model_core.cpp
  src/jira_model_connection.cpp
  src/jira_model_profiles.cpp
  src/jira_model_projects.cpp
  src/jira_model_issues.cpp
  src/jira_model_api.cpp
)

target_include_directories(jira_card_plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${ROUEN_ROOT_DIR}/plugin-sdk
  ${ROUEN_ROOT_DIR}/src
  ${ROUEN_ROOT_DIR}/external
)

target_link_libraries(jira_card_plugin PRIVATE
  imgui
  CURL::libcurl
)

if(TARGET glaze::glaze)
  target_link_libraries(jira_card_plugin PRIVATE glaze::glaze)
elseif(TARGET glaze)
  target_link_libraries(jira_card_plugin PRIVATE glaze)
endif()

set_target_properties(jira_card_plugin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)

if(MSVC)
  set_property(TARGET jira_card_plugin PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  )
  target_compile_options(jira_card_plugin PRIVATE /W4 /permissive- /utf-8)
elseif(APPLE)
  target_compile_options(jira_card_plugin PRIVATE -stdlib=libc++)
endif()
