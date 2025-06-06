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

Optimized settings for C++ development and testing:

#### Key Configurations
- **Default build directory**: `build-vcpkg`
- **CMake toolchain**: Automatically configured for vcpkg
- **Include paths**: Prioritizes vcpkg dependencies
- **File associations**: Comprehensive C++ file type mappings
- **IntelliSense**: Enhanced C++ language support
- **Test Explorer**: Native VS Code test discovery and execution
- **CTest Integration**: Parallel test execution with verbose output
- **Search Settings**: Include test build directories for comprehensive search

## 🧪 Testing Integration

The VS Code configuration includes comprehensive testing support with Google Test integration, individual test debugging, and automated test execution.

### Test Tasks

Access via **Cmd+Shift+P → "Tasks: Run Task"**:

#### Test Build and Configuration
- **"Configure Tests"** - Set up test build environment with vcpkg
- **"Build Tests"** ⭐ - Build all test executables (default test task)
- **"Clean Test Build"** - Remove test build artifacts

#### Test Execution
- **"Run All Tests"** - Complete test suite (Google Test + Legacy tests)
- **"Run Google Tests Only"** - Google Test framework tests only
- **"Run Legacy Tests Only"** - Original console-based tests
- **"Run CTest"** - CTest framework execution with verbose output

#### Individual Test Suites
- **"Run HTTP/SSL Tests"** - Execute HTTP/SSL configuration tests
- **"Run Math Operations Tests"** - Execute advanced math operations tests
- **"Run Bybit Currency Tests"** - Execute Bybit currency conversion tests

### Test Debug Configurations

Access via **F5** or **Run and Debug panel**:

#### Google Test Debugging (macOS)
- **"🧪 Debug HTTP/SSL Tests"** - Debug SSL configuration and HTTP tests
- **"🧮 Debug Math Operations Tests"** - Debug advanced math operations with Google Mock
- **"💰 Debug Bybit Currency Tests"** - Debug currency conversion functionality
- **"📊 Debug Example Math Tests"** - Debug basic math operations

#### Windows Test Debugging
- All test configurations have Windows equivalents using `cppvsdbg` debugger
- Automatic environment setup for Windows testing

#### Test Debug Features
- **Environment Variables**: `ROUEN_LOG_LEVEL=DEBUG` for verbose test logging
- **Library Paths**: Proper library path configuration for test dependencies
- **Pretty Printing**: LLDB/MSVC pretty-printing enabled for test objects
- **Pre-launch Tasks**: Automatic test building before debugging
- **Breakpoint Support**: Full breakpoint support in test code and source code

### Testing Workflow

#### 1. Quick Test Execution
```bash
# Build and run all tests
Cmd+Shift+P → "Tasks: Run Task" → "Run All Tests"

# Or build tests first, then run
Cmd+Shift+P → "Tasks: Run Task" → "Build Tests"
Cmd+Shift+P → "Tasks: Run Task" → "Run Google Tests Only"
```

#### 2. Test Debugging
1. **Set breakpoints** in test files or source code
2. **Select debug configuration**: `🧪 Debug HTTP/SSL Tests`
3. **Start debugging**: Press `F5`
4. **Step through code**: Use `F10`, `F11`, `F5` for debugging

#### 3. Individual Test Execution
```bash
# Run specific test suite
Cmd+Shift+P → "Tasks: Run Task" → "Run Math Operations Tests"

# Or use terminal in build-tests directory
cd build-tests
./test_fetch_ssl --gtest_filter="*SSLMode*"
```

### Test Configuration Features

#### Environment Variables
Available in all test debug configurations:
- `ROUEN_LOG_LEVEL=DEBUG` - Enhanced logging for test diagnosis
- `ROUEN_SSL_MODE=relaxed` - SSL configuration for testing
- `ROUEN_SSL_VERIFY_PEER=true` - SSL peer verification control

#### Google Test Integration
- **Test Discovery**: Automatic test detection and listing
- **Parameterized Tests**: Support for data-driven testing
- **Mock Objects**: Google Mock integration for unit testing
- **Death Tests**: Testing expected crashes and exceptions
- **Performance Tests**: Timing validation for performance-critical code

#### CTest Integration
- **Parallel Execution**: Multi-threaded test execution
- **XML Output**: Test results in standardized format
- **Failure Reporting**: Detailed failure information with file/line numbers
- **Test Filtering**: Run specific test suites or individual tests

### Adding New Tests

#### 1. Create Test File
```cpp
// tests/test_new_feature.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "your_module.hpp"

TEST(NewFeatureTest, BasicFunctionality) {
    EXPECT_EQ(expected_value, actual_value);
}
```

#### 2. Add to CMakeLists.txt
```cmake
# For tests with HTTP dependencies
add_gtest_http_executable(test_new_feature test_new_feature.cpp)

# For tests without HTTP dependencies  
add_gtest_executable(test_new_feature test_new_feature.cpp)
```

#### 3. Add VS Code Task (Optional)
```json
{
    "type": "shell",
    "label": "Run New Feature Tests",
    "command": "./test_new_feature",
    "options": { "cwd": "${workspaceFolder}/build-tests" },
    "group": "test",
    "dependsOn": "Build Tests"
}
```

#### 4. Add Debug Configuration (Optional)
```json
{
    "name": "🆕 Debug New Feature Tests",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build-tests/test_new_feature",
    "environment": [
        { "name": "ROUEN_LOG_LEVEL", "value": "DEBUG" }
    ],
    "MIMode": "lldb",
    "preLaunchTask": "Build Tests"
}
```

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
