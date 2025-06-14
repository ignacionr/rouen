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
- **macOS 13.3 (Ventura)** or later for full C++23 support
- **Automatic Dependencies**: All required libraries bundled in the app package

### Linux Installation

#### TAR.GZ Package (Ubuntu 20.04+)
> **✅ NEW**: Native Linux builds with vcpkg dependency management

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
- **vcpkg Dependencies**: All required libraries built and bundled consistently
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

#### Windows Release (Production Ready)
> **✅ STATUS**: Windows release workflow is **COMPLETE** and production-ready! See `WINDOWS_RELEASE_COMPLETE.md` for full details.

**Windows Features:**
- **MSVC Compatibility**: Optimized build configuration for Visual Studio 2022+
- **Automated Releases**: Complete GitHub Actions workflow for Windows builds (✅ **COMPLETED**)
- **DLL Packaging**: Automatic inclusion of all required dependencies (libcurl, OpenSSL, SDL2, etc.) (✅ **FULLY WORKING**)
- **MSI Installer**: User-mode Windows installer (.msi) with automatic dependency management (✅ **NEW**)
- **Application Icon**: Multi-resolution Windows icon (.ico) with taskbar and Explorer integration (✅ **NEW**)
- **Debug Console**: Development builds include dedicated console window for real-time debug output (✅ **NEW**)
- **Windows Resources**: Embedded version information and application metadata (✅ **NEW**)
- **Type Safety**: Enhanced type conversion handling for size_t/int compatibility
- **Warning Suppression**: Targeted warning suppression for third-party libraries
- **Optimization Flags**: Platform-specific optimization flags (/O2 for MSVC)
- **Release Automation**: Push-to-release pipeline with comprehensive dependency packaging (✅ **READY**)

#### macOS Release (ARM64 - Apple Silicon)
> **✅ STATUS**: macOS release workflow is **COMPLETE** and ready for testing!

**macOS Features:**
- **Apple Silicon Support**: Native ARM64 builds optimized for M1, M2, M3+ Macs
- **C++23 std::format Support**: Requires macOS 13.3 (Ventura) or later for full C++23 compatibility
- **App Bundle Packaging**: Proper macOS .app bundle structure with all dependencies
- **Dynamic Library Bundling**: Automatic dependency resolution and bundling using `otool` and `install_name_tool`
- **DMG Creation**: Professional disk image installer for easy distribution
- **vcpkg Integration**: Full vcpkg support with `arm64-osx` triplet for dependency management
- **Code Signing**: Ad-hoc signing support (requires developer certificate for distribution)
- **Automated Releases**: Complete GitHub Actions workflow matching Windows feature parity
- **Minimum Requirements**: macOS 13.3 (Ventura) or later, Apple Silicon Mac recommended

**macOS Release Workflow Features:**
- Comprehensive dependency manifest generation
- Asset and resource copying to app bundle
- Library path fixing for relocatable binaries
- DMG creation with Applications folder symlink
- Artifact upload and GitHub release integration
- Detailed build verification and logging

#### Linux Release (x64 - Ubuntu 20.04+)
> **✅ STATUS**: Linux release workflow is **COMPLETE** and production-ready!

**Linux Features:**
- **Native x64 Build**: Optimized for Linux x64 architecture with GCC 11+ and C++23 support
- **vcpkg Dependency Management**: Consistent cross-platform dependency resolution and building
- **Ubuntu Compatibility**: Tested on Ubuntu 20.04 LTS and newer distributions
- **Automated CI/CD**: Complete GitHub Actions workflow for Linux builds (✅ **NEW**)
- **System Integration**: Proper X11, OpenGL, and audio system integration
- **Self-contained Packaging**: All dependencies bundled in TAR.GZ archive
- **Launcher Script**: Convenient startup script with proper library path configuration
- **Test Coverage**: Automated testing of core SSL and HTTP functionality
- **Cross-compiler Support**: Built with modern GCC supporting full C++23 feature set

**Linux Release Workflow Features:**
- Automatic system dependency installation (X11, OpenGL, audio libraries)
- vcpkg package installation and caching for faster builds
- Debug and Release build configurations with Ninja build system
- Comprehensive test suite execution (SSL modes, HTTP/SSL fetch tests)
- TAR.GZ archive creation with launcher script and documentation
- GitHub release integration with cross-platform artifact management
- Build verification and executable validation

#### Cross-Platform Development Support
- **macOS**: Full automated CI/CD with ARM64 release packaging (✅ **COMPLETED**)
- **Windows**: Full automated CI/CD with x64 release packaging (✅ **COMPLETED**)
- **Linux**: Full automated CI/CD with x64 release packaging (✅ **COMPLETED**)

### System Utilities
- **System Monitor**: Track CPU, memory, disk usage, and uptime with native platform APIs
- **Settings Management**: Centralized configuration interface with category-based organization, search/filtering, sensitive value masking, and comprehensive status indicators for all application settings
- **Environment Variables**: View and manage system environment variables
- **Subnet Scanner**: Discover and monitor devices on your local network
- **Database Repair**: Maintain and fix SQLite database files

