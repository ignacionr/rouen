# Windows VS Code Debugging Setup - Complete Guide

## 🎯 Quick Start (Press F5)

The VS Code configuration supports seamless debugging on Windows. Here's what happens when you press **F5**:

### Primary Debug Configuration: "🚀 Debug Rouen (vcpkg - Windows)"
1. **Automatic Dependency Installation**: vcpkg dependencies are installed if needed
2. **CMake Configuration**: Project configures with Debug symbols and vcpkg toolchain  
3. **Build**: Project builds with full debug information and Windows-specific optimizations
4. **Launch**: Debugger launches with proper Windows debugging support

## 🖥️ Windows-Specific Features

### Debug Console (Development Builds Only)
- **Automatic Console Allocation**: Debug builds automatically open a console window
- **Real-time Output**: All debug messages, logs, and stdout are redirected to the console
- **Console Title**: "Rouen Debug Console" for easy identification
- **Color Support**: Enhanced console output with proper text attributes
- **Release Builds**: Console is disabled in release builds for clean user experience

### Application Icon Integration
- **Windows Resource File**: `resources/rouen.rc` includes application icon and version info
- **ICO Format**: Multi-resolution icon file (`resources/rouen.ico`) with sizes: 256x256, 128x128, 64x64, 48x48, 32x32, 16x16
- **Explorer Integration**: Icon appears in Windows Explorer, taskbar, and Alt+Tab
- **Version Information**: Embedded version info accessible via file properties

## 🛠️ Available Debug Configurations

### 1. 🚀 Debug Rouen (vcpkg - Windows) - *Recommended*
- **Best for**: Normal Windows debugging workflow
- **What it does**: Builds with MSVC optimizations and full dependency management
- **Debug Console**: Automatically enabled for development builds
- **Use when**: Primary Windows development and debugging

### 2. 🔧 Debug Rouen (Quick Launch - Windows)  
- **Best for**: When binary already exists and you just want to debug
- **What it does**: Launches existing Windows binary without rebuilding
- **Use when**: You've already built and just want to run the debugger quickly

### 3. 🔧 Debug Rouen (Full Setup - Windows)
- **Best for**: Complete rebuild from scratch with optimal debug flags
- **What it does**: Runs full dependency install → configure → build → launch sequence
- **Use when**: Maximum debug information or after major changes

### 4. 📦 Debug Rouen (System Dependencies - Windows)
- **Best for**: System dependency debugging
- **What it does**: Uses system-installed libraries instead of vcpkg
- **Use when**: Debugging against system libraries or troubleshooting vcpkg issues

### 5. 🖥️ Debug Installed Rouen (Windows)
- **Best for**: Debugging the installed application
- **What it does**: Debugs the version installed in Program Files
- **Use when**: Testing the final installed application

## 🔧 Windows Build Tasks Available

| Task | Purpose | When to Use |
|------|---------|-------------|
| **Build with vcpkg (Windows)** | Standard Windows build with vcpkg | Default F5 behavior |
| **Build Debug with vcpkg (Windows)** | Optimized debug build with console | Maximum debug info needed |
| **Quick Debug Setup (Windows)** | Full dependency + build | First time or after clean |
| **Configure Debug with vcpkg (Windows)** | Setup debug configuration | Manual configuration needed |
| **Install vcpkg dependencies (Windows)** | Install dependencies only | Dependency issues |

## ⚡ Windows Keyboard Shortcuts

- **F5**: Start debugging (primary configuration)
- **Ctrl+F5**: Run without debugging
- **Shift+F5**: Stop debugging
- **F9**: Toggle breakpoint
- **F10**: Step over
- **F11**: Step into
- **Shift+F11**: Step out

## 🐛 Windows Debug Console Features

### Console Output Categories
```
=== Rouen Debug Console Initialized ===
Debug build - Console output enabled
=========================================

[INFO] Application startup messages
[WARN] Warning messages in yellow
[ERROR] Error messages in red
[DEBUG] Detailed debug information
```

