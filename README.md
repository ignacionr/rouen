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

### Cross-Platform Support
- **Windows**: Full native Windows support with Windows API integration
- **macOS**: Native macOS app bundle with system integration
- **Linux**: Complete Linux desktop environment support

### System Utilities
- **System Monitor**: Track CPU, memory, disk usage, and uptime with native platform APIs
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

- C++23 compatible compiler (GCC 13+, Clang 16+, or MSVC 2022+)
- CMake 3.30+
- vcpkg package manager (recommended) or system packages

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
```

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

**Automated Windows Builds:**

Windows releases are automatically built and published via GitHub Actions when a new release is created. The workflow:
- Builds the application using the latest MSVC compiler and manually bootstrapped vcpkg
- Includes all required DLLs and dependencies
- Packages assets, fonts, and configuration files
- Creates a ready-to-run ZIP archive
- Publishes as a release artifact

You can also trigger a manual build by running the "Windows Release Build" workflow from the Actions tab.

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

## License

Open source - see LICENSE file for details.

## Contributing

Contributions welcome. Fork the repository, make your changes, and submit a pull request.