# Windows Build and Release Automation

This document explains the automated Windows build and release process for Rouen.

## GitHub Actions Workflow

The `.github/workflows/windows-release.yml` workflow automatically builds Windows releases when:

1. **A new release is published** - Automatically builds and attaches the Windows binary
2. **Manual trigger** - Can be run manually from the GitHub Actions tab

### What the Workflow Does

1. **Environment Setup**
   - Uses `windows-latest` runner with MSVC toolchain
   - Manually sets up vcpkg package manager (clones and bootstraps)
   - Caches dependencies for faster builds
   - Integrates vcpkg with the build system

2. **Dependency Installation**
   - Installs all required libraries via vcpkg:
     - CURL with SSL support
     - OpenSSL
     - SQLite3
     - SDL2 and SDL2_image
     - TinyXML2

3. **Build Process**
   - Configures CMake with vcpkg toolchain
   - Builds in Release mode with optimizations
   - Uses parallel compilation for speed
   - Includes verbose output for debugging

4. **Packaging**
   - Creates a distribution directory
   - Intelligently finds the executable (handles different build configurations)
   - Copies the executable and all required DLLs from vcpkg
   - Includes assets (images, fonts, sounds)
   - Includes configuration files and documentation
   - Creates a ZIP archive ready for distribution
   - Provides detailed logging of the packaging process

5. **Release Publishing**
   - Uploads the ZIP as a build artifact (always available)
   - For release events: Attaches to the existing GitHub release
   - For manual triggers: Can optionally create a new release using modern `softprops/action-gh-release` action
   - Provides detailed release notes automatically

## Manual Windows Building

If you want to build Windows binaries locally:

### Prerequisites

1. **Visual Studio 2022** with C++ development tools
2. **vcpkg** package manager
3. **Git** for cloning dependencies

### Steps

```powershell
# Install vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install

# Install dependencies
.\vcpkg install curl[ssl] openssl sqlite3 sdl2 sdl2-image tinyxml2 --triplet=x64-windows

# Clone and build Rouen
git clone https://github.com/ignacionr/rouen.git
cd rouen
mkdir build && cd build

# Configure with vcpkg
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows

# Build
cmake --build . --config Release --parallel

# The executable will be in Release\rouen.exe
```

## Windows-Specific Configuration

### CMake Configuration

- `cmake/windows.cmake` - Windows-specific build settings
- `vcpkg.json` - Dependency manifest for vcpkg
- Platform-specific library linking in main CMakeLists.txt

### Key Features

- **GUI Application**: Configured as WIN32_EXECUTABLE (no console window)
- **Modern C++ Standards**: Uses C++23 features with MSVC
- **Runtime Library**: Uses dynamic runtime (DLL)
- **Warning Level**: High warning level (/W4) for code quality
- **Parallel Compilation**: Enabled for faster builds
- **Asset Bundling**: Automatically copies required files post-build

### DLL Handling

The build system automatically:
- Copies all required DLLs from vcpkg to the output directory
- Handles debug vs release DLL variants
- Includes SDL2, OpenSSL, CURL, and other dependency DLLs

## Troubleshooting

### Common Issues

1. **vcpkg baseline errors**: If you see "builtin-baseline was not a valid commit sha"
   - Run `./scripts/update_vcpkg_baseline.sh` to automatically fix this
   - Or manually update the `builtin-baseline` in `vcpkg.json` with a valid commit SHA from vcpkg repository
   
2. **vcpkg not found**: Ensure vcpkg is installed and integrated
   - The workflow handles this automatically
   - For local builds: `git clone https://github.com/Microsoft/vcpkg.git && cd vcpkg && ./bootstrap-vcpkg`

3. **Missing DLLs**: The workflow handles this, but for local builds ensure vcpkg triplet matches

4. **Build failures**: Check that all dependencies installed correctly with vcpkg

### Dependencies

The Windows build requires these external libraries:
- **SDL2** - Window management and input
- **SDL2_image** - Image loading
- **CURL** - HTTP client functionality
- **OpenSSL** - Cryptographic functions
- **SQLite3** - Database functionality
- **TinyXML2** - XML parsing

All are automatically managed by vcpkg in the automated workflow.

## Release Process

1. **Create a Release**: Go to GitHub releases and create a new release
2. **Automatic Build**: The workflow triggers automatically
3. **Download**: The Windows ZIP will be attached to the release
4. **Distribution**: Users can download and run immediately

The resulting package includes everything needed to run Rouen on Windows without additional installations.
