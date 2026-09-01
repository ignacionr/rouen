# Tests CMake configuration for Rouen project
cmake_minimum_required(VERSION 3.16)

# Project declaration
project(RouenTests LANGUAGES CXX)

# Set C++23 standard for tests
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
include_directories(../external/imguicolortextedit)
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

# Helper function to create GTest-based tests
function(add_gtest_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} 
        PRIVATE 
        GTest::gtest 
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        imgui::imgui
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

# Helper function to create GTest-based tests with HTTP dependencies
function(add_gtest_http_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    
    # Add sources needed from the main project
    target_sources(${test_name} PRIVATE
        ../src/helpers/config_service.cpp
    )

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
    
    # Add RSS-related sources needed from the main project
    target_sources(${test_name} PRIVATE
        ../src/models/rss/rss_date_parser.cpp
        ../src/models/rss/feed_item.cpp
        ../src/models/rss/rss_url_resolver.cpp
    )

    # Include directories needed for the test
    target_include_directories(${test_name} PRIVATE
        ../src
    )
    
    target_link_libraries(${test_name} 
        PRIVATE 
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
    ../src/hosts/mcp_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/piped_process.cpp
    ../src/helpers/theme_manager.cpp
    ../src/helpers/card_decorations.cpp
    ../src/helpers/tag_manager.cpp
    ../src/fonts.cpp
)
target_link_libraries(test_adaptive_process_card PRIVATE SQLite::SQLite3 ${SDL3_LIBRARIES} CURL::libcurl)
if(NOT MSVC)
    target_compile_options(test_adaptive_process_card PRIVATE -Wno-error=sign-conversion -Wno-sign-conversion)
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
target_sources(test_rss_tags PRIVATE ../src/helpers/tag_manager.cpp)
target_link_libraries(test_rss_tags PRIVATE SQLite::SQLite3)

# RT Media Extraction Test - tests RT.com specific RSS media extraction patterns
add_gtest_rss_executable(test_rt_media test_rt_media.cpp)

# RSS Watermark Test
add_gtest_rss_executable(test_rss_watermark test_rss_watermark.cpp)
target_sources(test_rss_watermark PRIVATE 
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/config_service.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/tag_manager.cpp
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
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/config_service.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/tag_manager.cpp
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
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/config_service.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/tag_manager.cpp
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
add_test(NAME AudioTimeProgressionTest COMMAND test_audio_time_progression)

# Objectives Test
add_gtest_executable(test_objectives test_objectives.cpp)
target_link_libraries(test_objectives 
    PRIVATE 
    SQLite::SQLite3
)
add_test(NAME ObjectivesTest COMMAND test_objectives)

# Deck Scrolling Test
add_gtest_executable(test_deck_scrolling test_deck_scrolling.cpp)
target_sources(test_deck_scrolling PRIVATE
    ../src/cards/interface/card.cpp
    ../src/helpers/config_service.cpp
    ../src/helpers/theme_manager.cpp
)
target_link_libraries(test_deck_scrolling PRIVATE SQLite::SQLite3 ${SDL3_LIBRARIES} ${SDL3_IMAGE_LIBRARIES} imgui::imgui CURL::libcurl ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES} ${SWRESAMPLE_LIBRARIES})
set_target_properties(test_deck_scrolling PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME DeckScrollingTest COMMAND test_deck_scrolling)

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
    ../src/helpers/config_service.cpp
)
target_include_directories(test_terminal_cd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_terminal_cd
    PRIVATE
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
    ../src/hosts/mcp_host.cpp
    ../src/hosts/universal_sync_host.cpp
    ../src/hosts/git_sync_host.cpp
    ../src/hosts/rss_host.cpp
    ../src/models/rss/feed_xml_parser.cpp
    ../src/models/rss/rss_item_repo.cpp
    ../src/models/rss/rss_url_resolver.cpp
    ../src/models/rss/rss_date_parser.cpp
    ../src/models/rss/feed_item.cpp
    ../src/models/mail/metadata_serialization.cpp
    ../src/models/git_process_helper.cpp
    ../src/models/git_scanner.cpp
    ../src/helpers/config_service.cpp
    ../src/helpers/media_player.cpp
    ../src/helpers/media_player_item.cpp
    ../src/helpers/ytdlp_service.cpp
    ../src/helpers/audio_capture.cpp
    ../src/helpers/adlib_engine.cpp
    ../src/helpers/mp4_writer.cpp
    ../src/helpers/vu_meter.cpp
    ../src/helpers/image_cache.cpp
    ../src/helpers/tag_manager.cpp
    ../src/helpers/card_decorations.cpp
    ../src/cards/interface/card.cpp
    ../src/cards/information/rss.cpp
    ../src/helpers/theme_manager.cpp
    ../src/hosts/llm_host.cpp
)
if(APPLE)
    target_sources(test_mcp PRIVATE ../src/helpers/mac_mic_permissions.mm)
    target_link_libraries(test_mcp PRIVATE "-framework Foundation" "-framework AVFoundation")
endif()
target_link_libraries(test_mcp 
    PRIVATE 
    GTest::gtest 
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
    CURL::libcurl
    imgui::imgui
    tinyxml2
    SQLite::SQLite3
    ${SDL3_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
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
add_test(NAME BybitCurrencyFixTest COMMAND test_bybit_currency_fix)

# Example test for basic math operations
add_executable(test_example_math
    test_example_math.cpp
)
add_test(NAME ExampleMathTest COMMAND test_example_math)

add_executable(test_notes_repository_legacy
    test_notes_repository_legacy.cpp
)
target_link_libraries(test_notes_repository_legacy PRIVATE SQLite::SQLite3)
add_test(NAME NotesRepositoryLegacyTest COMMAND test_notes_repository_legacy)

add_executable(test_series_repository
    test_series_repository.cpp
)
target_link_libraries(test_series_repository PRIVATE SQLite::SQLite3)
add_test(NAME SeriesRepositoryTest COMMAND test_series_repository)

# Set output directory for test executables
set_target_properties(test_bybit_currency_fix test_example_math test_notes_repository_legacy test_series_repository test_rss_watermark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
    target_add_strict_warnings(test_bybit_currency_fix)
    target_add_strict_warnings(test_example_math)
    target_add_strict_warnings(test_notes_repository_legacy)
    target_add_strict_warnings(test_series_repository)
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
    COMMAND echo ""
    COMMAND echo "Legacy Console Tests:"
    COMMAND echo "---------------------"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_notes_repository_legacy
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_series_repository
    DEPENDS test_fetch_ssl test_ssl_ui test_math_operations test_weather_glaze test_adaptive_cards test_markdown_renderer test_rss_media_extraction test_rt_media test_rss_watermark test_gemini_adapter test_mcp test_bybit_currency_fix test_example_math test_notes_repository_legacy test_series_repository
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
    DEPENDS test_fetch_ssl test_ssl_ui test_math_operations test_weather_glaze test_adaptive_cards test_markdown_renderer test_rss_media_extraction test_rt_media test_rss_watermark test_gemini_adapter test_mcp
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
    DEPENDS test_fetch_ssl test_ssl_ui test_math_operations test_weather_glaze test_adaptive_cards test_markdown_renderer test_rss_media_extraction test_rt_media test_bybit_currency_fix test_example_math test_objectives
    COMMENT "Running tests through CTest framework"
)

# Add a clean target for tests
add_custom_target(clean_tests
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/test_*
    COMMENT "Cleaning test executables"
)