### Media
- **Internet Radio**: Listen to streaming radio stations
- **Alarm Sound**: Bundled alarm.mp3 is used for alarm notifications

### Interface
- **Dark Theme**: Eye-friendly interface designed for long coding sessions
- **Customizable Colors**: Each card features its own themed color scheme
- **Keyboard Shortcuts**: Power user navigation and quick access to functions
- **Card Snapshots**: Capture and save card contents as images
- **Persistent State**: Automatically save and restore your workspace layout

### Resource Management
- **Cross-Platform Resource Path**: Unified handling of resources in both development and app bundle environments
- **Auto-discovery**: Automatic detection and loading of resources from appropriate locations
- **Bundled Resources**: All required files (fonts, sounds, presets) are properly packaged in the application bundle

## Documentation

- [Card Infrastructure](src/cards/README.md) - Guide to the card and deck architecture
- [Hosts Infrastructure](src/hosts/README.md) - Documentation for external service connectors
- [Models Infrastructure](src/models/README.md) - Guide to data models and business logic
- [Platform Utilities](src/helpers/platform_utils.hpp) - Cross-platform utilities for resource handling and more
- [Windows Build Guide](docs/windows-build.md) - Comprehensive guide for Windows builds and automated releases
- [macOS Release Workflow](.github/workflows/macos-release.yml) - Complete macOS ARM64 release automation

## Configuration

Rouen provides centralized configuration management through the Settings card, accessible via the System menu. The settings interface organizes all configuration options into logical categories:

### Configuration Categories

- **API Credentials**: Manage API keys for external services (Grok AI, Google Calendar, etc.)
- **JIRA Profiles**: Configure JIRA server connections and authentication
- **Bybit Config**: Set up cryptocurrency exchange API credentials
- **System Paths**: Configure file system paths and directories
- **Executable Paths**: Configure paths to external executables (mpv, git, cmake, etc.)
- **Logging**: Control logging levels and output preferences
- **General**: Application-wide settings and preferences

### Settings Card Features

- **Category-Based Organization**: Settings grouped by functional area for easy navigation
- **Search and Filtering**: Quickly find specific settings using the search functionality
- **Sensitive Value Masking**: API keys and passwords are masked by default with toggle visibility
- **Status Indicators**: Color-coded status shows which settings are configured (SET), empty (EMPTY), required (REQUIRED), or sensitive (SENSITIVE)
- **Missing Configuration Alerts**: Visual indicators highlight required settings that need configuration
- **Live Updates**: Settings refresh automatically to reflect current configuration state
- **Export to .env**: Generate a .env file with all current configuration for easy deployment and backup

### Environment Variables

Configuration can be managed through environment variables or .env files for automation and deployment:

#### .env File Support

Rouen automatically loads configuration from a `.env` file located in the same directory as the executable. The .env file uses standard key=value format:

```bash
# API Credentials
GROK_API_KEY=your_grok_api_key_here
BYBIT_API_KEY=your_bybit_key

# Logging Configuration  
ROUEN_LOG_LEVEL=INFO
ROUEN_DEBUG=true
```

**Features:**
- **Automatic Loading**: .env file is loaded automatically when the application starts
- **Priority Handling**: .env file values take precedence over system environment variables
- **Export Functionality**: Use the "Export to .env" button in Settings to generate a .env file with all current configuration
- **Comment Support**: Lines starting with # are treated as comments
- **Quoted Values**: Supports both quoted and unquoted values with proper escape sequence handling

#### Environment Variable Priority

Configuration values are resolved in the following order (highest to lowest priority):
1. Values from .env file
2. System environment variables
3. Default values (if configured)

#### Executable Path Configuration

Rouen allows customization of external executable paths without modifying the source code. The following executables can be configured:

- `MPV_PATH`: Path to the MPV media player (default: "mpv")
- `CMAKE_PATH`: Path to CMake command (default: "cmake")
- `GIT_PATH`: Path to Git command (default: "git")
- `SAY_PATH`: Path to text-to-speech command (default: "say")
- `BASH_PATH`: Path to Bash shell (default: "/bin/bash")
- `SUDO_PATH`: Path to sudo command (default: "sudo")
- `VSCODE_PATH`: Path to VS Code or an alternative editor (default: "code")
- `PING_PATH`: Path to ping command (default: "ping")

You can configure these paths using any of the following methods:

1. Through the Settings card in the application UI (System → Settings → Executable Paths)
2. By setting environment variables (e.g., `ROUEN_CMAKE_PATH=/usr/local/bin/cmake`)
3. By adding entries to a .env file in the application directory:
   ```
   # External executable paths
   ROUEN_MPV_PATH=/opt/mpv/bin/mpv
   ROUEN_GIT_PATH=/usr/local/bin/git
   ROUEN_VSCODE_PATH=/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code
   ```

