# Rouen

A modern, card-based productivity dashboard with a clean, dark interface built on ImGui and SDL2.

![Rouen Dashboard](img/screenshot.png)

## About

Rouen is an opinionated productivity tool that organizes functionality into interactive cards. It provides a customizable workspace with tools for development, time management, and system monitoring.

The name "Rouen" is inspired by the Rouen pattern, a historic French playing card suit system. This naming reflects the application's card-based UI design, where each feature is presented as an interactive card that can be arranged and managed in your workspace.

## Features

### Development Tools
- **Git Integration**: View repository status, commits, branches and diffs visually
- **File System Navigation**: Browse, filter, and manage directories with color-coded file types
- **Code Editor**: Syntax highlighting powered by ImGuiColorTextEdit

### Productivity
- **Pomodoro Timer**: Stay focused with built-in time management techniques and sound notification when a session completes
- **Alarm Card**: Set alarms with sound notification, snooze and stop controls (uses bundled alarm sound)
- **Unit Converter**: Convert between different units of measurement including temperature, length, weight, volume, and area
- **Cards System**: Modular design with draggable, resizable, and persistent cards
- **Menu Launcher**: Quick-access command palette with search functionality

### Information & Planning
- **Calendar**: Sync and view events with Google Calendar integration
- **Travel Planner**: Create and manage travel plans with destinations and budgets
- **Email Client**: Connect to IMAP/SMTP servers to read and compose emails
- **Weather Info**: Check current conditions and forecasts for any location
- **RSS Reader**: Follow news, podcasts, and blogs with integrated feed reader
- **AI Assistant**: Interact with Grok AI for help and information with highly optimized chat interface, cached rendering for responsive input, proper message bubbles, and smooth scrolling
- **Bybit Assets**: View your cryptocurrency assets and account balance on Bybit exchange using API integration

## Installation

Rouen provides convenient installation options for Windows and macOS platforms with automated releases and dependency management.

### Windows Installation

#### Option 1: MSI Installer (Recommended)
> **✅ NEW**: User-mode MSI installer with automatic dependency management

