# Windows-specific configuration

if(WIN32)
    message(STATUS "Configuring for Windows build")
    
    # Set Windows subsystem for GUI application
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    # Add Windows resource file for icon and version info
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/rouen.rc")
        target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/resources/rouen.rc")
        message(STATUS "Windows resource file added: ${CMAKE_SOURCE_DIR}/resources/rouen.rc")
    else()
        message(WARNING "Windows resource file not found: ${CMAKE_SOURCE_DIR}/resources/rouen.rc")
    endif()
    
    # Windows-specific compile definitions
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    
    # Windows-specific compiler flags
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            /W4  # High warning level
            /permissive-  # Disable non-conforming code
            /utf-8  # Use UTF-8 for source and execution character sets
            /bigobj  # Allow large object files (needed for large translation units)
            /wd4267  # Suppress 'conversion from size_t to int' warnings
            /wd4244  # Suppress 'conversion from double to float' warnings
        )
        
        # Enable parallel compilation
        target_compile_options(${PROJECT_NAME} PRIVATE /MP)
        
        # Set runtime library to dynamic
        set_property(TARGET ${PROJECT_NAME} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
    endif()
    
    # Link Windows-specific libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        user32
        gdi32
        shell32
        ole32
        oleaut32
        imm32
        winmm
        version
        setupapi
        psapi       # For memory and process information
        kernel32    # For system time and process APIs
        uiautomationcore
    )
    
    # Handle DLL copying for vcpkg dependencies
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        if(COMMAND vcpkg_copy_dependencies)
            vcpkg_copy_dependencies(TARGET ${PROJECT_NAME})
        endif()
    endif()
    
    # Windows-specific asset handling
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/img
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/img
    )
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/external/fonts
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts
    )
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/external/MaterialIcons-Regular.ttf
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/MaterialIcons-Regular.ttf
    )
    
    # Copy configuration files
    set(CONFIG_FILES
        "presets.txt"
        "podcasts.txt"
    )
    
    foreach(CONFIG_FILE ${CONFIG_FILES})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/${CONFIG_FILE}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/${CONFIG_FILE}
        )
    endforeach()

    # Copy Adaptive Card test presets
    file(GLOB ADAPTIVE_CARD_RESOURCE_FILES "${CMAKE_SOURCE_DIR}/resources/adaptive_cards/*.json")
    foreach(RES_FILE ${ADAPTIVE_CARD_RESOURCE_FILES})
        get_filename_component(RES_FILENAME ${RES_FILE} NAME)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/adaptive_cards
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${RES_FILE}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/adaptive_cards/${RES_FILENAME}
            COMMENT "Copying ${RES_FILENAME} to adaptive_cards folder"
        )
    endforeach()
endif()
