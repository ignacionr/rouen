# macOS-specific configuration

# Set minimum macOS deployment target to support C++23 std::format
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.4")
message(STATUS "Setting macOS deployment target to ${CMAKE_OSX_DEPLOYMENT_TARGET} for C++23 std::format support and latest macOS compatibility")

# Ensure we're using the latest C++ standard library with proper std::format support
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

# Disable problematic warnings as errors for macOS builds
# Note: -Wno-error=nontrivial-memcall is not supported by Apple Clang, removing it
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=sign-conversion -Wno-error=double-promotion -Wno-error=implicit-fallthrough -Wno-error=implicit-int-float-conversion")

# When cross-compiling (e.g. -DCMAKE_OSX_ARCHITECTURES=arm64 on CI), Clang emits
# -Wpoison-system-directories for host paths such as /usr/local/include that end up
# in the search list via vcpkg or system package discovery.  Those libraries are
# already built for the correct target by vcpkg, so the warning is a false-positive
# in this setup.  We suppress it via target_compile_options (not CMAKE_CXX_FLAGS) so
# that it is appended AFTER the per-target -Weverything flag and therefore takes effect.
# NOTE: this file is always included from CMakeLists.txt after add_executable(), so
# ${PROJECT_NAME} is guaranteed to refer to an existing target.
if(TARGET ${PROJECT_NAME})
  target_compile_options(${PROJECT_NAME} PRIVATE -Wno-poison-system-directories)
endif()

# Add macOS-specific debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Enable DWARF with dSYM file generation
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-4")
  
  # Disable inlining in debug mode for better debugging experience
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-inline")
  
  # Set LLDB debugging helper macros
  add_compile_definitions(
    _LIBCPP_DEBUG=1           # Enable libcxx debug mode for container checks
    _GLIBCXX_DEBUG=1          # Enable debug mode for standard containers
    DEBUG_ROUEN=1             # Custom macro for conditional debug code
  )
  
  message(STATUS "macOS debug mode configured with enhanced symbols")
endif()

# Handle ARM64 (Apple Silicon) architecture specifically
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  message(STATUS "Configuring for Apple Silicon (ARM64)")
  
  # Add Homebrew paths for Apple Silicon
  include_directories(SYSTEM /opt/homebrew/include)
  link_directories(/opt/homebrew/lib)
endif()

# Special handling for GL on macOS
target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)

# Find required macOS frameworks
find_library(COCOA_LIBRARY Cocoa REQUIRED)
find_library(IOKIT_LIBRARY IOKit REQUIRED)
find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
find_library(COREHAPTICS_LIBRARY CoreHaptics REQUIRED)
find_library(GAMECONTROLLER_LIBRARY GameController REQUIRED)
find_library(METAL_LIBRARY Metal REQUIRED)
find_library(FORCEFEEDBACK_LIBRARY ForceFeedback REQUIRED)
find_library(CARBON_LIBRARY Carbon REQUIRED)

# Add GL library path directly for macOS
find_library(OPENGL_LIBRARY OpenGL REQUIRED)

# Update target link libraries for macOS
target_link_libraries(${PROJECT_NAME} PRIVATE 
  ${COCOA_LIBRARY}
  ${IOKIT_LIBRARY}
  ${COREVIDEO_LIBRARY}
  ${AUDIOTOOLBOX_LIBRARY}
  ${COREHAPTICS_LIBRARY}
  ${GAMECONTROLLER_LIBRARY}
  ${METAL_LIBRARY}
  ${FORCEFEEDBACK_LIBRARY}
  ${CARBON_LIBRARY}
  "-framework OpenGL"
  ${SDL2_LIBRARIES}
  ${SDL2_IMAGE_LIBRARIES}
  ${OPENGL_LIBRARY}
)

# Remove problematic libraries that are handled differently on macOS
get_target_property(CURRENT_LINK_LIBRARIES ${PROJECT_NAME} LINK_LIBRARIES)
if(CURRENT_LINK_LIBRARIES)
  list(REMOVE_ITEM CURRENT_LINK_LIBRARIES "GL")
  set_target_properties(${PROJECT_NAME} PROPERTIES LINK_LIBRARIES "${CURRENT_LINK_LIBRARIES}")
endif()

# -----------------------------------------------------------------------------
# macOS Application Bundle Configuration
# -----------------------------------------------------------------------------

# Set up the application as a proper macOS bundle
set_target_properties(${PROJECT_NAME} PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.rouen.app"
  MACOSX_BUNDLE_BUNDLE_NAME "Rouen"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 Rouen Contributors"
  MACOSX_BUNDLE_INFO_STRING "RSS and Productivity Dashboard"
  MACOSX_BUNDLE_ICON_FILE "Rouen.icns"
)