**Path Validation**: Paths are validated when the application starts. If a path is invalid:
- A warning is logged to the console
- The application falls back to the default value
- The configuration is automatically updated with the working default

This ensures the application continues to function even if custom executable paths are misconfigured.

#### Common Environment Variables

- `GROK_API_KEY`: API key for Grok AI integration
- `BYBIT_API_KEY`: API key for Bybit exchange integration
- `BYBIT_SECRET`: Secret key for Bybit exchange integration
- `ROUEN_LOG_LEVEL`: Override default logging level (ERROR, WARN, INFO, DEBUG, TRACE)
- `ROUEN_DEBUG`: Enable debug mode (true/false)

#### SSL/TLS Configuration

Rouen provides flexible SSL/TLS configuration for HTTPS connections (including JIRA integration) to work in various network environments. These settings can be configured either through the Settings UI or environment variables.

##### Using the Settings UI (Recommended)

Access the Settings card through **System → Settings** in the application menu, then select the **HTTP SSL Configuration** category. From there, you can:

1. Choose your SSL mode from a dropdown menu
2. See detailed descriptions of each mode
3. View which settings will be affected

This is particularly helpful for Windows users who may not be comfortable setting environment variables.

##### Using Environment Variables

The following environment variables can be used to configure SSL:

- `ROUEN_SSL_MODE`: Set SSL verification mode
  - `strict`: Full certificate validation including revocation checking (default)
  - `relaxed`: Suitable for corporate environments - disables certificate revocation checking but maintains certificate chain and hostname verification
  - `compatible`: Maximum cipher compatibility for problematic servers
  - `atlassian`: Optimized for Atlassian Cloud services (*.atlassian.net)
  - `insecure`: Disables all certificate validation (use with caution, only for testing)
- `ROUEN_SSL_VERIFY_PEER`: Enable/disable peer certificate verification (true/false)
- `ROUEN_SSL_VERIFY_HOST`: Enable/disable hostname verification (true/false)
- `ROUEN_SSL_CHECK_REVOCATION`: Enable/disable certificate revocation checking (true/false)

**Example configurations:**

Atlassian Cloud (recommended for *.atlassian.net):
```bash
ROUEN_SSL_MODE=atlassian
```

Corporate environment with restricted certificate revocation access:
```bash
ROUEN_SSL_MODE=relaxed
```

Maximum compatibility for problematic servers:
```bash
ROUEN_SSL_MODE=compatible
```

Development/testing environment:
```bash
ROUEN_SSL_MODE=insecure
# Warning: Only use this for testing purposes
```

Custom SSL configuration:
```bash
ROUEN_SSL_VERIFY_PEER=true
ROUEN_SSL_VERIFY_HOST=true
ROUEN_SSL_CHECK_REVOCATION=false
```

**Note**: The `relaxed` mode is recommended for corporate environments where certificate revocation servers (OCSP/CRL) may not be accessible, while still maintaining reasonable security by verifying the certificate chain and hostname.

**Important**: SSL configuration changes are applied immediately when changed through the Settings UI or when environment variables are set before launching the application.

## Logging System

Rouen uses a comprehensive logging system for debugging and error tracking. The system is defined in `src/helpers/debug.hpp` and provides component-specific logging at various severity levels.

### Log Levels

- **ERROR**: Critical issues that prevent functionality from working correctly
- **WARN**: Non-critical issues that might cause unexpected behavior
- **INFO**: General information about application state
- **DEBUG**: Detailed information for debugging purposes
- **TRACE**: Low-level diagnostic information

### Using the Logging System

Each component has its own set of logging macros:

```cpp
// Basic logging example for the SYSTEM component
SYS_ERROR("An error occurred");
SYS_WARN("A warning message");
SYS_INFO("An informational message");
SYS_DEBUG("A debug message");
SYS_TRACE("A trace message");

// Format-enabled logging (C++23 std::format)
SYS_ERROR_FMT("Error in function {}: {}", function_name, error_code);
```

### Adding Logging to a New Component

To add logging for a new component:

1. Add the component-specific macros to `src/helpers/debug.hpp`:

```cpp
// Component-specific logging macros
#define COMPONENT_ERROR(message) LOG_COMPONENT("COMPONENT", LOG_LEVEL_ERROR, message)
#define COMPONENT_WARN(message) LOG_COMPONENT("COMPONENT", LOG_LEVEL_WARN, message)
#define COMPONENT_INFO(message) LOG_COMPONENT("COMPONENT", LOG_LEVEL_INFO, message)
#define COMPONENT_DEBUG(message) LOG_COMPONENT("COMPONENT", LOG_LEVEL_DEBUG, message)
#define COMPONENT_TRACE(message) LOG_COMPONENT("COMPONENT", LOG_LEVEL_TRACE, message)

// Format-enabled macros
#define COMPONENT_ERROR_FMT(fmt, ...) COMPONENT_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define COMPONENT_WARN_FMT(fmt, ...) COMPONENT_WARN(debug::format_log(fmt, __VA_ARGS__))
#define COMPONENT_INFO_FMT(fmt, ...) COMPONENT_INFO(debug::format_log(fmt, __VA_ARGS__))
#define COMPONENT_DEBUG_FMT(fmt, ...) COMPONENT_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define COMPONENT_TRACE_FMT(fmt, ...) COMPONENT_TRACE(debug::format_log(fmt, __VA_ARGS__))
```

