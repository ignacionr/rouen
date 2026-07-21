cmake_minimum_required(VERSION 3.30)

# Set build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

# Debug-specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Add debug symbols and disable optimizations
  if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od")
  else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
  endif()
  
  # Enable address sanitizer for debug builds (optional)
  option(ENABLE_SANITIZER "Enable Address Sanitizer in Debug build" OFF)
  if(ENABLE_SANITIZER AND NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
  endif()
  
  # Add preprocessor definition for debug build
  add_compile_definitions(DEBUG_BUILD)
  
  message(STATUS "Debug build enabled with symbols")
else()
  # Release optimizations - handle differently for MSVC and GCC/Clang
  if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
  else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  endif()
  add_compile_definitions(NDEBUG)
  
  message(STATUS "Release build enabled with optimizations")
endif()

# Set compiler before project() call on Linux for better C++23 support.
# When CC/CXX are already set in the environment (e.g. Nix devShell, Docker),
# respect them so the environment-provided compiler and its include paths are used.
if(UNIX AND NOT APPLE)
  if("$ENV{CC}" STREQUAL "" AND "$ENV{CXX}" STREQUAL "")
    find_program(CLANG_EXECUTABLE clang)
    find_program(CLANGPP_EXECUTABLE clang++)

    if(CLANG_EXECUTABLE AND CLANGPP_EXECUTABLE)
      set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}")
      set(CMAKE_CXX_COMPILER "${CLANGPP_EXECUTABLE}")
      message(STATUS "Using Clang/Clang++ for Linux build")
    else()
      message(STATUS "Clang not found, using default system compiler")
    endif()
  else()
    message(STATUS "Using compiler from environment: $ENV{CXX}")
  endif()
endif()

# For MSVC, ensure no GCC/Clang flags leak in
if(MSVC)
  # Remove any GCC/Clang-specific flags that might be set by dependencies
  string(REPLACE "-O3" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "-g" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-g" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "-g" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# Project name and version
project(rouen VERSION 1.0 LANGUAGES C CXX)

# Retrieve the current git commit hash if not already provided (e.g. by Nix)
if(NOT COMPILE_GIT_HASH)
  find_program(GIT_EXECUTABLE git)
  if(GIT_EXECUTABLE)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      OUTPUT_VARIABLE COMPILE_GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()
  if(NOT COMPILE_GIT_HASH)
    set(COMPILE_GIT_HASH "unknown")
  endif()
endif()
add_compile_definitions(COMPILE_GIT_HASH="${COMPILE_GIT_HASH}")
message(STATUS "Build git commit hash: ${COMPILE_GIT_HASH}")

# Enable ccache if available to improve rebuild times
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "ccache enabled to speed up compilation")
endif()

