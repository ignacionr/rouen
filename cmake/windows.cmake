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
    )
    
    # Handle DLL copying for vcpkg dependencies
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        # Set up post-build steps to copy DLLs
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(CONFIG_SUFFIX "d")
        else()
            set(CONFIG_SUFFIX "")
        endif()
        
        # Ensure VCPKG_TARGET_TRIPLET is set
        if(NOT DEFINED VCPKG_TARGET_TRIPLET)
            set(VCPKG_TARGET_TRIPLET "x64-windows")
        endif()
        
        # Determine vcpkg installed directory
        if(DEFINED VCPKG_INSTALLED_DIR)
            set(VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        else()
            # Fallback paths for vcpkg installation
            set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
        
        message(STATUS "Windows DLL copying configured:")
        message(STATUS "  VCPKG_TARGET_TRIPLET: ${VCPKG_TARGET_TRIPLET}")
        message(STATUS "  VCPKG_BIN_DIR: ${VCPKG_BIN_DIR}")
        message(STATUS "  CONFIG_SUFFIX: '${CONFIG_SUFFIX}'")
        
        # Copy SDL2 DLLs
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_BIN_DIR}/SDL2${CONFIG_SUFFIX}.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL2${CONFIG_SUFFIX}.dll to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
        
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_BIN_DIR}/SDL2_image${CONFIG_SUFFIX}.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL2_image${CONFIG_SUFFIX}.dll to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
        
        # Copy other required DLLs (critical for functionality)
        set(REQUIRED_DLLS
            "libcurl${CONFIG_SUFFIX}.dll"
            "libcurl.dll"
            "tinyxml2${CONFIG_SUFFIX}.dll"
            "libssl-3-x64.dll"
            "libcrypto-3-x64.dll"
            "zlib1.dll"
        )
        
        foreach(DLL ${REQUIRED_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_BIN_DIR}/${DLL}"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying ${DLL} to $<TARGET_FILE_DIR:${PROJECT_NAME}> (if exists)"
                # Don't fail if DLL doesn't exist (some may be debug/release specific)
            )
        endforeach()
        
        # Also try to copy from debug bin if in debug mode
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(VCPKG_DEBUG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/debug/bin")
            message(STATUS "  VCPKG_DEBUG_BIN_DIR: ${VCPKG_DEBUG_BIN_DIR}")
            
            foreach(DLL ${REQUIRED_DLLS})
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_DEBUG_BIN_DIR}/${DLL}"
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                    COMMENT "Copying ${DLL} from debug bin to $<TARGET_FILE_DIR:${PROJECT_NAME}> (if exists)"
                    # Don't fail if DLL doesn't exist
                )
            endforeach()
        endif()
        
        # Add a final verification step
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "DLL copying completed for ${PROJECT_NAME}"
            COMMAND ${CMAKE_COMMAND} -E echo "Executable directory: $<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
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
endif()
