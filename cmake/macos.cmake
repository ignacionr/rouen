# macOS-specific configuration

# Disable problematic warnings as errors for macOS builds
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=sign-conversion -Wno-error=double-promotion -Wno-error=implicit-fallthrough -Wno-error=nontrivial-memcall -Wno-error=implicit-int-float-conversion")

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

# Configure GLFW specifically for macOS
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
  find_library(GLFW_LIBRARY
    NAMES glfw glfw3
    PATHS /opt/homebrew/lib /usr/local/lib
    REQUIRED
  )
  message(STATUS "Found GLFW: ${GLFW_LIBRARY}")
endif()

# Add GL library path directly for macOS
find_library(OPENGL_LIBRARY OpenGL REQUIRED)

# Update target link libraries for macOS
target_link_libraries(${PROJECT_NAME} PRIVATE 
  ${COCOA_LIBRARY}
  ${IOKIT_LIBRARY}
  ${COREVIDEO_LIBRARY}
  "-framework OpenGL"
  ${GLFW_LIBRARY}
  ${SDL2_LIBRARIES}
  ${SDL2_IMAGE_LIBRARIES}
  ${OPENGL_LIBRARY}
)

# Remove problematic libraries that are handled differently on macOS
get_target_property(CURRENT_LINK_LIBRARIES ${PROJECT_NAME} LINK_LIBRARIES)
if(CURRENT_LINK_LIBRARIES)
  list(REMOVE_ITEM CURRENT_LINK_LIBRARIES "glfw" "GL")
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

# Create application icon from PNG (if PNG exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/img/Rouen.png")
  message(STATUS "Found application icon PNG, creating .icns file")
  
  # Create Resources directory in the build dir for icon creation
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset")
  
  # Convert PNG to ICNS using sips and iconutil
  # This creates multiple resolution icon files from the original PNG
  set(ICON_SIZES "16;32;64;128;256;512;1024")
  foreach(SIZE IN LISTS ICON_SIZES)
    # Calculate double resolution for retina
    math(EXPR SIZE2X "${SIZE} * 2")
    
    # Create standard and retina (2x) versions
    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_${SIZE}x${SIZE}.png"
      COMMAND /usr/bin/sips -z ${SIZE} ${SIZE} "${CMAKE_SOURCE_DIR}/img/Rouen.png" 
              --out "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_${SIZE}x${SIZE}.png"
      DEPENDS "${CMAKE_SOURCE_DIR}/img/Rouen.png"
      COMMENT "Creating ${SIZE}x${SIZE} icon for app bundle"
    )
    
    # Create 2x versions (for retina)
    if(NOT SIZE EQUAL 1024)
      add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_${SIZE}x${SIZE}@2x.png"
        COMMAND /usr/bin/sips -z ${SIZE2X} ${SIZE2X} "${CMAKE_SOURCE_DIR}/img/Rouen.png" 
                --out "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_${SIZE}x${SIZE}@2x.png"
        DEPENDS "${CMAKE_SOURCE_DIR}/img/Rouen.png"
        COMMENT "Creating ${SIZE}x${SIZE}@2x icon for app bundle"
      )
    endif()
  endforeach()
  
  # Convert the iconset to ICNS
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns"
    COMMAND /usr/bin/iconutil -c icns "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset" 
            -o "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_16x16.png"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_32x32.png"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_64x64.png"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_128x128.png"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_256x256.png"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_512x512.png"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.iconset/icon_1024x1024.png"
    COMMENT "Generating Rouen.icns from iconset"
  )
  
  # Add the icon to the resources
  set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
  )
  
  # Add the icon file to the target sources
  target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Rouen.icns")
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
  "${CMAKE_SOURCE_DIR}/rouen.ini"
  "${CMAKE_SOURCE_DIR}/external/MaterialIcons-Regular.ttf"
  "${CMAKE_SOURCE_DIR}/external/fonts/NotoSansSymbols-Regular.ttf"
  "${CMAKE_SOURCE_DIR}/img/alarm.mp3"
)

# Copy resources into the bundle
foreach(RES_FILE ${RESOURCE_FILES})
  get_filename_component(RES_FILENAME ${RES_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${RES_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/${RES_FILENAME}"
    COMMENT "Copying ${RES_FILENAME} to app bundle Resources"
  )
endforeach()

# Create Resources/img directory and copy images
file(GLOB IMAGE_FILES "${CMAKE_SOURCE_DIR}/img/*.png" "${CMAKE_SOURCE_DIR}/img/*.jpg" "${CMAKE_SOURCE_DIR}/img/*.jpeg")
# Create the destination directory first (just once)
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory 
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Resources/img"
  COMMENT "Creating Resources/img directory in app bundle"
)
# Copy each file, but check if it exists first
foreach(IMG_FILE ${IMAGE_FILES})
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

message(STATUS "macOS application bundle configuration completed")