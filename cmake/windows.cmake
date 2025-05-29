# Windows-specific configuration

if(WIN32)
    message(STATUS "Configuring for Windows build")
    
    # Set Windows subsystem for GUI application
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
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
    if(VCPKG_TOOLCHAIN)
        # Set up post-build steps to copy DLLs
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(CONFIG_SUFFIX "d")
        else()
            set(CONFIG_SUFFIX "")
        endif()
        
        # Copy SDL2 DLLs
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/SDL2${CONFIG_SUFFIX}.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
        
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/SDL2_image${CONFIG_SUFFIX}.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
        
        # Copy other required DLLs
        set(REQUIRED_DLLS
            "libcurl${CONFIG_SUFFIX}.dll"
            "tinyxml2${CONFIG_SUFFIX}.dll"
            "libssl-3-x64.dll"
            "libcrypto-3-x64.dll"
            "zlib1.dll"
        )
        
        foreach(DLL ${REQUIRED_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/${DLL}"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMAND_EXPAND_LISTS
            )
        endforeach()
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