2. Or define them in your component's header file:

```cpp
// Include the debug header
#include "helpers/debug.hpp"

// Define component-specific logging macros
#define COMPONENT_ERROR(message) LOG_COMPONENT("COMPONENT", LOG_LEVEL_ERROR, message)
// ... additional macro definitions ...
```

### Best Practices

- Use ERROR for critical failures that prevent functionality from working
- Use WARN for non-critical issues that might affect behavior
- Use INFO for important state changes and successful operations
- Use DEBUG for detailed operation tracking during development
- Use TRACE for low-level debugging information
- Prefer format-enabled macros (_FMT) for complex messages with variables
- Always include relevant context in error messages (function names, error codes, etc.)
- Never use raw `perror()` or `std::cerr` directly; use the logging system instead

### Runtime Log Level Control

The default log level is controlled by the `ROUEN_LOG_LEVEL` preprocessor variable:
- In debug builds, warnings and errors are shown by default
- In release builds, only errors are shown
- You can override this by defining `ROUEN_LOG_LEVEL` at compile time

## Building from Source

### Prerequisites

- **C++23 compatible compiler** (GCC 13+, Clang 16+, or MSVC 2022+)
- **CMake 3.30+**
- **vcpkg package manager** (recommended) or system packages

### Platform-Specific Requirements

#### macOS
- **macOS 14.0 (Sonoma) or later** - Required for C++23 `std::format` support and vcpkg library compatibility
- **Apple Silicon (ARM64) or Intel (x64)** - Both architectures supported
- **Xcode 15.0+** with Command Line Tools for C++23 standard library features

#### Windows
- **Windows 10/11** with Visual Studio 2022 or compatible C++23 compiler
- **x64 or ARM64 architecture** supported

#### Linux
- **Modern Linux distribution** with GCC 13+ or Clang 16+
- **glibc 2.34+** recommended for full C++23 standard library support

### Build Instructions

#### Option 1: Using vcpkg (Recommended)

vcpkg provides the most reliable cross-platform dependency management. This is the preferred method for building Rouen.

**Setup vcpkg:**

```bash
# Clone vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Bootstrap vcpkg
./bootstrap-vcpkg.sh  # Linux/macOS
# OR
.\bootstrap-vcpkg.bat  # Windows

# Install dependencies using manifest mode
cd /path/to/rouen
vcpkg install
```

**Build with vcpkg:**

```bash
# Clone the repository
git clone https://github.com/ignacionr/rouen.git
cd rouen

# Create build directory
mkdir -p build-vcpkg && cd build-vcpkg

# Configure with vcpkg toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build . --parallel

# Run the application
./rouen.app/Contents/MacOS/rouen  # macOS
# OR
./rouen  # Linux
# OR
.\rouen.exe  # Windows
```

#### Windows Build Notes

⚠️ **Important**: Windows builds require proper vcpkg configuration to avoid CMake package discovery issues.

**Required CMake Parameters for Windows:**
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR=./vcpkg_installed
```

When building on Windows with MSVC, the project automatically:
- Uses `/O2` optimization instead of GCC's `-O3` flag (MSVC doesn't support `-O3`)
- Suppresses common type conversion warnings (C4267, C4244, C4101)
- Enables large object file support (`/bigobj`) for complex template instantiations
- Applies Windows-specific compiler flags for better compatibility
- Removes any global `-O3` flags that would cause MSVC compilation errors

**Complete Windows Build Example:**
```cmd
# Clone and setup
git clone https://github.com/ignacionr/rouen.git
cd rouen

# Ensure vcpkg dependencies are installed
.\vcpkg\vcpkg.exe install

# Configure (Debug)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Debug -DVCPKG_INSTALLED_DIR=.\vcpkg_installed

# Build
cmake --build build --config Debug --parallel

