set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET "11.0")

# Set compiler flags for ARM64
set(VCPKG_C_FLAGS "-arch arm64 -mmacosx-version-min=11.0")
set(VCPKG_CXX_FLAGS "-arch arm64 -mmacosx-version-min=11.0 -std=c++23")

# OpenSSL specific fixes for ARM64
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED MACOSX_DEPLOYMENT_TARGET)

# Use system tools when possible
set(VCPKG_PREFER_SYSTEM_LIBS ON)