# Platform-specific compiler settings
if(APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexperimental-library")
  endif()
elseif(UNIX AND NOT APPLE)
  # Linux-specific settings (compiler already set before project() call)
  message(STATUS "Linux configuration will be applied after dependencies")
endif()

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include precompiled header support
include(cmake/precompiled_headers.cmake)

# Enable export of compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include FetchContent module
include(FetchContent)

# Check if we're using vcpkg or system dependencies
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
  message(STATUS "Using vcpkg dependencies")
  
  # Find vcpkg dependencies with specific package names
  find_package(OpenGL REQUIRED)
  find_package(CURL REQUIRED)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(SDL3_image CONFIG REQUIRED)
  find_package(tinyxml2 CONFIG REQUIRED)
  find_package(glaze CONFIG REQUIRED)
  
  # Set variables for compatibility
  set(SQLite3_LIBRARIES unofficial::sqlite3::sqlite3)
  
else()
  message(STATUS "Using system dependencies")
  
  # Find system dependencies with standard package names
  find_package(OpenGL REQUIRED)
  find_package(CURL REQUIRED)
  find_package(SQLite3 REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(PkgConfig REQUIRED)
  
  # Use pkg-config for SDL3 on system builds
  pkg_check_modules(SDL3 REQUIRED sdl3)
  pkg_check_modules(SDL3_IMAGE REQUIRED sdl3-image)
  
  # Create imported targets for SDL3 if they don't exist
  if(SDL3_FOUND AND NOT TARGET SDL3::SDL3)
    list(GET SDL3_INCLUDE_DIRS 0 FIRST_SDL_INCLUDE)
    get_filename_component(SDL_INCLUDE_PARENT "${FIRST_SDL_INCLUDE}" DIRECTORY)
    add_library(SDL3::SDL3 INTERFACE IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIRS};${SDL_INCLUDE_PARENT}"
      INTERFACE_LINK_LIBRARIES "${SDL3_LIBRARIES}"
      INTERFACE_COMPILE_OPTIONS "${SDL3_CFLAGS_OTHER}"
    )
  endif()
  
  if(SDL3_IMAGE_FOUND AND NOT TARGET SDL3_image::SDL3_image)
    list(GET SDL3_IMAGE_INCLUDE_DIRS 0 FIRST_IMG_INCLUDE)
    get_filename_component(IMG_INCLUDE_PARENT "${FIRST_IMG_INCLUDE}" DIRECTORY)
    add_library(SDL3_image::SDL3_image INTERFACE IMPORTED)
    set_target_properties(SDL3_image::SDL3_image PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${SDL3_IMAGE_INCLUDE_DIRS};${IMG_INCLUDE_PARENT}"
      INTERFACE_LINK_LIBRARIES "${SDL3_IMAGE_LIBRARIES}"
      INTERFACE_COMPILE_OPTIONS "${SDL3_IMAGE_CFLAGS_OTHER}"
    )
  endif()
  
  # Try to find tinyxml2 via multiple methods
  # 1. Try CMake find_package first (most reliable)
  find_package(tinyxml2 QUIET)
  if(tinyxml2_FOUND OR TARGET tinyxml2::tinyxml2)
    set(TINYXML2_FOUND TRUE)
    message(STATUS "Found TinyXML2 via find_package")
  else()
    # 2. Try pkg-config with different names
    # Try tinyxml-2 first (modern TinyXML2 in Nix), then fallback to tinyxml2
    pkg_check_modules(TINYXML2 tinyxml-2)
    if(NOT TINYXML2_FOUND)
      pkg_check_modules(TINYXML2 tinyxml2)
    endif()
    
    if(NOT TINYXML2_FOUND)
      # 3. Manual search as last resort
      find_package(PkgConfig QUIET)
      find_path(TINYXML2_INCLUDE_DIRS tinyxml2.h
        PATHS /opt/homebrew/include /usr/local/include /usr/include)
      find_library(TINYXML2_LIBRARIES tinyxml2
        PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
      if(TINYXML2_INCLUDE_DIRS AND TINYXML2_LIBRARIES)
        set(TINYXML2_FOUND TRUE)
        message(STATUS "Found TinyXML2 via manual search")
      endif()
    else()
      message(STATUS "Found TinyXML2 via pkg-config")
    endif()
  endif()
  
  if(NOT TINYXML2_FOUND)
    if(APPLE)
      message(FATAL_ERROR "TinyXML2 not found. Please install with: brew install tinyxml2")
    elseif(UNIX)
      message(FATAL_ERROR "TinyXML2 not found. Please install with your package manager (e.g., apt install libtinyxml2-dev, yum install tinyxml2-devel, or use Nix)")
    else()
      message(FATAL_ERROR "TinyXML2 not found. Please install TinyXML2 development package")
    endif()
  endif()
  
  # For glaze, try to find it first, then fall back to FetchContent
  find_package(glaze CONFIG QUIET)
  if(NOT glaze_FOUND AND NOT TARGET glaze AND NOT TARGET glaze::glaze)
    # Try with different case (some systems use different naming)
    find_package(Glaze CONFIG QUIET)
    if(Glaze_FOUND OR TARGET glaze::glaze OR TARGET Glaze::Glaze)
      if(TARGET Glaze::Glaze AND NOT TARGET glaze::glaze)
        add_library(glaze::glaze ALIAS Glaze::Glaze)
        message(STATUS "Found Glaze via config and created glaze::glaze alias")
      endif()
    else()
      # If still not found, check if FETCHCONTENT_FULLY_DISCONNECTED is set (Nix build)
      if(FETCHCONTENT_FULLY_DISCONNECTED)
        message(FATAL_ERROR "Glaze not found via system packages and FetchContent is disabled (FETCHCONTENT_FULLY_DISCONNECTED=ON). Please ensure glaze is available in your build environment.")
      else()
        message(STATUS "Glaze not found via system packages. Using FetchContent as fallback...")
        
        # Use FetchContent to get Glaze - using an older compatible version
        FetchContent_Declare(
          glaze
          GIT_REPOSITORY https://github.com/stephenberry/glaze.git
          GIT_TAG v1.9.3
          GIT_SHALLOW TRUE
        )
        
        FetchContent_MakeAvailable(glaze)
        
        # Create glaze::glaze alias if it doesn't exist
        if(NOT TARGET glaze::glaze AND TARGET glaze)
          add_library(glaze::glaze ALIAS glaze)
          message(STATUS "Created glaze::glaze alias")
        endif()
      endif()
    endif()
  else()
    message(STATUS "Found glaze via find_package")
  endif()
  
  # Set variables for compatibility with vcpkg naming
  set(SQLite3_LIBRARIES SQLite::SQLite3)
endif()

# Handle ImGui - try system package first, then FetchContent
if(NOT TARGET imgui)
  # Debug: Check FETCHCONTENT_FULLY_DISCONNECTED early
  message(STATUS "FETCHCONTENT_FULLY_DISCONNECTED is: ${FETCHCONTENT_FULLY_DISCONNECTED}")
  
  # Try to find ImGui via system packages first (for Nix builds)
  find_package(imgui CONFIG QUIET)
  
  if(TARGET imgui::imgui OR TARGET imgui)
    message(STATUS "Found ImGui via find_package, using system package with local backends")
    
    # If found via find_package, create alias if needed
    if(TARGET imgui::imgui AND NOT TARGET imgui)
      add_library(imgui ALIAS imgui::imgui)
    endif()
    
    # Locate include path for system ImGui headers (required for clang-tidy support under Nix)
    find_path(IMGUI_INCLUDE_DIR NAMES imgui.h)
    if(IMGUI_INCLUDE_DIR)
      message(STATUS "Found ImGui headers at: ${IMGUI_INCLUDE_DIR}")
    else()
      message(WARNING "Could not find ImGui headers path explicitly via find_path")
    endif()
    
    message(STATUS "ImGui system package with local backends configured successfully")
  else()
    # System ImGui not found, try FetchContent
    if(FETCHCONTENT_FULLY_DISCONNECTED)
      message(FATAL_ERROR "ImGui requires FetchContent but FETCHCONTENT_FULLY_DISCONNECTED=ON. For Nix builds, ImGui should be provided via system packages or local sources.")
    else()
      message(STATUS "Using FetchContent for ImGui with full backend support")
      
      # Check if already declared to prevent redeclaration
      get_property(imgui_declared GLOBAL PROPERTY EP_STEPS_imgui)
      if(NOT imgui_declared)
        FetchContent_Declare(
          imgui
          GIT_REPOSITORY https://github.com/ocornut/imgui.git
          GIT_TAG v1.91.7
          GIT_SHALLOW TRUE
        )
        
        # Use modern FetchContent_MakeAvailable instead of deprecated Populate
        FetchContent_MakeAvailable(imgui)
      endif()
      
      message(STATUS "ImGui source dir: ${imgui_SOURCE_DIR}")

      # Create ImGui library using FetchContent with backends
      add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
      )

      target_include_directories(imgui PUBLIC 
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
      )
      
      message(STATUS "ImGui target created successfully via FetchContent")
    endif()
  endif()
else()
  message(STATUS "ImGui target already exists, skipping setup")
endif()

# Create backend library for system ImGui (when using Nix packages)
if(TARGET imgui::imgui AND NOT TARGET imgui_backends)
  add_library(imgui_backends STATIC
    ${CMAKE_SOURCE_DIR}/external/imgui_backends/imgui_impl_sdl3.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui_backends/imgui_impl_sdlgpu3.cpp
  )
  
  target_include_directories(imgui_backends PUBLIC 
    ${CMAKE_SOURCE_DIR}/external/imgui_backends
    ${IMGUI_INCLUDE_DIR}
  )
  
  target_link_libraries(imgui_backends PUBLIC 
    imgui::imgui
    SDL3::SDL3
  )
  message(STATUS "Created local backends library for system ImGui")
endif()

# Link ImGui with dependencies (only if it's our FetchContent target)
if(TARGET imgui AND NOT TARGET imgui::imgui)
  # This is our FetchContent imgui target, we can link to it
  if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    # vcpkg SDL3 targets
    target_link_libraries(imgui PUBLIC 
      $<IF:$<TARGET_EXISTS:SDL3::SDL3>,SDL3::SDL3,SDL3::SDL3-static>
    )
  else()
    # System SDL3 libraries
    target_link_libraries(imgui PUBLIC 
      SDL3::SDL3
    )
  endif()
endif()

# Also link system ImGui if using that target (but don't link to ALIAS targets)
if(TARGET imgui::imgui)
  # ImGui::imgui is likely an INTERFACE library, we don't need to link to it directly
  # as it will be linked transitively through the backends library
  message(STATUS "System ImGui found, using transitive linking")
endif()

# Set up ImColorTextEdit library using local files
add_library(imcolortextedit 
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit/TextEditor.cpp
)

# Link with appropriate ImGui target
if(TARGET imgui)
  # FetchContent ImGui
  target_include_directories(imcolortextedit PUBLIC 
    ${imgui_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
    ${CMAKE_SOURCE_DIR}/src/helpers
  )
  target_link_libraries(imcolortextedit PUBLIC imgui)
elseif(TARGET imgui::imgui)
  # System ImGui
  target_include_directories(imcolortextedit PUBLIC 
    ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
    ${CMAKE_SOURCE_DIR}/src/helpers
  )
  target_link_libraries(imcolortextedit PUBLIC imgui::imgui)
endif()

# Add mongoose as a static library (compiled as C)
add_library(mongoose STATIC ${CMAKE_SOURCE_DIR}/external/mongoose-7.15/mongoose.c)
set_target_properties(mongoose PROPERTIES LINKER_LANGUAGE C)
target_include_directories(mongoose PUBLIC ${CMAKE_SOURCE_DIR}/external/mongoose-7.15)
# Force C compilation for mongoose
set_source_files_properties(${CMAKE_SOURCE_DIR}/external/mongoose-7.15/mongoose.c PROPERTIES LANGUAGE C)

# Include modular cmake configuration files
include(cmake/warnings.cmake)

set(MAC_SOURCES)
if(APPLE)
  list(APPEND MAC_SOURCES src/helpers/mac_menu_helper.mm)
  set_source_files_properties(src/helpers/mac_menu_helper.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

# Add the executable
add_executable(${PROJECT_NAME} 
  src/rouen.cpp
  ${MAC_SOURCES}
  src/main_wnd_core.cpp
  src/main_wnd_init.cpp
  src/main_wnd_loop.cpp
  src/main_wnd_theme.cpp
  src/fonts.cpp
  src/helpers/capture_helper.cpp
  src/helpers/config_service.cpp
  src/helpers/llm_config.cpp
  src/helpers/mcp_service.cpp
  src/helpers/api_server.cpp
  src/helpers/theme_manager.cpp
  src/cards/interface/card.cpp
  src/cards/development/github_registrar.cpp
  src/cards/development/github/ci_card.cpp
  src/cards/productivity/jira_registrar.cpp
  src/cards/productivity/trello_registrar.cpp
  src/cards/productivity/converter.cpp
  src/cards/information/bybit_registrar.cpp
  src/cards/information/worldcup_registrar.cpp
  src/cards/information/movies_registrar.cpp
  src/models/jira_model_core.cpp
  src/models/jira_model_connection.cpp
  src/models/jira_model_profiles.cpp
  src/models/jira_model_projects.cpp
  src/models/jira_model_issues.cpp
  src/models/jira_model_api.cpp
  src/models/trello_model.cpp
  src/hosts/trello_host.cpp
  src/cards/productivity/trello_card_core.cpp
  src/cards/productivity/trello_card_connection.cpp
  src/cards/productivity/trello_card_boards.cpp
  src/cards/productivity/trello_card_cards.cpp
  src/cards/productivity/trello_card_ui.cpp
  src/cards/productivity/trello_card_async.cpp
  src/cards/media/chess_com_integration.cpp
  # RSS model refactor additions
  src/models/rss/feed_item.cpp
  src/models/rss/rss_date_parser.cpp
  src/models/rss/feed_xml_parser.cpp
  src/models/rss/rss_item_repo.cpp
  # Mail model refactor additions
  src/models/mail/metadata_serialization.cpp
  # Git model refactor additions
  src/models/git_process_helper.cpp
  src/models/git_scanner.cpp
  # Terminal refactor additions
  src/cards/system/terminal.cpp
  src/cards/system/terminal_output.cpp
  src/cards/system/terminal_bash.cpp
  src/cards/system/terminal_commands.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE 
  ${CMAKE_SOURCE_DIR}/external
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
  ${CMAKE_SOURCE_DIR}/external/mongoose-7.15
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/helpers
)

# Add ImGui include directories based on which target is available
if(TARGET imgui AND NOT TARGET imgui::imgui)
  # FetchContent ImGui
  target_include_directories(${PROJECT_NAME} PRIVATE 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )
elseif(TARGET imgui::imgui)
  # System ImGui - use local backends
  target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/external/imgui_backends
    ${IMGUI_INCLUDE_DIR}
  )
endif()

# Platform-specific include directories (conditional based on vcpkg vs system)
if(NOT (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg"))
  # Only add these for system builds - vcpkg handles includes automatically
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${TINYXML2_INCLUDE_DIRS}
    ${SDL3_INCLUDE_DIRS}
    ${SDL3_IMAGE_INCLUDE_DIRS}
  )
  if(SDL3_FOUND)
    foreach(inc_dir IN LISTS SDL3_INCLUDE_DIRS)
      get_filename_component(parent_dir "${inc_dir}" DIRECTORY)
      target_include_directories(${PROJECT_NAME} PRIVATE "${parent_dir}")
    endforeach()
  endif()
  if(SDL3_IMAGE_FOUND)
    foreach(inc_dir IN LISTS SDL3_IMAGE_INCLUDE_DIRS)
      get_filename_component(parent_dir "${inc_dir}" DIRECTORY)
      target_include_directories(${PROJECT_NAME} PRIVATE "${parent_dir}")
    endforeach()
  endif()
endif()

# Link with dependencies (conditional based on vcpkg vs system)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
  # vcpkg dependencies with specific target names
  target_link_libraries(${PROJECT_NAME} PRIVATE 
    imgui
    imcolortextedit
    glaze::glaze 
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    $<IF:$<TARGET_EXISTS:SDL3::SDL3>,SDL3::SDL3,SDL3::SDL3-static>
    $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image>,SDL3_image::SDL3_image,SDL3_image::SDL3_image-static>
    tinyxml2::tinyxml2
    unofficial::sqlite3::sqlite3
    mongoose
  )
else()
  # System dependencies with standard target names
  # Choose appropriate ImGui target and backends
  if(TARGET imgui)
    set(IMGUI_TARGET imgui)
  elseif(TARGET imgui::imgui)
    set(IMGUI_TARGET imgui::imgui)
  endif()
  
  target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${IMGUI_TARGET}
    imcolortextedit
    glaze::glaze 
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SQLite3_LIBRARIES}
    SDL3::SDL3
    SDL3_image::SDL3_image
    mongoose
  )
  
  # Always link with imgui_backends when using system ImGui
  if(TARGET imgui_backends)
    target_link_libraries(${PROJECT_NAME} PRIVATE imgui_backends)
  endif()
  
  # Handle TinyXML2 system dependencies
  if(TARGET tinyxml2::tinyxml2)
    # Modern cmake target from find_package
    target_link_libraries(${PROJECT_NAME} PRIVATE tinyxml2::tinyxml2)
  elseif(TINYXML2_FOUND)
    # pkg-config or manual detection
    target_include_directories(${PROJECT_NAME} PRIVATE ${TINYXML2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${TINYXML2_LIBRARIES})
  endif()
endif()

# ============================================================================
# PRECOMPILED HEADERS SETUP
# ============================================================================
# Apply precompiled headers to improve build performance
# This should come after all dependencies are linked but before platform config

setup_precompiled_headers(${PROJECT_NAME})
report_pch_info(${PROJECT_NAME})

# Optional: Disable PCH for files that might have issues
# Example: disable_pch_for_files(${PROJECT_NAME} src/some_problematic_file.cpp)

# Apply stricter warnings only to our code, not third-party libraries
# We use -Werror for our code but not for third-party libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR MSVC)
  target_add_strict_warnings(${PROJECT_NAME})
  message(STATUS "Applied strict warnings to local code for ${CMAKE_CXX_COMPILER_ID}")
endif()

# Platform-specific configuration
if(APPLE)
  include(cmake/macos.cmake)
elseif(UNIX AND NOT APPLE)
  include(cmake/linux.cmake)
elseif(WIN32)
  include(cmake/windows.cmake)
endif()

# Enable CTest only if tests are requested
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Tests disabled. Use -DBUILD_TESTS=ON to enable tests.")
endif()