# Run
.\build\rouen.exe
```

For GitHub Actions or CI builds on Windows, ensure the `DVCPKG_INSTALLED_DIR` parameter is correctly set to prevent package discovery failures.

**Dependencies managed by vcpkg:**
- SDL2 and SDL2_image
- SQLite3 with JSON1 extension
- OpenSSL
- curl with SSL support
- TinyXML2

**VS Code Integration:**

If you're using VS Code, the repository comes with pre-configured tasks and launch configurations:

- **Build Tasks**: Use Cmd+Shift+P → "Tasks: Run Task" to access:
  - "Build with vcpkg" (default build task)
  - "Configure with vcpkg" / "Configure with vcpkg (Release)"
  - "Build with vcpkg (Release)"
  - "Clean vcpkg build"
  - "Install vcpkg dependencies"
  - "Update vcpkg baseline"
  - Traditional build tasks for system dependencies

- **Debug Configurations**: Multiple launch configurations for:
  - macOS: vcpkg and traditional builds with lldb
  - Linux: vcpkg and traditional builds with gdb
  - Windows: vcpkg and traditional builds with msvc (when on Windows)

- **IntelliSense**: The `.vscode/c_cpp_properties.json` includes configurations for:
  - `macOS-vcpkg`: Uses vcpkg includes and ARM64 settings
  - `macOS-traditional`: Uses system includes
  - `Linux-vcpkg`: Uses vcpkg includes for Linux
  - `Linux-traditional`: Uses system includes for Linux

**Key VS Code Features:**
- Default build directory: `build-vcpkg`
- Automatic CMake toolchain configuration
- Proper include paths for vcpkg dependencies
- C++23 standard support
- ARM64 architecture support on macOS
- curl with SSL support
- TinyXML2

#### Option 2: System Dependencies (Linux/macOS)

For development builds using system package managers:

```bash
# Clone the repository
git clone https://github.com/ignacionr/rouen.git
cd rouen

# Install dependencies
# Ubuntu/Debian:
sudo apt-get install libsdl2-dev libsdl2-image-dev libsqlite3-dev libssl-dev libcurl4-openssl-dev libtinyxml2-dev

# macOS with Homebrew:
brew install sdl2 sdl2_image sqlite3 openssl curl tinyxml2

# Create build directory
mkdir -p build && cd build

# Configure and build
cmake ..
make

# Run the application
./rouen
```

#### Linux

Linux builds use vcpkg for comprehensive cross-platform dependency management.

**Prerequisites:**
- **Ubuntu 20.04 LTS or newer** (or equivalent Linux distribution)
- **GCC 11+ or Clang 16+** with C++23 support
- **CMake 3.20+** and **Ninja** build system
- **vcpkg package manager**
- **System dependencies**: X11, OpenGL, and audio libraries

**System Dependencies Installation:**
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  libx11-dev \
  libxrandr-dev \
  libxinerama-dev \
  libxcursor-dev \
  libxi-dev \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libasound2-dev \
  libpulse-dev \
  libudev-dev
```

**Build Steps:**
```bash
# Clone the repository
git clone https://github.com/ignacionr/rouen.git
cd rouen

# Bootstrap vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh && cd ..

# Install dependencies via vcpkg
./vcpkg/vcpkg install

# Configure and build (Release)
mkdir -p build-vcpkg && cd build-vcpkg
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -G "Ninja"

# Build
ninja

# Run the application
./rouen
```

**Linux-Specific Features:**
- **Native x64 builds** optimized for Linux performance
- **vcpkg dependency management** ensures consistent library versions
- **C++23 standard** with full `std::format` support
- **Modern OpenGL** with Mesa or proprietary driver support
- **X11 integration** with Wayland compatibility via XWayland
- **Audio support** through ALSA and PulseAudio
- **Automated testing** with SSL and HTTP functionality verification

#### Windows

Rouen has comprehensive Windows support with automated builds and releases.

**Prerequisites:**
- Visual Studio 2022 with C++ support (or compatible C++23 compiler)
- vcpkg package manager
- Git

**Build Steps:**

```powershell
# Clone the repository
git clone https://github.com/ignacionr/rouen.git
cd rouen

# Install dependencies via vcpkg (manifest mode - automatic)
# Dependencies are defined in vcpkg.json and will be installed automatically

# Create build directory
mkdir build-vcpkg
cd build-vcpkg

# Configure with vcpkg toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows

# Build
cmake --build . --config Release

# Run the application
.\Release\rouen.exe
```

**Windows-Specific Features:**
- **Full System Integration**: Complete Windows API integration for system monitoring
- **Windows User Directories**: Proper APPDATA and user profile directory support
- **Windows Font System**: Automatic Windows system font directory detection
- **File Operations**: Native Windows `start` command support for opening files
- **Process Management**: Windows-specific process enumeration and management
- **Memory & Disk Monitoring**: Native Windows APIs for system resource monitoring
- **Path Handling**: Robust Windows path handling with proper drive letter support
- **POSIX Compatibility**: Cross-platform process status handling with Windows-specific implementations
- **Type Safety**: Full C++23 compatibility with MSVC compiler and proper type conversions

**Latest Compatibility Improvements (2025-05-28):**
- Fixed POSIX process status macros (`WIFEXITED`, `WEXITSTATUS`, etc.) for Windows compatibility
- Resolved type conversion warnings and missing identifiers
- Enhanced cross-platform command execution support
- Improved Unicode character handling in font systems

