# Precompiled Headers Configuration for Rouen
# This file provides portable PCH support across platforms and compilers

# Check if the compiler supports precompiled headers
function(check_pch_support TARGET_NAME)
    # CMake 3.16+ has built-in PCH support
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
        set(PCH_SUPPORTED TRUE PARENT_SCOPE)
        message(STATUS "Using CMake built-in precompiled header support")
    else()
        set(PCH_SUPPORTED FALSE PARENT_SCOPE)
        message(WARNING "CMake version ${CMAKE_VERSION} doesn't support built-in PCH. Requires 3.16+")
    endif()
endfunction()

# Configure precompiled headers for the target
function(setup_precompiled_headers TARGET_NAME)
    check_pch_support(${TARGET_NAME})
    
    if(DISABLE_PCH OR NOT PCH_SUPPORTED)
        message(STATUS "Skipping PCH setup for ${TARGET_NAME} - disabled or not supported")
        return()
    endif()

    # Define common system headers (Tier 1 - Most stable, rarely change)
    set(SYSTEM_PCH_HEADERS
        # Standard C++ Library headers (most commonly used)
        <algorithm>
        <chrono>
        <filesystem>
        <format>
        <fstream>
        <functional>
        <iostream>
        <memory>
        <mutex>
        <optional>
        <regex>
        <string>
        <string_view>
        <thread>
        <unordered_map>
        <vector>
    )

    # Define library headers (Tier 2 - External dependencies, change occasionally)
    set(LIBRARY_PCH_HEADERS
        # SDL headers (used throughout the application)
        <SDL.h>
        
        # Project-specific wrapper headers (use relative paths from src/)
        "src/helpers/imgui_include.hpp"
        "src/helpers/glaze_include.hpp"
    )

    # Apply precompiled headers based on build configuration
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # In debug builds, use conservative PCH to avoid masking issues
        target_precompile_headers(${TARGET_NAME} PRIVATE ${SYSTEM_PCH_HEADERS})
        message(STATUS "Applied system-only PCH for debug build of ${TARGET_NAME}")
    else()
        # In release builds, use aggressive PCH for maximum speed
        target_precompile_headers(${TARGET_NAME} PRIVATE 
            ${SYSTEM_PCH_HEADERS}
            ${LIBRARY_PCH_HEADERS}
        )
        message(STATUS "Applied full PCH for release build of ${TARGET_NAME}")
    endif()

    # Compiler-specific optimizations
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang-specific PCH optimizations
        target_compile_options(${TARGET_NAME} PRIVATE 
            $<$<CONFIG:Release>:-fpch-preprocess>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # GCC-specific PCH optimizations  
        target_compile_options(${TARGET_NAME} PRIVATE 
            $<$<CONFIG:Release>:-fpch-preprocess>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        # MSVC automatically handles PCH efficiently
        message(STATUS "Using MSVC built-in PCH handling")
    endif()
endfunction()

# Optional: Function to disable PCH for specific files that might conflict
function(disable_pch_for_files TARGET_NAME)
    set(FILES_TO_EXCLUDE ${ARGN})
    if(FILES_TO_EXCLUDE)
        set_source_files_properties(${FILES_TO_EXCLUDE} 
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON
        )
        message(STATUS "Disabled PCH for files: ${FILES_TO_EXCLUDE}")
    endif()
endfunction()

# Diagnostic function to report PCH effectiveness
function(report_pch_info TARGET_NAME)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "PCH Configuration for ${TARGET_NAME}:")
        message(STATUS "  - Build Type: Debug (Conservative PCH)")
        message(STATUS "  - System headers only")
    else()
        message(STATUS "PCH Configuration for ${TARGET_NAME}:")
        message(STATUS "  - Build Type: Release (Aggressive PCH)")
        message(STATUS "  - System + Library headers")
    endif()
    message(STATUS "  - Compiler: ${CMAKE_CXX_COMPILER_ID}")
    message(STATUS "  - CMake PCH Support: ${PCH_SUPPORTED}")
endfunction()