1. **Download** the latest `.msi` file from the [Releases page](https://github.com/ignaciorodriguez/rouen/releases)
2. **Run** the MSI installer (no administrator privileges required)
3. **Install** in user mode - the application will be installed to your local AppData folder
4. **Launch** from Start Menu or Desktop shortcut

**MSI Installer Features:**
- **User-mode Installation**: No administrator privileges required
- **Automatic Dependencies**: All required DLLs and runtime libraries included
- **Start Menu Integration**: Creates Start Menu and Desktop shortcuts
- **Clean Uninstall**: Proper Windows uninstall support via Control Panel
- **Version Management**: Automatic updates and version tracking

#### Option 2: Portable ZIP Package
1. **Download** the `rouen-windows-x64.zip` file from the [Releases page](https://github.com/ignaciorodriguez/rouen/releases)
2. **Extract** to any folder on your computer
3. **Run** `rouen.exe` directly (no installation required)

**ZIP Package Features:**
- **Portable**: Run from any location without installation
- **Self-contained**: All dependencies included in the package
- **No Registry Changes**: Leaves no traces on the system
- **Multiple Versions**: Run different versions side-by-side

### macOS Installation

#### DMG Package (Apple Silicon)
1. **Download** the latest `.dmg` file from the [Releases page](https://github.com/ignaciorodriguez/rouen/releases)
2. **Mount** the DMG by double-clicking
3. **Drag** Rouen.app to your Applications folder
4. **Launch** from Applications or Spotlight

**macOS Requirements:**
- **Apple Silicon Mac** (M1, M2, M3+) - ARM64 native
- **macOS 11.0 (Big Sur)** or later - optimized for modern macOS
- **Automatic Dependencies**: All required libraries bundled in the app package
- **Enhanced Build System**: Robust dependency management with Nix (see below)

### Linux Installation

#### TAR.GZ Package (Ubuntu 20.04+)
> **✅ NEW**: Native Linux builds with Nix dependency management

1. **Download** the latest `rouen-linux-x64.tar.gz` file from the [Releases page](https://github.com/ignaciorodriguez/rouen/releases)
2. **Extract** the archive: `tar -xzf rouen-linux-x64.tar.gz`
3. **Install** system dependencies:
   ```bash
   sudo apt-get update
   sudo apt-get install libx11-6 libgl1-mesa-glx libasound2
   ```
4. **Run** using the launcher script: `./run-rouen.sh`

**Linux Package Features:**
- **Native Linux Build**: Compiled specifically for Linux x64 architecture
- **Nix Dependencies**: All required libraries built and bundled consistently
- **Launcher Script**: Convenient startup script with proper library path configuration
- **Self-contained**: All dependencies included, minimal system requirements
- **Universal Compatibility**: Tested on Ubuntu 20.04+ and compatible distributions

**Linux Requirements:**
- **Ubuntu 20.04 LTS** or newer (or equivalent Linux distribution)
- **X11 Display Server** (Wayland compatibility via XWayland)
- **OpenGL Support** (Mesa or proprietary drivers)
- **4 GB RAM** minimum, 8 GB recommended
- **100 MB** disk space for installation

### System Requirements

#### Windows
- **Windows 10** version 1903 or later (64-bit)
- **DirectX 11** compatible graphics card
- **4 GB RAM** minimum, 8 GB recommended
- **100 MB** disk space for installation

#### macOS
- **Apple Silicon Mac** (M1/M2/M3+ processors)
- **macOS 13.3 (Ventura)** or later
- **4 GB RAM** minimum, 8 GB recommended
- **100 MB** disk space for installation

#### Linux
- **Ubuntu 20.04 LTS** or newer (or equivalent distribution)
- **X11 Display Server** (Wayland via XWayland)
- **OpenGL Support** (Mesa or proprietary graphics drivers)
- **4 GB RAM** minimum, 8 GB recommended
- **100 MB** disk space for installation

### Multi-Platform Release Support
Rouen supports automated builds and releases for **Windows x64**, **macOS ARM64**, and **Linux x64** with comprehensive packaging and dependency management.

---

## Development Environment with Nix (Recommended)

Rouen now supports fully isolated, reproducible development and build environments using [Nix](https://nixos.org/). This replaces vcpkg and system/Homebrew dependencies for all supported platforms.

### Why Nix?
- **Reproducible builds**: All dependencies, compilers, and tools are pinned and isolated
- **Multi-platform**: Works on macOS (Apple Silicon), Linux, and (experimental) Windows
- **No system pollution**: No need for Homebrew, vcpkg, or system package managers
- **Easy onboarding**: One command to get a working build environment

### Getting Started with Nix

1. **Install Nix** (if you don't have it):
   See the [Nix installation guide](https://nixos.org/download.html).

2. **Enter the Nix shell**:
   ```sh
   nix-shell
   ```
   This will drop you into a shell with CMake, a modern C++23 compiler, all required libraries, and the correct macOS SDK (on macOS).

3. **Build using CMake**:
   ```sh
   cmake -B build-nix -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake
   cmake --build build-nix --parallel
   ./build-nix/rouen.app/Contents/MacOS/rouen  # macOS
   ./build-nix/rouen  # Linux
   ```
   Or use the VS Code build task: **Nix Build (Debug)**

4. **Run tests**:
   ```sh
   mkdir -p build-tests && cd build-tests
   cmake ../tests -DCMAKE_TOOLCHAIN_FILE=../cmake/nix-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
   cmake --build . --parallel
   ctest --output-on-failure
   ```
   Or use the VS Code test tasks (all labeled with "(Nix)").

### VS Code Integration

Rouen provides pre-configured VS Code tasks and launch configurations for Nix-based development:
- **Build**: `Nix Build (Debug)` (default build task)
- **Test**: All test tasks use Nix builds and are labeled with "(Nix)"
- **Debug**: Launch configs use Nix-built binaries and set up the correct environment

> **Note:** All previous vcpkg/system build tasks and launch configs have been removed in favor of Nix-only workflows. See `.vscode/tasks.json` and `.vscode/launch.json` for details.

### Nix Build Status

✅ **Fully Working**: The Nix build system is now fully operational for both local development and CI:
- **Local builds**: All compilation warnings/errors are caught with strict GCC 14.3.0/Clang 19 flags
- **Nix flake builds**: `nix build` produces working binaries for both Linux and macOS
- **CI integration**: GitHub Actions use the same Nix environment as local development
- **Icon handling**: Pre-generated application icons are used for reproducible builds across platforms
- **Test suite**: All tests pass with the strict warning configuration

### Customizing the Nix Environment

- Edit `shell.nix` to add or update dependencies
- The CMake toolchain file is at `cmake/nix-toolchain.cmake`
- The Nix shell removes Homebrew and `/usr/local` from `PATH` for full isolation on macOS
- macOS SDK frameworks are provided by Nix for proper header/library isolation

### Network Isolation and ImGui

Rouen now uses **system ImGui packages from Nix**, eliminating network dependencies during build:

- **All Builds**: ImGui is provided via Nix system packages (`imgui` in `flake.nix`)
- **CI/Release Builds**: Network isolation is fully supported (`FETCHCONTENT_FULLY_DISCONNECTED=ON`)
- **Texture Compatibility**: Enhanced `texture_utils.hpp` provides robust ImTextureID casting using C++23 `decltype` and type traits

#### C++23 Texture Utilities

The migration includes a sophisticated texture ID conversion system that handles different ImTextureID definitions:

```cpp
// Automatic type-safe conversion using decltype and constexpr
auto texture_id = rouen::helpers::texture_id_cast(gl_texture_handle);
auto sdl_texture_id = rouen::helpers::sdl_texture_cast(sdl_texture_ptr);
```

**Key Features:**
- **Compile-time type inference** using `decltype(ImTextureID{})`
- **Universal conversion** between pointer and integral types via `uint64_t` bridge
- **Zero runtime overhead** with `constexpr` functions and template metaprogramming
- **Type safety** with SFINAE and C++23 concepts

This ensures compatibility across different ImGui builds (system packages vs FetchContent) without runtime type checking.

---

## Building from Source

### Prerequisites

- **C++23 compatible compiler** (GCC 13+, Clang 16+, or MSVC 2022+)
- **CMake 3.30+**
- **Nix** (for all platforms)

### Nix-Based Build (Recommended)

The project has been **fully migrated to Nix** for dependency management, ensuring reproducible builds across all platforms and network isolation.

#### Dependencies Managed by Nix:
- ImGui (system package with local backends)
- Glaze (JSON serialization)
- SDL2, OpenSSL, SQLite, curl
- All required development tools and compilers

#### Build Instructions:

1. **Enter the Nix shell**:
   ```sh
   nix-shell  # Loads all dependencies automatically
   ```

2. **Configure and build**:
   ```sh
   cmake -B build-nix -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake
   cmake --build build-nix --parallel
   ```

3. **Run the application**:
   ```sh
   ./build-nix/rouen.app/Contents/MacOS/rouen  # macOS
   ./build-nix/rouen  # Linux
   ```

#### Benefits of Nix Migration:
- ✅ **Network isolation**: No FetchContent downloads at build time
- ✅ **Reproducible builds**: Exact same dependencies across environments
- ✅ **CI/CD efficiency**: Faster builds with dependency caching
- ✅ **Developer experience**: Single command setup (`nix-shell`)
- ✅ **Robust ImGui handling**: Seamless system package integration with custom backends

### Legacy vcpkg Workflow (Deprecated)

> **⚠️ Deprecated**: vcpkg-based builds are deprecated and will be removed in a future release. Use Nix for all new development.

---

## License

Open source - see LICENSE file for details.

## Contributing

Contributions welcome. Fork the repository, make your changes, and submit a pull request.

### Strict Warnings Configuration ✅ COMPLETED

Rouen now uses **extremely strict compiler warnings** with the same configuration as CI builds. The migration to strict warnings is **complete** and all builds are clean with no warnings.

#### Enabled Warnings

All builds (local and CI) use these strict warning flags:

- **`-Werror`**: Treat all warnings as errors (fail fast on issues)
- **`-Weverything`** (Clang): Enable all available warnings for maximum safety
- **`-Wunused-result`**: Catch unused `system()` calls and similar issues
- **`-Wshadow-all`**: Detect all variable shadowing problems
- **`-Wconversion`**: Catch implicit type conversions that may lose data
- **`-Wold-style-cast`**: Enforce modern C++ casting (static_cast, etc.)
- **`-Wnull-dereference`**: Detect potential null pointer dereferences
- **`-Wfloat-equal`**: Prevent unsafe floating-point equality comparisons
- **`-Wcast-align`**: Detect alignment issues in pointer casts
- **`-Wformat=2`**: Strict printf/format string validation
- **`-Wpedantic`**: Enforce strict ISO C++23 compliance

#### Completed Fixes

The strict warning migration addressed:

1. **Old-style Casts**: Replaced all C-style casts with modern C++ `static_cast`/`reinterpret_cast`
2. **Variable Shadowing**: Fixed all parameter and variable name conflicts
3. **Float Comparisons**: Use epsilon-based comparisons for floating-point values
4. **Unused Results**: Proper handling of `system()` calls with `[[maybe_unused]]`
5. **ImGuiColorTextEdit Modernization**: Updated "abandonware" code to modern C++23
6. **Macro Semicolons**: Removed inappropriate semicolons from macro invocations
7. **Exhaustive Enum Switches**: Ensured all enum values are handled

#### Warning Configuration Files

- **`cmake/warnings.cmake`**: Defines all warning flags with compiler-specific handling
- **Clang 19.1.7**: Full `-Weverything` with carefully chosen exclusions
- **GCC 14.3.0**: Equivalent warnings for cross-platform compatibility
- **Selective Disabling**: Only specific warnings disabled (e.g., `-Wno-padded`, `-Wno-unsafe-buffer-usage`)

#### Local-CI Parity

✅ **Achievement**: Local builds now catch **exactly the same warnings as CI**, eliminating surprises:

1. **Early Error Detection**: All issues caught during development, not in CI
2. **Consistent Code Quality**: Same ultra-strict standards across all platforms
3. **Maintainable Codebase**: Prevents common programming mistakes at compile time
4. **Future-Proof**: Ready for compiler updates and new warning flags

> **Success**: The codebase is now 100% warning-free with the strictest possible compiler settings. All builds pass with `-Werror` and comprehensive warning detection.