**vcpkg Configuration Files:**
- `vcpkg.json`: Defines project dependencies and features
- `vcpkg-configuration.json`: Platform-specific settings (e.g., ARM64 on macOS)

**Architecture Support:**
- Windows: x64-windows, x86-windows, arm64-windows
- macOS: arm64-osx (Apple Silicon), x64-osx (Intel)
- Linux: x64-linux, arm64-linux

**Note:** If you encounter vcpkg baseline errors, run:
```bash
./scripts/update_vcpkg_baseline.sh
```

**Automated Windows Releases:**

Windows releases are automatically built and published via GitHub Actions when a new release is created. The dedicated Windows workflow includes comprehensive fixes for DLL packaging:

**DLL Collection Strategy:**
- **CMake Post-Build Integration**: Automatically copies DLLs to the executable directory during build
- **Dual-Layer DLL Discovery**: CMake copies DLLs first, then vcpkg fallback ensures nothing is missed
- **Comprehensive Dependency Scanning**: Locates all required DLLs across multiple vcpkg directories
- **Critical DLL Verification**: Specifically checks for libcurl.dll, SSL libraries, and system DLLs

**Dependencies Automatically Included:**
- **libcurl.dll** and SSL libraries (libssl-3-x64.dll, libcrypto-3-x64.dll) for network functionality
- **crypt32.dll** and Windows system libraries for cryptographic operations  
- **SDL2.dll** and SDL2_image.dll for graphics and multimedia
- **zlib1.dll**, **tinyxml2.dll** for data handling
- Visual C++ runtime redistributables (msvcp140.dll, vcruntime140.dll)

**Enhanced Build Process:**
- Fresh vcpkg clone with smart package caching to prevent corruption
- Explicit VCPKG_TARGET_TRIPLET and VCPKG_INSTALLED_DIR configuration
- Multiple fallback mechanisms for missing DLLs with extensive debugging
- Emergency DLL search throughout the entire vcpkg installation
- Comprehensive dependency manifest with troubleshooting guidance

**Release Artifacts:**
- Self-contained ZIP archive with all dependencies
- DEPENDENCIES.txt manifest listing all included DLLs with categorization
- Troubleshooting guidance for missing CURL or SSL libraries
- Assets, fonts, and configuration files properly packaged

**Manual Workflow Trigger:**
You can trigger a manual build by running the "Windows Release Build" workflow from the GitHub Actions tab with an optional release creation toggle.

**Why Windows-Only Releases?**
- Windows has the most complex dependency management requirements
- vcpkg provides excellent Windows library packaging
- GitHub Actions Windows runners offer reliable MSVC build environment
- Most users require Windows binaries with all DLLs included

**Troubleshooting GitHub Actions:**
- The workflow automatically handles vcpkg setup, so no pre-configuration is needed
- Build artifacts are always available even if release upload fails
- Verbose logging helps diagnose any build issues
- All required dependencies are cached for faster subsequent builds

### Installation (macOS)

To install Rouen.app to your Applications folder:

```bash
# After building the application
cd build
sudo cmake --install .
```

This will copy the Rouen.app bundle to your /Applications folder, making it available in Launchpad and Spotlight.

## Testing

Rouen includes a comprehensive testing framework built on Google Test (gtest) with advanced testing features including fixtures, parameterized tests, mocking, and CI/CD integration.

### Test Framework Features

- **Google Test Integration**: Modern C++ testing framework with rich assertion macros
- **Test Fixtures**: Reusable test environments with setup and teardown
- **Parameterized Tests**: Data-driven testing with multiple input scenarios
- **Google Mock**: Object mocking for unit testing with dependencies
- **Death Tests**: Validation of expected crashes and exceptions
- **Performance Tests**: Timing validation for performance-critical code
- **CTest Integration**: Automated test running for CI/CD pipelines

### Running Tests

#### Quick Test Execution

```bash
# Build and run all tests (from project root)
cd build-tests
cmake --build . --parallel
ctest --output-on-failure

# Or use the comprehensive test target
make run-all-tests
```

#### Individual Test Suites

```bash
# Run specific test executables
./test_fetch_ssl          # HTTP/SSL configuration tests
./test_ssl_modes_simple   # SSL UI mode configuration tests
./test_math_operations     # Advanced math operations with mocking
./legacy_tests             # Original console-based tests
```

#### Test Target Options

The build system provides several test execution targets:

- `run-gtest-only`: Execute only Google Test suites
- `run-legacy-tests`: Execute original console-based tests
- `run-all-tests`: Execute comprehensive test suite (gtest + legacy)
- Individual test executables can be run directly

### Test Organization

#### Current Test Suites

1. **HTTP/SSL Configuration Tests** (`test_fetch_ssl.cpp`)
   - Environment variable handling and validation
   - SSL option factory method testing
   - Boolean value parsing verification
   - Test fixtures with proper setup/teardown

