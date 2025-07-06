# Nix CI and Network Isolation

This document explains the network isolation strategy used in Rouen's CI/release workflows and how it ensures reproducible builds.

## Overview

Rouen's Linux CI workflow (`linux-release.yml`) uses Nix for dependency management with enforced network isolation during the build phase. This ensures that:

1. All dependencies are explicitly declared and managed via Nix
2. Builds are reproducible and deterministic
3. No unexpected network dependencies are introduced at build time
4. CI builds fail fast if network-dependent components are not properly managed

## Network Isolation Implementation

### CMake Configuration

The CI workflow sets `FETCHCONTENT_FULLY_DISCONNECTED=ON` to prevent CMake from making network requests during build:

```yaml
- name: Configure CMake (Debug)
  run: |
    nix-shell --run "cmake -B build-nix -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON"
```

### ImGui Dependency Strategy

Rouen uses a hybrid approach for ImGui:

- **Local Development**: FetchContent downloads ImGui automatically for convenience
- **CI/Release**: Network isolation is enforced, requiring explicit ImGui management

### Error Handling

When network isolation is enabled but ImGui is not available locally, the build fails with a clear message:

```
CMake Error at CMakeLists.txt:203 (message):
  ImGui requires FetchContent but FETCHCONTENT_FULLY_DISCONNECTED=ON.  For
  Nix builds, ImGui should be provided via system packages or local sources.
```

## Benefits

1. **Reproducible Builds**: No hidden network dependencies can affect build output
2. **Fast Failure**: Clear error messages when dependencies are missing
3. **Developer Experience**: Local builds remain convenient with automatic FetchContent
4. **CI Reliability**: Network issues cannot cause spurious build failures
5. **Security**: No untrusted code can be downloaded during CI builds

## Testing Network Isolation

To test the network isolation behavior locally:

```sh
# This should fail with a clear error
nix-shell --run "cmake -B build-test -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DFETCHCONTENT_FULLY_DISCONNECTED=ON"

# This should work normally  
nix-shell --run "cmake -B build-normal -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake"
```

## Vendoring ImGui for Offline Builds

For completely offline builds, ImGui can be vendored:

```sh
git clone https://github.com/ocornut/imgui.git external/imgui
cd external/imgui && git checkout v1.91.1 && cd ../..
cmake -B build-offline -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

## Dependencies Managed by Nix

The following dependencies are provided by Nix and available during network-isolated builds:

- **glaze**: JSON/serialization library
- **SDL2**: Graphics and input handling
- **SDL2_image**: Image loading support  
- **OpenSSL**: Cryptographic functions
- **curl**: HTTP client library
- **SQLite**: Database functionality
- **Google Test**: Testing framework
- **CMake & C++23 compiler**: Build toolchain

## Migration Summary

The Rouen project has been successfully migrated from vcpkg to Nix for the Linux CI workflow. Key achievements:

### ✅ Completed
- **Removed vcpkg dependency**: All vcpkg steps removed from `.github/workflows/linux-release.yml`
- **Added Nix integration**: Full Nix installation, caching, and verification in CI
- **Network isolation**: Enforced `FETCHCONTENT_FULLY_DISCONNECTED=ON` in CI builds
- **Hybrid ImGui strategy**: Local FetchContent for development, network isolation for CI
- **Error handling**: Clear error messages when network isolation conflicts occur
- **Documentation**: Updated README and created detailed CI isolation docs
- **Testing**: Local and CI build verification, network isolation testing

### ✅ Dependencies Now Managed by Nix
- **glaze**: JSON/serialization library
- **SDL2 & SDL2_image**: Graphics and input handling
- **OpenSSL**: Cryptographic functions
- **curl**: HTTP client library
- **SQLite**: Database functionality
- **Google Test**: Testing framework
- **CMake & Toolchain**: Build system and C++23 compiler

### ✅ ImGui Strategy
- **Development**: Automatic FetchContent download for convenience
- **CI/Release**: Network isolation enforced, fails fast if ImGui not available
- **Vendoring Support**: Instructions provided for fully offline builds

The migration ensures reproducible, network-isolated CI builds while maintaining developer convenience.

## Migration Summary

### CMakeLists.txt Logic

```cmake
if(FETCHCONTENT_FULLY_DISCONNECTED)
    message(FATAL_ERROR 
        "ImGui requires FetchContent but FETCHCONTENT_FULLY_DISCONNECTED=ON. "
        "For Nix builds, ImGui should be provided via system packages or local sources.")
endif()
```

### Nix Configuration

All dependencies except ImGui are declared in `flake.nix` and `shell.nix`. ImGui is intentionally excluded to maintain the FetchContent workflow for local development while ensuring CI builds are network-isolated.

This hybrid approach balances developer convenience with build reproducibility and security.