### Console Commands (Debug Builds)
- The console shows real-time output from all debug macros
- `DB_INFO`, `DB_WARN`, `DB_ERROR` messages are displayed immediately
- All `std::cout` and `printf` output is visible
- SDL and ImGui debug messages are captured

### Console Window Management
- **Positioning**: Console opens alongside the main application window
- **Persistence**: Console remains open for the duration of the application
- **Scrollback**: Full scrollback buffer for reviewing earlier messages
- **Closing**: Console closes automatically when application exits

## 🔧 Windows-Specific Compiler Features

### MSVC Optimizations
- **Warning Level 4** (`/W4`): High warning level for code quality
- **Conformance Mode** (`/permissive-`): Strict C++ standard compliance
- **UTF-8 Support** (`/utf-8`): Proper Unicode handling
- **Large Objects** (`/bigobj`): Support for large translation units
- **Parallel Compilation** (`/MP`): Faster build times

### Suppressed Warnings
- **C4267**: size_t to int conversion (common in cross-platform code)
- **C4244**: double to float conversion (ImGui compatibility)

### Runtime Library
- **Dynamic Runtime**: `MultiThreadedDLL` for Release, `MultiThreadedDebugDLL` for Debug
- **Compatibility**: Matches vcpkg library linkage

## 📁 Windows File Structure

```
rouen/
├── resources/
│   ├── rouen.rc          # Windows resource file
│   └── rouen.ico         # Multi-resolution icon
├── cmake/
│   └── windows.cmake     # Windows-specific build configuration
└── src/
    └── rouen.cpp         # Main entry with Windows console setup
```

## 🎯 Troubleshooting Windows Issues

### Debug Console Not Appearing
1. Ensure you're using a **Debug** build configuration
2. Check that `_DEBUG` preprocessor definition is set
3. Verify Windows resource compilation succeeded
4. Try rebuilding with "Clean vcpkg build" first

### Icon Not Showing
1. Verify `resources/rouen.ico` exists and is valid
2. Check that `resources/rouen.rc` is included in build
3. Ensure Windows resource compiler (rc.exe) is available
4. Try rebuilding the Windows resource file

### vcpkg Dependency Issues
1. Run "Install vcpkg dependencies (Windows)" task
2. Check vcpkg triplet is set to `x64-windows`
3. Verify all required DLLs are copied to output directory
4. Review vcpkg-manifest-install.log for errors

### Performance Issues
1. Use Release build for performance testing
2. Check Windows Defender isn't scanning build directory
3. Enable parallel compilation in MSVC settings
4. Consider SSD storage for faster I/O

## 📋 Windows Requirements

### Development Environment
- **Visual Studio 2022** or **Visual Studio Build Tools 2022**
- **Windows 10/11** (x64)
- **vcpkg** package manager
- **Git** for version control

### Runtime Dependencies (Automatically Managed)
- **SDL2** and **SDL2_image** 
- **libcurl** with **OpenSSL**
- **tinyxml2**
- **Visual C++ Redistributable 2022**

## 🚀 Getting Started on Windows

1. **Clone the repository**:
   ```cmd
   git clone <repository-url>
   cd rouen
   ```

2. **Initialize vcpkg** (if not already done):
   ```cmd
   git submodule update --init --recursive
   ```

3. **Open in VS Code**:
   ```cmd
   code .
   ```

4. **Press F5** to start debugging!

The build system will automatically:
- Install vcpkg dependencies
- Configure CMake with Windows settings
- Compile with MSVC optimizations
- Include Windows icon and resources
- Launch with debug console (Debug builds)

## 📖 Additional Resources

- [WINDOWS_RELEASE_COMPLETE.md](WINDOWS_RELEASE_COMPLETE.md) - Production release information
- [vcpkg.json](vcpkg.json) - Package dependencies
- [cmake/windows.cmake](cmake/windows.cmake) - Windows build configuration
- [resources/rouen.rc](resources/rouen.rc) - Windows resource definitions