2. **SSL UI Mode Tests** (`test_ssl_modes_simple.cpp`)
   - UI-based SSL configuration mode testing
   - Direct settings verification without ConfigService dependencies
   - Cross-platform environment variable testing
   - Certificate verification option testing

3. **Math Operations Tests** (`test_math_operations.cpp`)
   - Parameterized prime number validation
   - Exception testing with EXPECT_THROW
   - Google Mock integration examples
   - Performance testing with timing validation
   - Death test demonstrations

3. **Legacy Tests** (`tests.cpp`)
   - Original console-based tests for backward compatibility
   - Basic functionality validation

### Adding New Tests

#### Creating a New Test Suite

1. **Create the test file** in the `tests/` directory:

```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "your_module.hpp"

// Test fixture for shared setup
class YourModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }
    
    void TearDown() override {
        // Cleanup code
    }
};

// Basic test
TEST_F(YourModuleTest, BasicFunctionality) {
    EXPECT_EQ(expected_value, actual_value);
    ASSERT_TRUE(condition);
}

// Parameterized test
class ParameterizedTest : public ::testing::TestWithParam<int> {};

TEST_P(ParameterizedTest, TestWithParameters) {
    int param = GetParam();
    EXPECT_GT(param, 0);
}

INSTANTIATE_TEST_SUITE_P(
    ParameterValues,
    ParameterizedTest,
    ::testing::Values(1, 2, 3, 5, 8, 13)
);
```

2. **Add to CMakeLists.txt**:

```cmake
# For tests with HTTP dependencies
add_gtest_http_executable(test_your_module test_your_module.cpp)

# For tests without HTTP dependencies  
add_gtest_executable(test_your_module test_your_module.cpp)
```

#### Test Types and Patterns

- **Unit Tests**: Test individual functions and classes in isolation
- **Integration Tests**: Test interactions between components
- **Fixture Tests**: Use `TEST_F` for tests requiring shared setup
- **Parameterized Tests**: Use `TEST_P` for data-driven testing
- **Mock Tests**: Use Google Mock for testing with dependencies
- **Death Tests**: Use `EXPECT_DEATH` for testing expected failures

### Test Configuration

#### Environment Variables for Testing

Tests can be configured using environment variables:

```bash
# SSL testing configuration
export ROUEN_SSL_MODE=relaxed
export ROUEN_SSL_VERIFY_PEER=true
export ROUEN_SSL_VERIFY_HOST=true

# Logging level for test output
export ROUEN_LOG_LEVEL=DEBUG

# Run tests with configuration
ctest --output-on-failure
```

#### Test Build Configuration

The test build system uses a separate build directory (`build-tests`) to:

- Isolate test builds from main application builds
- Prevent pollution of the main build directory
- Enable parallel development and testing workflows
- Support different compiler flags and optimization levels

### CI/CD Integration

Tests are designed for automated execution in CI/CD pipelines:

- **CTest Integration**: Tests can be executed via `ctest` for automated reporting
- **Exit Code Handling**: Proper exit codes for CI/CD success/failure detection
- **Parallel Execution**: Tests can run in parallel for faster CI/CD builds
- **Detailed Output**: Comprehensive test output for debugging failed builds

### Dependencies

The testing framework requires:

- **Google Test (gtest)**: Managed via vcpkg
- **Google Mock (gmock)**: Included with Google Test
- **CURL with SSL**: For HTTP/SSL configuration tests
- **C++23 Compiler**: Same requirements as the main application

### Best Practices

- **Test Isolation**: Each test should be independent and repeatable
- **Clear Naming**: Use descriptive test names that explain what is being tested
- **Fixtures for Setup**: Use test fixtures for complex setup/teardown
- **Mock External Dependencies**: Use Google Mock to isolate units under test
- **Test Edge Cases**: Include boundary conditions and error scenarios
- **Performance Awareness**: Use performance tests for time-critical code
- **Documentation**: Comment complex test logic and parameterized test data

### Troubleshooting

#### Common Issues

1. **Build Failures**: Ensure all dependencies are installed via vcpkg
2. **Test Failures**: Check environment variable configuration
3. **SSL Test Issues**: Verify network connectivity and SSL configuration
4. **Performance Test Flakiness**: Adjust timing thresholds for slower systems

#### Debug Options

```bash
# Run tests with verbose output
ctest --verbose

# Run specific test with gtest options
./test_fetch_ssl --gtest_filter="*SSLMode*" --gtest_repeat=3

# Run tests with debug information
ROUEN_LOG_LEVEL=DEBUG ./test_math_operations
```

For detailed testing documentation and examples, see [tests/README_GTEST.md](tests/README_GTEST.md).

## Compiler Warning Flags

Rouen is developed with strict compiler warning settings to ensure high-quality, robust code. We use a two-tiered approach to warnings:

