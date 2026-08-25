cmake_minimum_required(VERSION 3.20)
project(trello_card_plugin CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_TARGET_NAME trello_card_plugin)

set(PLUGIN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../plugin-sdk")
set(ROUEN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(ROUEN_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external")

file(GLOB_RECURSE PLUGIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(${PLUGIN_TARGET_NAME} SHARED ${PLUGIN_SOURCES})

target_include_directories(${PLUGIN_TARGET_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${PLUGIN_SDK_DIR}"
        "${ROUEN_SRC_DIR}"
        "${ROUEN_EXTERNAL_DIR}"
)

target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE
    DEBUG_BUILD
    DEBUG_ROUEN=1
)

find_package(glaze REQUIRED)
target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE glaze::glaze)

find_package(CURL REQUIRED)
target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE CURL::libcurl)

target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE imgui_backends)

if(APPLE)
    target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE
        "-framework Foundation"
        "-framework AppKit"
    )
endif()

set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES
    PREFIX "lib"
    SUFFIX ".dylib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)