# Set application bundle resources directory
set(MACOSX_BUNDLE_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources")

# Create application icon from PNG (if PNG exists and not skipped via environment)
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/Rouen.icns")
  message(STATUS "Using pre-generated Rouen.icns icon")
  
  # Copy the pre-generated ICNS file to build directory
  configure_file(
    "${CMAKE_SOURCE_DIR}/resources/icons/Rouen.icns"
    "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns"
    COPYONLY
  )
  
  # Add the icon to the resources
  set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
  )
  
  # Add the icon file to the target sources
  target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns")
else()
  message(WARNING "No pre-generated application icon found at resources/icons/Rouen.icns")
endif()

# Create custom Info.plist file
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
  @ONLY
)
set_target_properties(${PROJECT_NAME} PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
)

# Bundle resource files in the application package
set(RESOURCE_FILES
  "${CMAKE_SOURCE_DIR}/podcasts.txt"
  "${CMAKE_SOURCE_DIR}/presets.txt"
  "${CMAKE_SOURCE_DIR}/external/MaterialIcons-Regular.ttf"
  "${CMAKE_SOURCE_DIR}/external/fonts/NotoSansSymbols-Regular.ttf"
  "${CMAKE_SOURCE_DIR}/scripts/fetch_calendar.scpt"
  "${CMAKE_SOURCE_DIR}/scripts/fetch_calendar.swift"
  "${CMAKE_SOURCE_DIR}/scripts/create_event.scpt"
)

# Create Resources directory first
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory 
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources"
  COMMENT "Creating Resources directory in app bundle"
)

# Copy resources into the bundle
foreach(RES_FILE ${RESOURCE_FILES})
  get_filename_component(RES_FILENAME ${RES_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RES_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/${RES_FILENAME}"
    COMMENT "Copying ${RES_FILENAME} to app bundle Resources"
  )
endforeach()

# Copy Adaptive Card test presets
file(GLOB ADAPTIVE_CARD_RESOURCE_FILES "${CMAKE_SOURCE_DIR}/resources/adaptive_cards/*.json")
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/adaptive_cards"
  COMMENT "Creating Resources/adaptive_cards directory in app bundle"
)
foreach(RES_FILE ${ADAPTIVE_CARD_RESOURCE_FILES})
  get_filename_component(RES_FILENAME ${RES_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RES_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/adaptive_cards/${RES_FILENAME}"
    COMMENT "Copying ${RES_FILENAME} to app bundle Resources/adaptive_cards"
  )
endforeach()

# Create Resources/img directory and copy images and audio files
file(GLOB IMG_FILES "${CMAKE_SOURCE_DIR}/img/*.png" "${CMAKE_SOURCE_DIR}/img/*.jpg" "${CMAKE_SOURCE_DIR}/img/*.jpeg" "${CMAKE_SOURCE_DIR}/img/*.mp3" "${CMAKE_SOURCE_DIR}/img/*.wav")
# Create the destination directory first (just once)
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory 
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/img"
  COMMENT "Creating Resources/img directory in app bundle"
)
# Copy each file, but check if it exists first
foreach(IMG_FILE ${IMG_FILES})
  get_filename_component(IMG_FILENAME ${IMG_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IMG_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/img/${IMG_FILENAME}"
    COMMENT "Copying ${IMG_FILENAME} to app bundle Resources/img"
  )
endforeach()

# Add option for code signing
option(ENABLE_CODESIGN "Enable code signing of the application bundle" OFF)
if(ENABLE_CODESIGN)
  # The developer ID must be set in a CMake variable or passed via command line
  if(NOT DEFINED DEVELOPER_ID)
    message(WARNING "DEVELOPER_ID not set. If you enable code signing, please set -DDEVELOPER_ID=\"Developer ID Application: Your Name (TEAMID)\"")
  else()
    add_custom_command(
      TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND codesign --force --deep --sign "${DEVELOPER_ID}" 
              "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app"
      COMMENT "Code signing application with identity: ${DEVELOPER_ID}"
    )
    message(STATUS "Code signing enabled with identity: ${DEVELOPER_ID}")
  endif()
endif()

# Add a custom target to create a DMG installer
add_custom_target(dmg
  COMMAND hdiutil create -volname "Rouen" -srcfolder 
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app" 
          -ov -format UDZO "${CMAKE_CURRENT_BINARY_DIR}/Rouen-${PROJECT_VERSION}.dmg"
  DEPENDS ${PROJECT_NAME}
  COMMENT "Creating DMG installer for Rouen"
)

# Add installation rule to copy app to /Applications folder
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app"
        DESTINATION "/Applications"
        USE_SOURCE_PERMISSIONS
        COMPONENT Runtime)

message(STATUS "macOS application bundle configuration completed")
