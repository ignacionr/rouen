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
- **Pomodoro Timer**: Stay focused with built-in time management techniques
- **Cards System**: Modular design with draggable, resizable, and persistent cards
- **Menu Launcher**: Quick-access command palette with search functionality

### Information & Planning
- **Calendar**: Sync and view events with Google Calendar integration
- **Travel Planner**: Create and manage travel plans with destinations and budgets
- **Email Client**: Connect to IMAP/SMTP servers to read and compose emails
- **Weather Info**: Check current conditions and forecasts for any location
- **RSS Reader**: Follow news, podcasts, and blogs with integrated feed reader
- **AI Assistant**: Interact with Grok AI for help and information

### System Utilities
- **System Monitor**: Track CPU, memory, disk usage, and uptime
- **Environment Variables**: View and manage system environment variables
- **Subnet Scanner**: Discover and monitor devices on your local network
- **Database Repair**: Maintain and fix SQLite database files

### Media
- **Internet Radio**: Listen to streaming radio stations

### Interface
- **Dark Theme**: Eye-friendly interface designed for long coding sessions
- **Customizable Colors**: Each card features its own themed color scheme
- **Keyboard Shortcuts**: Power user navigation and quick access to functions
- **Card Snapshots**: Capture and save card contents as images
- **Persistent State**: Automatically save and restore your workspace layout

## Documentation

- [Card Infrastructure](src/cards/README.md) - Guide to the card and deck architecture
- [Hosts Infrastructure](src/hosts/README.md) - Documentation for external service connectors
- [Models Infrastructure](src/models/README.md) - Guide to data models and business logic

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

- C++23 compatible compiler
- CMake 3.30+
- SDL2 and SDL2_image
- ImGui
- GLFW

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/ignacionr/rouen.git
cd rouen

# Create build directory
mkdir -p build && cd build

# Configure and build
cmake ..
make

# Run the application
./rouen
```

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