# Linux-specific configuration

# Find X11 and other Linux-specific dependencies
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

# Add Linux-specific compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=sign-conversion -Wno-error=double-promotion")

# Add Linux-specific debug settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Add debug symbols
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -ggdb")
  
  # Disable inlining in debug mode for better debugging experience
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-inline")
  
  # Set debugging helper macros
  add_compile_definitions(
    _GLIBCXX_DEBUG=1          # Enable debug mode for standard containers
    DEBUG_ROUEN=1             # Custom macro for conditional debug code
  )
  
  message(STATUS "Linux debug mode configured with enhanced symbols")
endif()

# Find and link with additional Linux libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
  ${X11_LIBRARIES}
  Threads::Threads
  ${CMAKE_DL_LIBS}
  ${OPENGL_LIBRARIES}
  GL  # Explicitly link with OpenGL
)

# Check for additional Linux dependencies
find_library(GLFW_LIBRARY glfw)
if(GLFW_LIBRARY)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${GLFW_LIBRARY})
  message(STATUS "Found GLFW: ${GLFW_LIBRARY}")
endif()

# Copy resources to build directory for easier access during development
set(RESOURCE_FILES
  "${CMAKE_SOURCE_DIR}/podcasts.txt"
  "${CMAKE_SOURCE_DIR}/presets.txt"
  "${CMAKE_SOURCE_DIR}/rouen.ini"
  "${CMAKE_SOURCE_DIR}/external/MaterialIcons-Regular.ttf"
)

# Copy resources to build directory
foreach(RES_FILE ${RESOURCE_FILES})
  get_filename_component(RES_FILENAME ${RES_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${RES_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${RES_FILENAME}"
    COMMENT "Copying ${RES_FILENAME} to build directory"
  )
endforeach()

# Create img directory and copy images
file(GLOB IMAGE_FILES "${CMAKE_SOURCE_DIR}/img/*.png" "${CMAKE_SOURCE_DIR}/img/*.jpg" "${CMAKE_SOURCE_DIR}/img/*.jpeg")
foreach(IMG_FILE ${IMAGE_FILES})
  get_filename_component(IMG_FILENAME ${IMG_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory 
            "${CMAKE_CURRENT_BINARY_DIR}/img"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${IMG_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/img/${IMG_FILENAME}"
    COMMENT "Copying ${IMG_FILENAME} to build directory"
  )
endforeach()

message(STATUS "Linux configuration completed")