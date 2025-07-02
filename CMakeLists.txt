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

# Set compiler before project() call on Linux for better C++23 support
if(UNIX AND NOT APPLE)
  find_program(CLANG_EXECUTABLE clang)
  find_program(CLANGPP_EXECUTABLE clang++)
  
  if(CLANG_EXECUTABLE AND CLANGPP_EXECUTABLE)
    set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}")
    set(CMAKE_CXX_COMPILER "${CLANGPP_EXECUTABLE}")
    message(STATUS "Using Clang/Clang++ for Linux build")
  else()
    message(STATUS "Clang not found, using default system compiler")
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
project(rouen VERSION 1.0 LANGUAGES CXX)

# Platform-specific compiler settings
if(APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
elseif(UNIX AND NOT APPLE)
  # Linux-specific settings (compiler already set before project() call)
  message(STATUS "Linux configuration will be applied after dependencies")
endif()

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  find_package(SDL2 CONFIG REQUIRED)
  find_package(SDL2_image CONFIG REQUIRED)
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
  
  # Use pkg-config for SDL2 on system builds
  pkg_check_modules(SDL2 REQUIRED sdl2)
  pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
  
  # Create imported targets for SDL2 if they don't exist
  if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
      INTERFACE_COMPILE_OPTIONS "${SDL2_CFLAGS_OTHER}"
    )
  endif()
  
  if(SDL2_IMAGE_FOUND AND NOT TARGET SDL2_image::SDL2_image)
    add_library(SDL2_image::SDL2_image INTERFACE IMPORTED)
    set_target_properties(SDL2_image::SDL2_image PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_IMAGE_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${SDL2_IMAGE_LIBRARIES}"
      INTERFACE_COMPILE_OPTIONS "${SDL2_IMAGE_CFLAGS_OTHER}"
    )
  endif()
  
  # Try to find tinyxml2 via pkg-config first, then standard find
  pkg_check_modules(TINYXML2 tinyxml2)
  if(NOT TINYXML2_FOUND)
    find_package(PkgConfig QUIET)
    find_path(TINYXML2_INCLUDE_DIRS tinyxml2.h
      PATHS /opt/homebrew/include /usr/local/include /usr/include)
    find_library(TINYXML2_LIBRARIES tinyxml2
      PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
    if(TINYXML2_INCLUDE_DIRS AND TINYXML2_LIBRARIES)
      set(TINYXML2_FOUND TRUE)
    endif()
  endif()
  
  if(NOT TINYXML2_FOUND)
    message(FATAL_ERROR "TinyXML2 not found. Please install with: brew install tinyxml2")
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

# Handle ImGui - always use FetchContent with proper disconnected mode check
if(NOT TARGET imgui)
  # Debug: Check FETCHCONTENT_FULLY_DISCONNECTED early
  message(STATUS "FETCHCONTENT_FULLY_DISCONNECTED is: ${FETCHCONTENT_FULLY_DISCONNECTED}")
  
  # Check if FETCHCONTENT_FULLY_DISCONNECTED is set (Nix build)
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
        GIT_TAG v1.89.9
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
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    )

    target_include_directories(imgui PUBLIC 
      ${imgui_SOURCE_DIR}
      ${imgui_SOURCE_DIR}/backends
    )
    
    message(STATUS "ImGui target created successfully via FetchContent")
  endif()
else()
  message(STATUS "ImGui target already exists, skipping setup")
endif()

# Link ImGui with dependencies (conditional based on vcpkg vs system)
if(TARGET imgui)
  if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    # vcpkg SDL2 targets
    target_link_libraries(imgui PUBLIC 
      $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
      ${OPENGL_LIBRARIES}
    )
  else()
    # System SDL2 libraries - always FetchContent ImGui with built-in backends
    target_link_libraries(imgui PUBLIC 
      SDL2::SDL2
      ${OPENGL_LIBRARIES}
    )
  endif()
endif()

# Set up ImColorTextEdit library using local files
add_library(imcolortextedit 
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit/TextEditor.cpp
)

# Always use FetchContent ImGui
target_include_directories(imcolortextedit PUBLIC 
  ${imgui_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
  ${CMAKE_SOURCE_DIR}/src/helpers
)
target_link_libraries(imcolortextedit PUBLIC imgui)

# Include modular cmake configuration files
include(cmake/warnings.cmake)

# Add the executable
add_executable(${PROJECT_NAME} 
  src/rouen.cpp
  src/main_wnd.cpp
  src/fonts.cpp
  src/helpers/capture_helper.cpp
  src/helpers/config_service.cpp
  src/cards/development/github_registrar.cpp
  src/cards/productivity/jira_registrar.cpp
  src/cards/information/bybit_registrar.cpp
  src/models/jira_model.cpp
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
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${CMAKE_SOURCE_DIR}/external/imguicolortextedit
)

# Platform-specific include directories (conditional based on vcpkg vs system)
if(NOT (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg"))
  # Only add these for system builds - vcpkg handles includes automatically
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${TINYXML2_INCLUDE_DIRS}
  )
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
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    tinyxml2::tinyxml2
    unofficial::sqlite3::sqlite3
  )
else()
  # System dependencies with standard target names
  target_link_libraries(${PROJECT_NAME} PRIVATE 
    $<IF:$<TARGET_EXISTS:imgui_complete>,imgui_complete,imgui>
    imcolortextedit
    glaze::glaze 
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SQLite3_LIBRARIES}
    SDL2::SDL2
    SDL2_image::SDL2_image
  )
  
  # Handle TinyXML2 system dependencies
  if(TINYXML2_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${TINYXML2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${TINYXML2_LIBRARIES})
  endif()
endif()

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
