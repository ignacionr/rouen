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