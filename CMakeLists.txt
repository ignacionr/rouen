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

# Platform-specific compiler settings
if(APPLE)
  # Use system clang for better compatibility with macOS headers
  # set(CMAKE_C_COMPILER "/opt/homebrew/opt/llvm/bin/clang")
  # set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++")
elseif(UNIX AND NOT APPLE)
  # Use clang on Linux if available
  find_program(CLANG_EXECUTABLE clang)
  find_program(CLANGPP_EXECUTABLE clang++)
  
  if(CLANG_EXECUTABLE AND CLANGPP_EXECUTABLE)
    set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}")
    set(CMAKE_CXX_COMPILER "${CLANGPP_EXECUTABLE}")
    message(STATUS "Using Clang/Clang++ for Linux build")
  else()
    message(STATUS "Clang not found, using default system compiler")
  endif()
  
  # Configure the generator to use Ninja if available
  find_program(NINJA_EXECUTABLE ninja)
  if(NINJA_EXECUTABLE)
    set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "")
    message(STATUS "Using Ninja build system")
  endif()
endif()

# Project name and version
project(rouen VERSION 1.0 LANGUAGES CXX)

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable export of compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include modular cmake configuration files
include(cmake/warnings.cmake)
include(cmake/dependencies.cmake)

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

# Platform-specific include directories
if(WIN32)
  target_include_directories(${PROJECT_NAME} PRIVATE
    # Windows paths will be handled by vcpkg automatically
  )
elseif(UNIX)
  target_include_directories(${PROJECT_NAME} PRIVATE
    /usr/include/stb
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${TINYXML2_INCLUDE_DIRS}
  )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE 
  imgui
  imcolortextedit
  glaze::glaze 
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
)

# Platform-specific library linking
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
  # Using vcpkg - use modern CMake targets
  target_link_libraries(${PROJECT_NAME} PRIVATE
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    tinyxml2::tinyxml2
    unofficial::sqlite3::sqlite3
  )
elseif(WIN32)
  # Windows without vcpkg
  message(FATAL_ERROR "Windows builds require vcpkg")
elseif(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${TINYXML2_LIBRARIES}
    ${SQLite3_LIBRARIES}
  )
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${TINYXML2_LIBRARIES}
    ${SQLite3_LIBRARIES}
  )
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

# Enable CTest
enable_testing()
add_subdirectory(tests)
