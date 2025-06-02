# VS Code Configuration for Rouen

This directory contains VS Code-specific configuration files that provide a comprehensive development environment for the Rouen project with full support for both vcpkg and traditional dependency management.

## Configuration Files

### `tasks.json` - Build Tasks

Provides multiple build tasks accessible via **Cmd+Shift+P → "Tasks: Run Task"**:

#### vcpkg Tasks (Recommended)
- **"Build with vcpkg"** ⭐ - Default build task (Cmd+Shift+B)
- **"Configure with vcpkg"** - Configure Debug build with vcpkg
- **"Configure with vcpkg (Release)"** - Configure Release build with vcpkg  
- **"Build with vcpkg (Release)"** - Build Release version
- **"Clean vcpkg build"** - Remove build-vcpkg directory
- **"Install vcpkg dependencies"** - Install/update dependencies from manifest
- **"Update vcpkg baseline"** - Update vcpkg to latest baseline
- **"Run Rouen (vcpkg)"** - Build and launch the application

#### Traditional Build Tasks
- **"Configure traditional build"** - Configure with system dependencies
- **"Build traditional"** - Build using system packages
- **"CMake: build"** - Generic CMake build
- **"CMake: configure"** - Generic CMake configure

### `launch.json` - Debug Configurations

Multiple debug configurations accessible via **F5** or **Run and Debug**:

#### Primary Configurations
- **"🚀 Debug Rouen (vcpkg)"** ⭐ - Debug vcpkg build with pre-build task (recommended)
- **"🔧 Debug Rouen (Quick Launch)"** - Debug without building (fastest startup)
- **"🔧 Debug Rouen (Full Setup)"** - Complete debug setup with dependencies
- **"📦 Debug Rouen (Traditional Build)"** - Debug traditional build
- **"🍎 Debug Installed Rouen"** - Debug installed app from /Applications

All configurations use LLDB debugger and include proper environment setup for macOS.

#### Debugging Setup
The debug configurations are set up to work out of the box. If F5 debugging fails:

1. **Quick Fix**: Use "🔧 Debug Rouen (Quick Launch)" which skips pre-build tasks
2. **Build First**: Run "Build with vcpkg" task manually (Cmd+Shift+P → Tasks: Run Task)
3. **Verify Executable**: Ensure `build-vcpkg/rouen.app/Contents/MacOS/rouen` exists
4. **Check Dependencies**: Run "Install vcpkg dependencies" if needed

#### Debug Features
- **Breakpoints**: Set breakpoints anywhere in source code
- **Variable Inspection**: Hover over variables or use Debug Console
- **Call Stack**: View function call hierarchy
- **Debug Console**: Execute LLDB commands directly

### `c_cpp_properties.json` - IntelliSense

Platform and build-system specific IntelliSense configurations:

#### Configurations Available
- **"macOS-vcpkg"** ⭐ - ARM64 macOS with vcpkg includes (recommended)
- **"macOS-traditional"** - macOS with system includes
- **"Linux-vcpkg"** ⭐ - Linux with vcpkg includes (recommended)  
- **"Linux-traditional"** - Linux with system includes

Features:
- C++23 standard support
- Proper include paths for vcpkg and system dependencies
- Architecture-specific settings (ARM64 for macOS, x64 for Linux)
- Compile commands integration for accurate IntelliSense

### `settings.json` - Workspace Settings

Optimized settings for C++ development:

#### Key Configurations
- **Default build directory**: `build-vcpkg`
- **CMake toolchain**: Automatically configured for vcpkg
- **Include paths**: Prioritizes vcpkg dependencies
- **File associations**: Comprehensive C++ file type mappings
- **IntelliSense**: Enhanced C++ language support

## Quick Start Guide

### 1. Build the Project
```
Cmd+Shift+P → "Tasks: Run Task" → "Build with vcpkg"
```
Or use the default build shortcut: **Cmd+Shift+B**

### 2. Debug the Application
```
F5 → Select "(lldb) Launch macOS - vcpkg build"
```

### 3. Switch Build Configurations
- **Debug**: Use "Configure with vcpkg" task
- **Release**: Use "Configure with vcpkg (Release)" task
- **Clean**: Use "Clean vcpkg build" task

### 4. Update Dependencies
```
Cmd+Shift+P → "Tasks: Run Task" → "Install vcpkg dependencies"
```

## Architecture Support

### macOS
- **Primary**: ARM64 (Apple Silicon) via `arm64-osx` triplet
- **Legacy**: x64 (Intel) support available
- **Debug**: LLDB integration with proper DYLD paths

### Linux  
- **Primary**: x64 via `x64-linux` triplet
- **Debug**: GDB integration with pretty-printing
- **Alternative**: ARM64 support available

### Windows
- **Support**: x64, x86, ARM64 via appropriate triplets
- **Build**: Automated via GitHub Actions
- **Debug**: MSVC debugger support (when on Windows)

## Troubleshooting

### Build Issues
1. **Missing dependencies**: Run "Install vcpkg dependencies" task
2. **Stale cache**: Run "Clean vcpkg build" then "Configure with vcpkg"
3. **Baseline errors**: Run "Update vcpkg baseline" task

### IntelliSense Issues
1. **Wrong includes**: Switch to appropriate configuration in status bar
2. **Missing symbols**: Rebuild compile_commands.json via configure task
3. **Architecture mismatch**: Verify correct platform configuration selected

### Debug Issues
1. **App won't launch**: Ensure build completed successfully
2. **Missing symbols**: Use Debug build configuration
3. **Path issues**: Check DYLD_LIBRARY_PATH in launch configuration

## Best Practices

### Development Workflow
1. Use vcpkg build system (recommended)
2. Configure Debug builds for development
3. Use Release builds for distribution
4. Run "Clean vcpkg build" when switching between Debug/Release

### Code Quality
- All configurations enforce C++23 standards
- IntelliSense provides real-time error checking
- Compiler warnings treated as errors
- Automatic code formatting and style checks

### Cross-Platform Development
- Test builds on multiple platforms using appropriate configurations
- Use platform-specific debug configurations
- Leverage GitHub Actions for automated testing

## Related Documentation

- [Main README.md](../README.md) - Project overview and build instructions
- [Windows Build Guide](../docs/windows-build.md) - Windows-specific setup
- [vcpkg.json](../vcpkg.json) - Dependency manifest
- [vcpkg-configuration.json](../vcpkg-configuration.json) - Platform settings

---

This VS Code setup provides a production-ready development environment with comprehensive tooling support for building, debugging, and maintaining the Rouen application across all supported platforms.
