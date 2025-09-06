# Nix CMake toolchain file for consistent C++23 builds on macOS/Linux
# Ensures CMake uses the Nix-provided compiler, standard library, and headers

# Set the C and C++ compilers from environment variables (set by shell.nix)
set(CMAKE_C_COMPILER "$ENV{CC}" CACHE STRING "")
set(CMAKE_CXX_COMPILER "$ENV{CXX}" CACHE STRING "")

# Use C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer libc++ on Darwin (macOS) for consistency with Nix
if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
    
    # Set rpath for Nix store libraries on macOS
    set(CMAKE_BUILD_RPATH "/nix/store")
    set(CMAKE_INSTALL_RPATH "/nix/store")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

# Avoid picking up system SDK headers/libraries
# (Nix should provide all dependencies in the environment)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