### Base Warnings (All Code)
These warnings apply to all code, including third-party libraries:
- `-Wall`: Enable all common warnings
- `-Wextra`: Enable extra warnings not covered by `-Wall`
- `-Wpedantic`: Enforce strict ISO C++ compliance
- `-Wnull-dereference`: Warn about potential null pointer dereferences
- `-Wformat=2`: Warn about printf format issues
- `-Wimplicit-fallthrough`: Warn about fallthrough in switch statements
- `-Wunused`: Warn about unused variables/functions

### Strict Warnings (Project Code Only)
These stricter warnings only apply to the project's code, not third-party libraries:
- `-Wconversion`: Warn on implicit type conversions
- `-Wsign-conversion`: Warn on sign conversions
- `-Wdouble-promotion`: Warn about implicit doubles from float
- `-Wshadow`: Warn when a variable declaration shadows another
- `-Wunreachable-code`: Warn about unreachable code
- `-Wself-assign`: Warn about self-assignment
- `-Woverloaded-virtual`: Warn when a virtual function declaration hides another
- `-Wrange-loop-analysis`: Warn about issues with range-based for loops
- `-Wredundant-move`: Warn about redundant move operations
- `-Wundef`: Warn if an undefined identifier is evaluated in #if
- `-Wdeprecated`: Warn about deprecated feature usage

After all warnings are addressed, we enable `-Werror` to treat warnings as errors, ensuring the codebase remains warning-free.

### Developer Guidelines

When contributing to Rouen:

1. **Maintain Warning-Free Code**: All code must compile without warnings when using the project's strict compiler settings.
2. **Don't Disable Warnings**: Avoid using pragma directives to disable warnings in your code.
3. **Fix Issues, Don't Hide Them**: Address the root cause of warnings rather than suppressing them.
4. **Test with Warnings Enabled**: Always test your changes with all warning flags enabled.

These strict settings help catch potential bugs early, ensure consistent code quality, and maintain the project's long-term stability and maintainability.

## 🛠️ Development Documentation

### IDE Configuration

#### VS Code C++ Setup

Rouen includes a comprehensive VS Code configuration for optimal C++ development experience:

- **[VS Code C++ Configuration Guide](VSCODE_CPP_CONFIGURATION.md)** - Complete setup and troubleshooting guide
  - IntelliSense configuration for C++23
  - vcpkg integration with proper include paths
  - Compile commands generation and usage
  - False positive error elimination
  - CMake Tools integration
  - Debugging configuration

**Quick Setup for VS Code:**
1. Install the C++ Extension Pack
2. Open the project in VS Code
3. Select "macOS-vcpkg" configuration (bottom-right status bar)
4. Run the "Configure Debug with vcpkg" task
5. IntelliSense should work without false positive errors

**Supported Configurations:**
- **macOS-vcpkg**: Primary development configuration with all dependencies
- **macOS-traditional**: Fallback using system libraries
- **Linux-vcpkg**: Cross-platform development support
- **Linux-traditional**: Linux system library configuration

### Platform-Specific Setup Guides

- **[Windows Development Setup](WINDOWS_DEBUG_SETUP.md)** - Complete VS Code debugging guide for Windows
  - Debug console configuration
  - Application icon integration  
  - MSVC compiler optimization
  - vcpkg dependency management
  - Windows-specific troubleshooting

- **[macOS Development Setup](MACOS_DEBUG_SETUP.md)** - Complete VS Code debugging guide for macOS
  - LLDB debugging configuration
  - Apple Silicon optimization
  - Xcode integration
  - Homebrew dependency management
  - macOS-specific troubleshooting

### Build System

#### CMake Configuration

The project uses CMake with vcpkg for dependency management:

```bash
# Configure with vcpkg (recommended)
cmake -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build build-vcpkg --parallel
```

#### Dependencies

All dependencies are managed through vcpkg:
- **Core Libraries**: SDL2, OpenGL, OpenSSL, cURL
- **UI Framework**: Dear ImGui with custom extensions
- **Data Formats**: TinyXML2, SQLite3
- **Serialization**: Glaze (header-only JSON library)
- **Testing**: Google Test and Google Mock

#### C++23 Features

The project uses modern C++23 features:
- `std::format` for string formatting
- Concepts and constraints
- Range-based algorithms
- Modern standard library features

**Minimum Requirements:**
- **macOS**: 13.3 (Ventura) or later for full C++23 support
- **Windows**: Visual Studio 2022 17.8 or later
- **Linux**: GCC 13+ or Clang 17+

### Key Development Features

- **Multi-Platform Debug Support**: Dedicated debug consoles and logging for each platform
- **Integrated Icon Systems**: Platform-native icon integration (ICO for Windows, ICNS for macOS)
- **Resource Management**: Embedded version information and application metadata
- **Strict Code Quality**: Warning-free compilation with enhanced error detection
- **Modern C++23**: Latest standard features with cross-platform compatibility
- **IntelliSense Integration**: Pre-configured VS Code setup eliminates false positive errors

## License

Open source - see LICENSE file for details.

## Contributing

Contributions welcome. Fork the repository, make your changes, and submit a pull request.