# Linux-specific configuration for system dependencies

# Find Linux-specific dependencies 
# X11 is required for SDL2 on Linux systems
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

# Ensure all warnings are treated as errors for local target code

# Add Linux-specific debug settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Add debug symbols
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -ggdb")
  
  # Disable inlining in debug mode for better debugging experience
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-inline")
  
  # Set debugging helper macros - but avoid _GLIBCXX_DEBUG which conflicts with Glaze constexpr evaluation
  add_compile_definitions(
    DEBUG_ROUEN=1             # Custom macro for conditional debug code
  )
  
  # Note: _GLIBCXX_DEBUG=1 disabled to avoid constexpr conflicts with Glaze library
  message(STATUS "Linux debug mode configured with enhanced symbols (without _GLIBCXX_DEBUG)")
endif()

# Link with additional Linux libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
  ${X11_LIBRARIES}
  Threads::Threads
  ${CMAKE_DL_LIBS}
  ${OPENGL_LIBRARIES}
  GL  # Explicitly link with OpenGL
)

# Copy resources to build directory for easier access during development
set(RESOURCE_FILES
  "${CMAKE_SOURCE_DIR}/podcasts.txt"
  "${CMAKE_SOURCE_DIR}/presets.txt"
  "${CMAKE_SOURCE_DIR}/external/MaterialIcons-Regular.ttf"
  "${CMAKE_SOURCE_DIR}/external/fonts/NotoSansSymbols-Regular.ttf"
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

# Copy Adaptive Card test presets
file(GLOB ADAPTIVE_CARD_RESOURCE_FILES "${CMAKE_SOURCE_DIR}/resources/adaptive_cards/*.json")
foreach(RES_FILE ${ADAPTIVE_CARD_RESOURCE_FILES})
  get_filename_component(RES_FILENAME ${RES_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/adaptive_cards"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${RES_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/adaptive_cards/${RES_FILENAME}"
    COMMENT "Copying ${RES_FILENAME} to build directory adaptive_cards"
  )
endforeach()

# Create img directory and copy images and audio files
file(GLOB IMAGE_FILES "${CMAKE_SOURCE_DIR}/img/*.png" "${CMAKE_SOURCE_DIR}/img/*.jpg" "${CMAKE_SOURCE_DIR}/img/*.jpeg")
file(GLOB AUDIO_FILES "${CMAKE_SOURCE_DIR}/img/*.mp3" "${CMAKE_SOURCE_DIR}/img/*.wav")
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

foreach(AUDIO_FILE ${AUDIO_FILES})
  get_filename_component(AUDIO_FILENAME ${AUDIO_FILE} NAME)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory 
            "${CMAKE_CURRENT_BINARY_DIR}/img"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${AUDIO_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/img/${AUDIO_FILENAME}"
    COMMENT "Copying ${AUDIO_FILENAME} to build directory"
  )
endforeach()

message(STATUS "Linux configuration completed")
