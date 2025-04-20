# Rouen

A modern, card-based productivity dashboard with a clean, dark interface built on ImGui and SDL2.

![Rouen Dashboard](img/screenshot.png)

## About

Rouen is an opinionated productivity tool that organizes functionality into interactive cards. It provides a customizable workspace with tools for development, time management, and system monitoring.

The name "Rouen" is inspired by the Rouen pattern, a historic French playing card suit system. This naming reflects the application's card-based UI design, where each feature is presented as an interactive card that can be arranged and managed in your workspace.

## Features

- **Card-based interface**: Modular design with draggable, resizable cards
- **Git integration**: Visual Git status tracking and operations
- **Pomodoro timer**: Stay focused with built-in time management
- **Code editor**: Syntax highlighting powered by ImGuiColorTextEdit
- **File system navigation**: Browse directories efficiently
- **System monitoring**: Track system resources
- **Dark theme**: Eye-friendly interface designed for long sessions

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

## License

Open source - see LICENSE file for details.

## Contributing

Contributions welcome. Fork the repository, make your changes, and submit a pull request.