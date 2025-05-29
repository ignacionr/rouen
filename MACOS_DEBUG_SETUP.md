# macOS VS Code Debugging Setup - Complete Guide

## 🎯 Quick Start (Press F5)

The VS Code configuration is now optimized for seamless debugging on macOS. Here's what happens when you press **F5**:

### Primary Debug Configuration: "🚀 Debug Rouen (vcpkg)"
1. **Automatic Dependency Installation**: vcpkg dependencies are installed if needed
2. **CMake Configuration**: Project configures with Debug symbols and vcpkg toolchain  
3. **Build**: Project builds with full debug information
4. **Launch**: Debugger launches with LLDB and proper environment variables

## 🛠️ Available Debug Configurations

### 1. 🚀 Debug Rouen (vcpkg) - *Recommended*
- **Best for**: Normal debugging workflow
- **What it does**: Builds and launches with full dependency management
- **Use when**: You want reliable debugging with all dependencies handled

### 2. 🔧 Debug Rouen (Quick Launch)  
- **Best for**: When binary already exists and you just want to debug
- **What it does**: Launches existing binary without rebuilding
- **Use when**: You've already built and just want to run the debugger quickly

### 3. 🔧 Debug Rouen (Full Setup)
- **Best for**: Complete rebuild from scratch with optimal debug flags
- **What it does**: Runs full dependency install → configure → build → launch sequence
- **Use when**: You want maximum debug information or after major changes

### 4. 📦 Debug Rouen (Traditional Build)
- **Best for**: System dependency debugging
- **What it does**: Uses system-installed libraries instead of vcpkg
- **Use when**: Debugging against system libraries or troubleshooting vcpkg issues

### 5. 🍎 Debug Installed Rouen
- **Best for**: Debugging the installed application
- **What it does**: Debugs the version installed in /Applications
- **Use when**: Testing the final installed application

## 🔧 Build Tasks Available

| Task | Purpose | When to Use |
|------|---------|-------------|
| **Build with vcpkg** | Standard build with vcpkg | Default F5 behavior |
| **Build Debug with vcpkg** | Optimized debug build | Maximum debug info needed |
| **Quick Debug Setup** | Full dependency + build | First time or after clean |
| **Configure Debug with vcpkg** | Setup debug configuration | Manual configuration needed |
| **Install vcpkg dependencies** | Install dependencies only | Dependency issues |

## ⚡ Keyboard Shortcuts

- **F5**: Start Debugging (uses primary configuration)
- **Ctrl+F5**: Run Without Debugging  
- **Shift+F5**: Stop Debugging
- **F9**: Toggle Breakpoint
- **F10**: Step Over
- **F11**: Step Into  
- **Shift+F11**: Step Out

## 🔍 Debugging Features Enabled

### LLDB Configuration
- **Pretty printing**: Enhanced variable display
- **Inline values**: See variable values in editor
- **Breakpoint optimization**: Better breakpoint handling
- **Smart stepping**: Improved step through code experience

### Environment Setup
- **DYLD_LIBRARY_PATH**: Proper library loading for vcpkg libraries
- **DYLD_FALLBACK_LIBRARY_PATH**: System library fallback paths
- **Working Directory**: Set to workspace root for resource access

### VS Code Integration  
- **Compile Commands**: Full IntelliSense with `compile_commands.json`
- **C++23 Standard**: Latest C++ standard support
- **Problem Matchers**: Automatic error detection and navigation
- **Task Dependencies**: Automatic dependency resolution

## 🚨 Troubleshooting

### "sqlite3 package not found" Error
- **Solution**: Run "Install vcpkg dependencies" task first
- **Alternative**: Use "🔧 Debug Rouen (Full Setup)" configuration

### Debugger Won't Start
- **Check**: Binary exists at `build-vcpkg/rouen.app/Contents/MacOS/rouen`
- **Solution**: Run "Build with vcpkg" task manually first
- **Alternative**: Use "🔧 Debug Rouen (Quick Launch)" if binary exists

### Breakpoints Not Hitting
- **Check**: Using Debug build configuration (not Release)
- **Solution**: Use "Configure Debug with vcpkg" task for maximum debug info
- **Verify**: Check that `-g -O0 -DDEBUG` flags are applied

### Library Loading Issues
- **Check**: DYLD_LIBRARY_PATH includes `build-vcpkg` directory
- **Solution**: Environment variables are automatically set in launch configurations
- **Alternative**: Use traditional build if vcpkg libraries cause issues

## 🎯 Recommended Workflow

### First Time Setup
1. Press **F5** → Choose "🚀 Debug Rouen (vcpkg)"
2. Wait for dependencies to install and build to complete  
3. Set breakpoints and start debugging

### Daily Development  
1. Press **F5** → Uses your last selected configuration
2. For quick iterations: Use "🔧 Debug Rouen (Quick Launch)"
3. For full rebuilds: Use "🔧 Debug Rouen (Full Setup)"

### After Major Changes
1. Run "Clean vcpkg build" task if needed
2. Use "🔧 Debug Rouen (Full Setup)" configuration  
3. Or manually run "Install vcpkg dependencies" → "Configure Debug with vcpkg" → "Build Debug with vcpkg"

## ✅ Configuration Validation

The setup has been tested and verified:
- ✅ vcpkg dependencies install correctly
- ✅ CMake configures with proper debug flags  
- ✅ Build completes successfully with debug symbols
- ✅ Debugger launches and can set breakpoints
- ✅ Environment variables properly configured
- ✅ All task dependencies resolved automatically

**Status**: Ready for seamless F5 debugging on macOS! 🚀
