# Tests CMake configuration for Rouen project
cmake_minimum_required(VERSION 3.28)

# Project declaration
project(RouenTests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.4")
  add_compile_options(-stdlib=libc++)
  add_link_options(-stdlib=libc++)
endif()

# Enable testing
enable_testing()

# Find Google Test
find_package(GTest CONFIG REQUIRED)

# Find required dependencies for tests
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(SDL3_IMAGE REQUIRED sdl3-image)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

# Find TinyXML2 for RSS parsing tests
if(NOT TARGET tinyxml2::tinyxml2 AND NOT TARGET tinyxml2)
    find_package(tinyxml2 CONFIG REQUIRED)
endif()
if(NOT TARGET imgui AND NOT TARGET imgui::imgui)
    find_package(imgui CONFIG REQUIRED)
endif()
if(TARGET imgui AND NOT TARGET imgui::imgui)
    add_library(imgui::imgui ALIAS imgui)
endif()

# Include parent directories for access to source files
include_directories(..)
include_directories(../src)
include_directories(../src/helpers)
include_directories(../src/hosts)
include_directories(../src/models)
include_directories(../src/cards)
include_directories(../external)
include_directories(../external/mongoose-7.15)
include_directories(../external/quickjs)
include_directories(../external/imguicolortextedit)
include_directories(../external/imgui)
include_directories(../external/imgui/backends)
include_directories(../plugin-sdk)
include_directories(../external/PDFWriter/PDFWriter)
include_directories(../external/PDFWriter/FreeType/include)
include_directories(${SDL3_INCLUDE_DIRS})
include_directories(${SDL3_IMAGE_INCLUDE_DIRS})
if(imgui_SOURCE_DIR)
    include_directories(${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
endif()
if(IMGUI_INCLUDE_DIR)
    include_directories(${IMGUI_INCLUDE_DIR})
endif()

if(NOT COMMAND target_add_strict_warnings)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/warnings.cmake)
endif()

# C++20 Modules Library Target for Tests
add_library(rouen_test_modules STATIC)

target_sources(rouen_test_modules
  PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
  FILES
    ../src/models/rss/rss_date_parser.cppm
    ../src/models/rss/rss_url_resolver.cppm
    ../src/models/rss/feed_item.cppm
    ../src/models/mail/metadata_serialization.cppm
    ../src/helpers/config_service.cppm
    ../src/helpers/tag_manager.cppm
    ../src/helpers/theme_manager.cppm
    ../src/helpers/dynamic_library.cppm
)

target_sources(rouen_test_modules
  PRIVATE
    ../src/models/rss/rss_date_parser.cpp
    ../src/models/rss/rss_url_resolver.cpp
    ../src/models/rss/feed_item.cpp
    ../src/models/mail/metadata_serialization.cpp
    ../src/helpers/config_service.cpp
    ../src/helpers/tag_manager.cpp
    ../src/helpers/theme_manager.cpp
    ../src/helpers/dynamic_library.cpp
)

target_include_directories(rouen_test_modules PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/helpers
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/hosts
  ${CMAKE_CURRENT_SOURCE_DIR}/../external
  ${CMAKE_CURRENT_SOURCE_DIR}/../plugin-sdk
)

if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
  target_link_libraries(rouen_test_modules PUBLIC
    CURL::libcurl
    unofficial::sqlite3::sqlite3
    tinyxml2::tinyxml2
    glaze::glaze
    imgui
  )
else()
  target_link_libraries(rouen_test_modules PUBLIC
    CURL::libcurl
    SQLite::SQLite3
    glaze::glaze
  )
  if(TARGET imgui)
    target_link_libraries(rouen_test_modules PUBLIC imgui)
  elseif(TARGET imgui::imgui)
    target_link_libraries(rouen_test_modules PUBLIC imgui::imgui)
  endif()
  if(TARGET tinyxml2::tinyxml2)
    target_link_libraries(rouen_test_modules PUBLIC tinyxml2::tinyxml2)
  else()
    target_link_libraries(rouen_test_modules PUBLIC ${TINYXML2_LIBRARIES})
    target_include_directories(rouen_test_modules PUBLIC ${TINYXML2_INCLUDE_DIRS})
  endif()
  target_include_directories(rouen_test_modules PUBLIC
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
  )
endif()

if(imgui_SOURCE_DIR)
  target_include_directories(rouen_test_modules PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
endif()
if(IMGUI_INCLUDE_DIR)
  target_include_directories(rouen_test_modules PUBLIC ${IMGUI_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui_backends)
endif()

# Helper function to create GTest-based tests
function(add_gtest_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} 
        PRIVATE 
        rouen_test_modules
        GTest::gtest 
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        imgui::imgui
    )
    # Add test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    if(APPLE)
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/rouen.app/Contents/MacOS:${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # Set properties for better output
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        BUILD_RPATH "${CMAKE_BINARY_DIR}/rouen.app/Contents/MacOS;${CMAKE_CURRENT_BINARY_DIR}"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
        target_add_strict_warnings(${test_name})
    endif()
endfunction()

# Helper function to create GTest-based tests with HTTP dependencies
function(add_gtest_http_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    
    # Include directories needed for the test
    target_include_directories(${test_name} PRIVATE
        ../src
    )
    
    # Additional compiler definitions needed for the test
    target_compile_definitions(${test_name} PRIVATE
        TEST_CONFIG_SERVICE
    )
    
    target_link_libraries(${test_name} 
        PRIVATE 
        rouen_test_modules
        GTest::gtest 
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        CURL::libcurl
    )
    # Add test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Set properties for better output
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
        target_add_strict_warnings(${test_name})
    endif()
endfunction()

# Helper function to create GTest-based tests with RSS dependencies
function(add_gtest_rss_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    
    # Include directories needed for the test
    target_include_directories(${test_name} PRIVATE
        ../src
    )
    
    target_link_libraries(${test_name} 
        PRIVATE 
        rouen_test_modules
        GTest::gtest 
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        tinyxml2::tinyxml2
        CURL::libcurl
    )
    # Add test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Set properties for better output
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
        target_add_strict_warnings(${test_name})
    endif()
endfunction()

# Create Google Test-based tests
add_gtest_http_executable(test_fetch_ssl test_fetch_ssl.cpp)
add_gtest_http_executable(test_ssl_ui test_ssl_ui.cpp)
add_gtest_http_executable(test_ssl_modes_simple test_ssl_modes_simple.cpp)
add_gtest_executable(test_math_operations test_math_operations.cpp)
add_gtest_executable(test_weather_glaze test_weather_glaze.cpp)
add_gtest_executable(test_adaptive_cards test_adaptive_cards.cpp)
if(NOT MSVC)
    target_compile_options(test_adaptive_cards PRIVATE -Wno-error=sign-conversion -Wno-sign-conversion)
endif()
add_gtest_executable(test_adaptive_process_card test_adaptive_process_card.cpp)
target_sources(test_adaptive_process_card PRIVATE
    ../src/cards/interface/card.cpp
    ../src/cards/productivity/theme_card.cpp
    ../src/cards/productivity/invoice_card.cpp
    ../src/cards/productivity/calculator.cpp
    ../src/cards/productivity/converter.cpp
    ../src/cards/information/ai_chat.cpp
    ../src/cards/information/rss.cpp
    ../src/cards/information/rss_item.cpp
    ../src/cards/media/chess_com_integration.cpp
    ../src/cards/media/media_companion.cpp
    ../src/cards/development/cmake.cpp
    ../src/cards/development/fs-directory.cpp
    ../src/cards/development/git.cpp
    ../src/cards/development/git_overlay.cpp
    ../src/cards/development/github/github_card.cpp
    ../src/cards/development/github/ci_card.cpp
    ../src/cards/development/github/login_screen.cpp
    ../src/cards/development/github/repo_screen.cpp
    ../src/cards/production/adlib.cpp
    ../src/cards/system/terminal.cpp
    ../src/cards/system/terminal_bash.cpp
    ../src/cards/system/terminal_output.cpp
    ../src/cards/system/terminal_commands.cpp
    ../src/hosts/mcp_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/hosts/git_sync_host.cpp
    ../src/hosts/llm_host.cpp
    ../src/hosts/event_bus_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/hosts/footprints_host.cpp
    ../src/hosts/process_host.cpp
    ../src/hosts/quickjs_host.cpp
    ../src/hosts/plugin_host.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/piped_process.cpp
    ../src/helpers/card_decorations.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/ui_automation_explorer.cpp
    ../src/helpers/directory_watch.cpp
    ../src/helpers/filetype_handler.cpp
    ../src/helpers/capture_helper.cpp
    ../src/models/git_process_helper.cpp
    ../src/models/git_scanner.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/fonts.cpp
    ../external/imguicolortextedit/TextEditor.cpp
)
if(APPLE)
    target_sources(test_adaptive_process_card PRIVATE ../src/helpers/mac_mic_permissions.mm ../src/helpers/app_icon.mm)
    target_link_libraries(test_adaptive_process_card PRIVATE "-framework AVFoundation" "-framework Foundation" "-framework ApplicationServices" "-framework Cocoa")
endif()
target_link_libraries(test_adaptive_process_card PRIVATE SQLite::SQLite3 ${SDL3_LIBRARIES} ${SDL3_IMAGE_LIBRARIES} CURL::libcurl ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES} ${SWRESAMPLE_LIBRARIES} tinyxml2::tinyxml2 PDFWriter imcolortextedit quickjs imgui_backends $<IF:$<TARGET_EXISTS:pdfium::pdfium>,pdfium::pdfium,$<IF:$<TARGET_EXISTS:pdfium>,pdfium,>>)
if(NOT MSVC)
    target_compile_options(test_adaptive_process_card PRIVATE -Wno-error=sign-conversion -Wno-sign-conversion)
endif()
add_gtest_executable(test_card_adaptive_interface test_card_adaptive_interface.cpp)
target_sources(test_card_adaptive_interface PRIVATE
    ../src/cards/interface/card.cpp
    ../src/cards/productivity/theme_card.cpp
    ../src/cards/productivity/invoice_card.cpp
    ../src/cards/information/ai_chat.cpp
    ../src/cards/information/rss.cpp
    ../src/cards/information/rss_item.cpp
    ../src/hosts/api_server_host.cpp
    ../external/mongoose-7.15/mongoose.c
    ../src/hosts/mcp_host.cpp
    ../src/hosts/llm_host.cpp
    ../src/hosts/event_bus_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/hosts/git_sync_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/hosts/footprints_host.cpp
    ../src/hosts/process_host.cpp
    ../src/helpers/ui_automation_explorer.cpp
    ../src/helpers/directory_watch.cpp
    ../src/cards/media/chess_com_integration.cpp
    ../src/cards/media/media_companion.cpp
    ../src/cards/development/cmake.cpp
    ../src/cards/development/fs-directory.cpp
    ../src/cards/development/git.cpp
    ../src/cards/development/github/github_card.cpp
    ../src/cards/development/github/ci_card.cpp
    ../src/cards/development/github/login_screen.cpp
    ../src/cards/development/github/repo_screen.cpp
    ../src/cards/productivity/calculator.cpp
    ../src/cards/productivity/converter.cpp
    ../src/cards/system/terminal.cpp
    ../src/cards/system/terminal_bash.cpp
    ../src/cards/system/terminal_output.cpp
    ../src/cards/system/terminal_commands.cpp
    ../src/hosts/quickjs_host.cpp
    ../src/helpers/filetype_handler.cpp
    ../src/helpers/capture_helper.cpp
    ../src/cards/development/git_overlay.cpp
    ../src/cards/production/adlib.cpp
    ../src/models/git_process_helper.cpp
    ../src/models/git_scanner.cpp
    ../src/hosts/plugin_host.cpp
    ../external/imguicolortextedit/TextEditor.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/piped_process.cpp
    ../src/helpers/card_decorations.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/theme_manager.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/image_cache.cpp
    ../src/fonts.cpp
)
if(APPLE)
    target_sources(test_card_adaptive_interface PRIVATE ../src/helpers/mac_mic_permissions.mm ../src/helpers/app_icon.mm)
    target_link_libraries(test_card_adaptive_interface PRIVATE "-framework AVFoundation" "-framework Foundation" "-framework ApplicationServices" "-framework Cocoa")
endif()
target_link_libraries(test_card_adaptive_interface PRIVATE SQLite::SQLite3 ${SDL3_LIBRARIES} ${SDL3_IMAGE_LIBRARIES} CURL::libcurl ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES} ${SWRESAMPLE_LIBRARIES} tinyxml2::tinyxml2 PDFWriter imcolortextedit quickjs imgui_backends $<IF:$<TARGET_EXISTS:pdfium::pdfium>,pdfium::pdfium,$<IF:$<TARGET_EXISTS:pdfium>,pdfium,>>)
if(NOT MSVC)
    target_compile_options(test_card_adaptive_interface PRIVATE -Wno-error=sign-conversion -Wno-sign-conversion)
endif()

add_gtest_executable(test_adaptive_cards_repository test_adaptive_cards_repository.cpp)
target_link_libraries(test_adaptive_cards_repository PRIVATE SQLite::SQLite3)
add_gtest_http_executable(test_adaptive_card_image test_adaptive_card_image.cpp)
target_sources(test_adaptive_card_image PRIVATE ../src/helpers/image_cache.cpp)
target_link_libraries(test_adaptive_card_image PRIVATE SQLite::SQLite3 ${SDL3_LIBRARIES} ${SDL3_IMAGE_LIBRARIES})
if(NOT MSVC)
    target_compile_options(test_adaptive_card_image PRIVATE -Wno-error=sign-conversion -Wno-sign-conversion)
endif()
add_gtest_executable(test_markdown_renderer test_markdown_renderer.cpp)
add_gtest_http_executable(test_contact_gravatar test_contact_gravatar.cpp)

# RSS Media Extraction Test - tests HTML media extraction and RSS parsing
add_gtest_rss_executable(test_rss_media_extraction test_rss_media_extraction.cpp)

# RSS Autodiscovery Test - tests HTML RSS feed link autodiscovery
add_gtest_rss_executable(test_rss_autodiscovery test_rss_autodiscovery.cpp)

# RSS Tags Test
add_gtest_rss_executable(test_rss_tags test_rss_tags.cpp)
target_link_libraries(test_rss_tags PRIVATE SQLite::SQLite3)

# RT Media Extraction Test - tests RT.com specific RSS media extraction patterns
add_gtest_rss_executable(test_rt_media test_rt_media.cpp)

# RSS Watermark Test
add_gtest_rss_executable(test_rss_watermark test_rss_watermark.cpp)
target_sources(test_rss_watermark PRIVATE 
    ../src/hosts/event_bus_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/vu_meter.cpp
)
if(APPLE)
    target_sources(test_rss_watermark PRIVATE ../src/helpers/mac_mic_permissions.mm)
    target_link_libraries(test_rss_watermark PRIVATE "-framework Foundation" "-framework AVFoundation")
endif()
target_link_libraries(test_rss_watermark 
    PRIVATE 
    SQLite::SQLite3
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    imgui::imgui
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)
add_test(NAME RSSWatermarkTest COMMAND test_rss_watermark)

# RSS Diagnostics Test
add_gtest_rss_executable(test_rss_diagnostics test_rss_diagnostics.cpp)
target_sources(test_rss_diagnostics PRIVATE 
    ../src/hosts/event_bus_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/vu_meter.cpp
)
if(APPLE)
    target_sources(test_rss_diagnostics PRIVATE ../src/helpers/mac_mic_permissions.mm)
    target_link_libraries(test_rss_diagnostics PRIVATE "-framework Foundation" "-framework AVFoundation")
endif()
target_link_libraries(test_rss_diagnostics 
    PRIVATE 
    SQLite::SQLite3
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    imgui::imgui
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)
add_test(NAME RSSDiagnosticsTest COMMAND test_rss_diagnostics)

# Audio Time Progression Test
add_gtest_rss_executable(test_audio_time_progression test_audio_time_progression.cpp)
target_sources(test_audio_time_progression PRIVATE 
    ../src/hosts/event_bus_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/vu_meter.cpp
)
if(APPLE)
    target_sources(test_audio_time_progression PRIVATE ../src/helpers/mac_mic_permissions.mm)
    target_link_libraries(test_audio_time_progression PRIVATE "-framework Foundation" "-framework AVFoundation")
endif()
target_link_libraries(test_audio_time_progression 
    PRIVATE 
    SQLite::SQLite3
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    imgui::imgui
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)

# Objectives Test
add_gtest_executable(test_objectives test_objectives.cpp)
target_link_libraries(test_objectives 
    PRIVATE 
    SQLite::SQLite3
)

# Deck Scrolling Test
add_gtest_executable(test_deck_scrolling test_deck_scrolling.cpp)
target_sources(test_deck_scrolling PRIVATE
    ../src/hosts/event_bus_host.cpp
    ../src/cards/interface/card.cpp
    ../src/helpers/image_cache.cpp
    ../src/hosts/process_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/hosts/footprints_host.cpp
    ../src/hosts/llm_host.cpp
    ../src/hosts/git_sync_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/hosts/plugin_host.cpp
    ../src/hosts/quickjs_host.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/directory_watch.cpp
    ../src/helpers/ui_automation_explorer.cpp
    ../src/helpers/piped_process.cpp
    ../src/models/git_scanner.cpp
    ../src/models/git_process_helper.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/fonts.cpp
    ../src/cards/information/ai_chat.cpp
    ../src/cards/information/rss.cpp
    ../src/cards/information/rss_item.cpp
    ../src/cards/productivity/converter.cpp
    ../src/cards/productivity/calculator.cpp
    ../src/cards/productivity/invoice_card.cpp
    ../src/cards/productivity/theme_card.cpp
    ../src/cards/development/cmake.cpp
    ../src/cards/development/fs-directory.cpp
    ../src/cards/development/git.cpp
    ../src/cards/development/git_overlay.cpp
    ../src/cards/development/github_registrar.cpp
    ../src/cards/development/github/ci_card.cpp
    ../src/cards/development/github/github_card.cpp
    ../src/cards/development/github/login_screen.cpp
    ../src/cards/development/github/repo_screen.cpp
    ../src/cards/information/bybit_registrar.cpp
    ../src/cards/information/movies_registrar.cpp
    ../src/cards/media/chess_com_integration.cpp
    ../src/cards/media/media_companion.cpp
    ../src/cards/production/adlib.cpp
    ../src/cards/system/terminal.cpp
    ../src/cards/system/terminal_output.cpp
    ../src/cards/system/terminal_bash.cpp
    ../src/cards/system/terminal_commands.cpp
    ../src/hosts/mcp_host.cpp
    ../src/helpers/filetype_handler.cpp
    ../src/helpers/card_decorations.cpp
    ../src/helpers/capture_helper.cpp
    ../external/imguicolortextedit/TextEditor.cpp
)
if(APPLE)
    target_sources(test_deck_scrolling PRIVATE ../src/helpers/mac_mic_permissions.mm ../src/helpers/app_icon.mm)
    target_link_libraries(test_deck_scrolling PRIVATE "-framework Cocoa" "-framework Foundation" "-framework AVFoundation" "-framework ApplicationServices")
endif()
target_link_libraries(test_deck_scrolling PRIVATE quickjs pdfium PDFWriter tinyxml2::tinyxml2 SQLite::SQLite3 ${SDL3_LIBRARIES} ${SDL3_IMAGE_LIBRARIES} imgui::imgui imgui_backends CURL::libcurl ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES} ${SWRESAMPLE_LIBRARIES})

# Camera Device List Test
add_gtest_executable(test_camera_device_list test_camera_device_list.cpp)
target_link_libraries(test_camera_device_list PRIVATE ${SDL3_LIBRARIES})

add_gtest_executable(test_sdl3_camera_open test_sdl3_camera_open.cpp)
target_link_libraries(test_sdl3_camera_open PRIVATE ${SDL3_LIBRARIES})

add_gtest_executable(test_camera_gpu_upload_safety test_camera_gpu_upload_safety.cpp)
target_link_libraries(test_camera_gpu_upload_safety PRIVATE ${SDL3_LIBRARIES})

add_gtest_executable(test_pixel_conversion test_pixel_conversion.cpp)
target_link_libraries(test_pixel_conversion PRIVATE ${SDL3_LIBRARIES})

# Terminal CWD Tracking Test
add_executable(test_terminal_cd
    test_terminal_cd.cpp
    ../src/cards/system/terminal_bash.cpp
    ../src/cards/system/terminal_output.cpp
)
target_include_directories(test_terminal_cd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_terminal_cd
    PRIVATE
    rouen_test_modules
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
    CURL::libcurl
    imgui::imgui
)
add_test(NAME TerminalCdTest COMMAND test_terminal_cd)
set_target_properties(test_terminal_cd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
    target_add_strict_warnings(test_terminal_cd)
endif()

# Gemini Adapter Test
add_gtest_http_executable(test_gemini_adapter test_gemini_adapter.cpp)

# MCP Test
add_executable(test_mcp 
    test_mcp.cpp 
    ../src/hosts/event_bus_host.cpp
    ../src/hosts/mcp_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/hosts/git_sync_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/hosts/llm_host.cpp
    ../src/hosts/footprints_host.cpp
    ../src/hosts/process_host.cpp
    ../src/hosts/quickjs_host.cpp
    ../src/hosts/plugin_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/models/git_process_helper.cpp
    ../src/models/git_scanner.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/card_decorations.cpp
    ../src/helpers/ui_automation_explorer.cpp
    ../src/helpers/directory_watch.cpp
    ../src/helpers/filetype_handler.cpp
    ../src/helpers/capture_helper.cpp
    ../src/helpers/piped_process.cpp
    ../src/cards/interface/card.cpp
    ../src/cards/information/rss.cpp
    ../src/cards/information/rss_item.cpp
    ../src/cards/information/ai_chat.cpp
    ../src/cards/productivity/theme_card.cpp
    ../src/cards/productivity/invoice_card.cpp
    ../src/cards/productivity/calculator.cpp
    ../src/cards/productivity/converter.cpp
    ../src/cards/media/chess_com_integration.cpp
    ../src/cards/media/media_companion.cpp
    ../src/cards/development/cmake.cpp
    ../src/cards/development/fs-directory.cpp
    ../src/cards/development/git.cpp
    ../src/cards/development/git_overlay.cpp
    ../src/cards/development/github/github_card.cpp
    ../src/cards/development/github/ci_card.cpp
    ../src/cards/development/github/login_screen.cpp
    ../src/cards/development/github/repo_screen.cpp
    ../src/cards/production/adlib.cpp
    ../src/cards/system/terminal.cpp
    ../src/cards/system/terminal_bash.cpp
    ../src/cards/system/terminal_output.cpp
    ../src/cards/system/terminal_commands.cpp
    ../src/fonts.cpp
    ../external/imguicolortextedit/TextEditor.cpp
)
if(APPLE)
    target_sources(test_mcp PRIVATE ../src/helpers/mac_mic_permissions.mm ../src/helpers/app_icon.mm)
    target_link_libraries(test_mcp PRIVATE "-framework Cocoa" "-framework Foundation" "-framework AVFoundation" "-framework ApplicationServices")
endif()
target_link_libraries(test_mcp 
    PRIVATE 
    rouen_test_modules
    GTest::gtest 
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
    CURL::libcurl
    imgui::imgui
    imgui_backends
    tinyxml2::tinyxml2
    SQLite::SQLite3
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    quickjs
    PDFWriter
    $<IF:$<TARGET_EXISTS:pdfium::pdfium>,pdfium::pdfium,$<IF:$<TARGET_EXISTS:pdfium>,pdfium,>>
)
add_test(NAME MCPTest COMMAND test_mcp)
set_target_properties(test_mcp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
    target_add_strict_warnings(test_mcp)
endif()

# TextEditor Functionality Unit Test
add_executable(test_text_editor
    test_text_editor.cpp
    ../external/imguicolortextedit/TextEditor.cpp
    ../external/imgui_backends/imgui_impl_sdl3.cpp
    ../external/imgui_backends/imgui_impl_sdlgpu3.cpp
    ../src/fonts.cpp
    ../src/helpers/capture_helper.cpp
)
target_include_directories(test_text_editor PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../external
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui_backends
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imguicolortextedit
)
target_link_libraries(test_text_editor
    PRIVATE
    rouen_test_modules
    GTest::gtest
    GTest::gtest_main
    imgui::imgui
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
)
add_test(NAME TextEditorTest COMMAND test_text_editor)
set_target_properties(test_text_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Editor Screenshot Unit Test
add_executable(test_editor_screenshot
    test_editor_screenshot.cpp
    ../external/imguicolortextedit/TextEditor.cpp
    ../external/imgui_backends/imgui_impl_sdl3.cpp
    ../external/imgui_backends/imgui_impl_sdlgpu3.cpp
    ../src/fonts.cpp
    ../src/helpers/capture_helper.cpp
)
target_include_directories(test_editor_screenshot PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../external
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui_backends
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imguicolortextedit
)
target_link_libraries(test_editor_screenshot
    PRIVATE
    rouen_test_modules
    GTest::gtest
    GTest::gtest_main
    imgui::imgui
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
)
add_test(NAME EditorScreenshotTest COMMAND test_editor_screenshot)
set_target_properties(test_editor_screenshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# TextEditor Scrolling Unit Test
add_executable(test_text_editor_scrolling
    test_text_editor_scrolling.cpp
    ../external/imguicolortextedit/TextEditor.cpp
)
target_include_directories(test_text_editor_scrolling PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../external
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imguicolortextedit
)
target_link_libraries(test_text_editor_scrolling
    PRIVATE
    rouen_test_modules
    GTest::gtest
    GTest::gtest_main
    imgui::imgui
)
add_test(NAME TextEditorScrollingTest COMMAND test_text_editor_scrolling)
set_target_properties(test_text_editor_scrolling PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
    # target_add_strict_warnings(test_text_editor_scrolling)
endif()



# Legacy tests (keeping for compatibility)
# Test for Bybit currency conversion fix
add_executable(test_bybit_currency_fix 
    test_currency_fix.cpp
)
target_link_libraries(test_bybit_currency_fix PRIVATE rouen_test_modules)
add_test(NAME BybitCurrencyFixTest COMMAND test_bybit_currency_fix)

# Example test for basic math operations
add_executable(test_example_math
    test_example_math.cpp
)
target_link_libraries(test_example_math PRIVATE rouen_test_modules)
add_test(NAME ExampleMathTest COMMAND test_example_math)

# Event Bus infrastructure unit test
add_executable(test_event_bus
    test_event_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hosts/event_bus_host.cpp
)
target_link_libraries(test_event_bus PRIVATE rouen_test_modules)
add_test(NAME EventBusTest COMMAND test_event_bus)

add_executable(test_notes_repository_legacy
    test_notes_repository_legacy.cpp
)
target_link_libraries(test_notes_repository_legacy PRIVATE rouen_test_modules SQLite::SQLite3)
add_test(NAME NotesRepositoryLegacyTest COMMAND test_notes_repository_legacy)

add_executable(test_series_repository
    test_series_repository.cpp
)
target_link_libraries(test_series_repository PRIVATE rouen_test_modules SQLite::SQLite3)
add_test(NAME SeriesRepositoryTest COMMAND test_series_repository)

# Set output directory for test executables
set_target_properties(test_bybit_currency_fix test_example_math test_notes_repository_legacy test_series_repository test_rss_watermark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# UI Automation Explorer Test
add_gtest_executable(test_ui_automation_explorer test_ui_automation_explorer.cpp)
target_sources(test_ui_automation_explorer PRIVATE ../src/helpers/ui_automation_explorer.cpp)
if(APPLE)
    target_link_libraries(test_ui_automation_explorer PRIVATE "-framework ApplicationServices" "-framework CoreFoundation")
elseif(WIN32)
    target_link_libraries(test_ui_automation_explorer PRIVATE ole32 oleaut32 uiautomationcore)
endif()

# Add QuickJS static library for tests if not already defined by main CMakeLists
if(NOT TARGET quickjs)
    set(QUICKJS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs/quickjs.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs/dtoa.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs/libunicode.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs/cutils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs/libregexp.c
    )
    if(NOT WIN32)
        list(APPEND QUICKJS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs/quickjs-libc.c)
    endif()
    add_library(quickjs STATIC ${QUICKJS_SOURCES})
    set_target_properties(quickjs PROPERTIES LINKER_LANGUAGE C)
    target_include_directories(quickjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs)
    if(NOT MSVC)
        target_compile_definitions(quickjs PRIVATE _GNU_SOURCE CONFIG_VERSION="2024-01-13")
    else()
        target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="2024-01-13")
    endif()
    set_source_files_properties(${QUICKJS_SOURCES} PROPERTIES LANGUAGE C)
    if(NOT MSVC)
        set_source_files_properties(${QUICKJS_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-array-bounds -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough")
    endif()
endif()

# QuickJS Unit Test
add_gtest_executable(test_quickjs test_quickjs.cpp)
target_sources(test_quickjs PRIVATE
    ../src/hosts/quickjs_host.cpp
    ../src/hosts/event_bus_host.cpp
    ../src/cards/interface/card.cpp
    ../src/helpers/image_cache.cpp
    ../src/hosts/process_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/hosts/footprints_host.cpp
    ../src/hosts/llm_host.cpp
    ../src/hosts/git_sync_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/hosts/plugin_host.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/directory_watch.cpp
    ../src/helpers/ui_automation_explorer.cpp
    ../src/helpers/piped_process.cpp
    ../src/models/git_scanner.cpp
    ../src/models/git_process_helper.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/fonts.cpp
    ../src/cards/information/ai_chat.cpp
    ../src/cards/information/rss.cpp
    ../src/cards/information/rss_item.cpp
    ../src/cards/productivity/converter.cpp
    ../src/cards/productivity/calculator.cpp
    ../src/cards/productivity/invoice_card.cpp
    ../src/cards/productivity/theme_card.cpp
    ../src/cards/development/cmake.cpp
    ../src/cards/development/fs-directory.cpp
    ../src/cards/development/git.cpp
    ../src/cards/development/git_overlay.cpp
    ../src/cards/development/github_registrar.cpp
    ../src/cards/development/github/ci_card.cpp
    ../src/cards/development/github/github_card.cpp
    ../src/cards/development/github/login_screen.cpp
    ../src/cards/development/github/repo_screen.cpp
    ../src/cards/information/bybit_registrar.cpp
    ../src/cards/information/movies_registrar.cpp
    ../src/cards/information/worldcup_registrar.cpp
    ../src/cards/media/chess_com_registrar.cpp
    ../src/cards/media/chess_com_integration.cpp
    ../src/cards/media/media_companion.cpp
    ../src/cards/production/adlib.cpp
    ../src/cards/system/terminal.cpp
    ../src/cards/system/terminal_output.cpp
    ../src/cards/system/terminal_bash.cpp
    ../src/cards/system/terminal_commands.cpp
    ../src/hosts/mcp_host.cpp
    ../src/helpers/filetype_handler.cpp
    ../src/helpers/card_decorations.cpp
    ../src/helpers/capture_helper.cpp
    ../external/imguicolortextedit/TextEditor.cpp
)
if(APPLE)
    target_sources(test_quickjs PRIVATE ../src/helpers/mac_mic_permissions.mm ../src/helpers/app_icon.mm)
    target_link_libraries(test_quickjs PRIVATE "-framework Cocoa" "-framework Foundation" "-framework AVFoundation" "-framework ApplicationServices")
endif()
target_include_directories(test_quickjs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../external
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin-sdk
)
target_link_libraries(test_quickjs PRIVATE 
    rouen_test_modules
    quickjs
    pdfium
    PDFWriter
    tinyxml2::tinyxml2
    SQLite::SQLite3 
    CURL::libcurl 
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    imgui::imgui
    imgui_backends
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
)
if(NOT MSVC)
    target_compile_options(test_quickjs PRIVATE -Wno-error -Wno-c99-extensions -Wno-old-style-cast -Wno-sign-conversion -Wno-unused-parameter -Wno-shorten-64-to-32 -Wno-implicit-int-float-conversion)
endif()

# Create a comprehensive test runner that builds and runs all tests
add_custom_target(run_all_tests
    COMMAND echo "Running Rouen Test Suite..."
    COMMAND echo "============================="
    COMMAND echo "Google Test Suite:"
    COMMAND echo "-------------------"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_fetch_ssl
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_ssl_ui
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_math_operations
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_weather_glaze
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_adaptive_cards
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_markdown_renderer
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rss_media_extraction
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rt_media
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rss_watermark
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_gemini_adapter
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_mcp
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_ui_automation_explorer
    COMMAND echo ""
    COMMAND echo "Legacy Console Tests:"
    COMMAND echo "---------------------"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_notes_repository_legacy
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_series_repository
    DEPENDS test_fetch_ssl test_ssl_ui test_math_operations test_weather_glaze test_adaptive_cards test_markdown_renderer test_rss_media_extraction test_rt_media test_rss_watermark test_gemini_adapter test_mcp test_ui_automation_explorer test_bybit_currency_fix test_example_math test_notes_repository_legacy test_series_repository
    COMMENT "Running complete Rouen test suite"
)

# Google Test specific targets
add_custom_target(run_gtest_only
    COMMAND echo "Running Google Test Suite Only..."
    COMMAND echo "=================================="
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_fetch_ssl
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_ssl_ui
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_math_operations
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_weather_glaze
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_adaptive_cards
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_markdown_renderer
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rss_media_extraction
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rt_media
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rss_watermark
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_gemini_adapter
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_mcp
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_ui_automation_explorer
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_quickjs
    DEPENDS test_fetch_ssl test_ssl_ui test_math_operations test_weather_glaze test_adaptive_cards test_markdown_renderer test_rss_media_extraction test_rt_media test_rss_watermark test_gemini_adapter test_mcp test_ui_automation_explorer test_quickjs
    COMMENT "Running Google Test suite only"
)

# Legacy test targets (keeping for compatibility)
add_custom_target(run_legacy_tests
    COMMAND echo "Running Legacy Console Tests..."
    COMMAND echo "================================"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_notes_repository_legacy
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_series_repository
    DEPENDS test_bybit_currency_fix test_example_math test_notes_repository_legacy test_series_repository
    COMMENT "Running legacy console tests"
)

# Individual test targets for convenience
add_custom_target(run_currency_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    DEPENDS test_bybit_currency_fix
    COMMENT "Running Bybit currency conversion test"
)

add_custom_target(run_math_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    DEPENDS test_example_math
    COMMENT "Running basic math example test"
)

# Individual Google Test targets
add_custom_target(run_fetch_ssl_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_fetch_ssl
    DEPENDS test_fetch_ssl
    COMMENT "Running HTTP Fetch SSL configuration tests"
)

add_custom_target(run_ssl_ui_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_ssl_ui
    DEPENDS test_ssl_ui
    COMMENT "Running SSL UI configuration tests"
)

add_custom_target(run_math_operations_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_math_operations
    DEPENDS test_math_operations
    COMMENT "Running advanced math operations tests"
)

add_custom_target(run_rss_media_extraction_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rss_media_extraction
    DEPENDS test_rss_media_extraction
    COMMENT "Running RSS media extraction and HTML parsing tests"
)

add_custom_target(run_rt_media_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rt_media
    DEPENDS test_rt_media
    COMMENT "Running RT.com specific RSS media extraction tests"
)

# Enable CTest integration
add_custom_target(run_ctest
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --verbose
    DEPENDS test_fetch_ssl test_ssl_ui test_math_operations test_weather_glaze test_adaptive_cards test_markdown_renderer test_rss_media_extraction test_rt_media test_bybit_currency_fix test_example_math test_objectives test_ui_automation_explorer
    COMMENT "Running tests through CTest framework"
)

# Add a clean target for tests
add_custom_target(clean_tests
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/test_*
    COMMENT "Cleaning test executables"
)
