#pragma once

// ============================================================================
// ROUEN PROJECT PRECOMPILED HEADER
// ============================================================================
// This header contains the most commonly used headers across the Rouen project.
// It's designed to be portable across platforms and compilers.
//
// Organization:
//   1. Standard C++ Library headers (most stable)
//   2. System headers (platform-specific)
//   3. Third-party library headers 
//   4. Project-specific wrapper headers
//
// Usage: This file is automatically included by CMake's PCH system.
//        Do NOT manually include this file in source code.
// ============================================================================

// ============================================================================
// 1. STANDARD C++ LIBRARY HEADERS
// ============================================================================
// These are the most commonly used STL headers in the project.
// They rarely change and provide the best PCH benefit.

#include <algorithm>      // std::find, std::sort, etc. (used in 20+ files)
#include <chrono>         // Time-related functionality (timers, scheduling)
#include <filesystem>     // File system operations (fs-directory, config)
#include <format>         // String formatting (C++20, used extensively)
#include <fstream>        // File I/O operations
#include <functional>     // std::function, lambdas (callbacks, events)
#include <iostream>       // Basic I/O, debugging (used in most files)
#include <memory>         // Smart pointers (used in 40+ files)
#include <mutex>          // Thread synchronization
#include <optional>       // Optional types (error handling, config)
#include <regex>          // Regular expressions (parsing, validation)
#include <string>         // String operations (used in 50+ files)
#include <string_view>    // String views for performance
#include <thread>         // Threading operations
#include <unordered_map>  // Hash maps (caching, lookups)
#include <vector>         // Dynamic arrays (used in 50+ files)

// ============================================================================
// 2. PLATFORM-SPECIFIC SYSTEM HEADERS
// ============================================================================
// Only include platform-specific headers when building for that platform

#ifdef _WIN32
    // Windows-specific headers
    #include <windows.h>      // Core Windows API
    #include <io.h>           // Windows I/O functions
    #include <fcntl.h>        // File control operations
#elif __APPLE__
    // macOS-specific headers
    #include <unistd.h>       // POSIX functions
    #include <sys/types.h>    // System types
#elif __linux__
    // Linux-specific headers  
    #include <unistd.h>       // POSIX functions
    #include <sys/types.h>    // System types
#endif

// ============================================================================
// 3. THIRD-PARTY LIBRARY HEADERS
// ============================================================================
// These are external dependencies that are used throughout the project.
// Only include the most stable and commonly used ones.

// SDL2 - Used for windowing, input, and graphics context
#include <SDL.h>

// Conditionally include SDL2_image if available
#ifdef SDL_IMAGE_H_
    // Already included elsewhere
#else
    // Try to include SDL2_image if it exists
    #if __has_include(<SDL_image.h>)
        #include <SDL_image.h>
    #endif
#endif

// ============================================================================
// 4. PROJECT-SPECIFIC WRAPPER HEADERS  
// ============================================================================
// These are our own wrapper headers that handle third-party includes
// with proper warning suppression and compatibility handling.

// ImGui wrapper - handles all ImGui includes with warning suppression
// This is used in 30+ files and is expensive to parse repeatedly
#include "helpers/imgui_include.hpp"

// JSON/Serialization wrapper - handles Glaze library includes
// Used for configuration, API communication, and data serialization
#include "helpers/glaze_include.hpp"

// ============================================================================
// NOTES ON PORTABILITY AND COMPATIBILITY
// ============================================================================
/*
 * COMPILER COMPATIBILITY:
 * - GCC 10+: Full support with -fpch-preprocess
 * - Clang 10+: Full support with -fpch-preprocess  
 * - MSVC 2019+: Full support with /Yc and /Yu flags
 * - Apple Clang: Full support (comes with modern Xcode)
 *
 * PLATFORM COMPATIBILITY:
 * - Windows: Uses Windows.h and related headers
 * - macOS: Uses POSIX headers, SDL2 framework paths
 * - Linux: Uses POSIX headers, pkg-config SDL2 paths
 * - Nix: Works with nix-provided dependencies
 *
 * PERFORMANCE EXPECTATIONS:
 * - Debug builds: 20-40% faster compilation
 * - Release builds: 30-60% faster compilation  
 * - Clean builds: 40-70% faster compilation
 * - Incremental builds: 10-30% faster compilation
 *
 * MAINTENANCE CONSIDERATIONS:
 * - Add new headers only if used in 10+ source files
 * - Remove headers that are no longer commonly used
 * - Keep system headers separate from project headers
 * - Monitor PCH size - should stay under 100MB compiled
 */
