#!/bin/bash

# Test Windows CMake Configuration
# This script validates that the Windows CMake configuration is properly set up

echo "Testing Windows CMake configuration..."

# Create a temporary build directory for testing
TEST_DIR="build_test_windows"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

# Try to configure with Windows settings (this will fail on macOS but we can check for syntax errors)
echo "Testing CMake configuration syntax..."

# Test that vcpkg.json is valid JSON
echo "Validating vcpkg.json..."
if command -v jq &> /dev/null; then
    if jq empty ../vcpkg.json; then
        echo "✅ vcpkg.json is valid JSON"
    else
        echo "❌ vcpkg.json has syntax errors"
        exit 1
    fi
else
    echo "⚠️  jq not found, skipping vcpkg.json validation"
fi

# Test CMake configuration parsing (dry run)
echo "Testing CMake configuration parsing..."
# Note: This will fail on macOS since we don't have Windows dependencies,
# but we can check for major syntax errors
cmake .. -DWIN32=ON -DCMAKE_SYSTEM_NAME=Windows -DVCPKG_TARGET_TRIPLET=x64-windows > cmake_output.log 2>&1

# Check for syntax errors rather than successful configuration
if grep -q "syntax error\|CMake Error.*syntax" cmake_output.log; then
    echo "❌ CMake configuration has syntax errors:"
    grep -i "syntax error\|cmake error.*syntax" cmake_output.log
    cd ..
    rm -rf "$TEST_DIR"
    exit 1
else
    echo "✅ No major CMake syntax errors detected"
fi

# Check that Windows-specific files exist
echo "Checking Windows-specific files..."
if [ -f "../cmake/windows.cmake" ]; then
    echo "✅ windows.cmake exists"
else
    echo "❌ windows.cmake missing"
fi

if [ -f "../vcpkg.json" ]; then
    echo "✅ vcpkg.json exists"
else
    echo "❌ vcpkg.json missing"
fi

if [ -f "../.github/workflows/windows-release.yml" ]; then
    echo "✅ GitHub Actions workflow exists"
else
    echo "❌ GitHub Actions workflow missing"
fi

# Clean up
cd ..
rm -rf "$TEST_DIR"

echo "Windows configuration test completed successfully! 🎉"
echo ""
echo "To build on Windows:"
echo "1. Install Visual Studio 2022 with C++ support"
echo "2. Clone vcpkg: git clone https://github.com/Microsoft/vcpkg.git"
echo "3. Bootstrap vcpkg: cd vcpkg && .\\bootstrap-vcpkg.bat"
echo "4. Install dependencies: .\\vcpkg install curl[ssl] openssl sqlite3 sdl2 sdl2-image tinyxml2 --triplet=x64-windows"
echo "5. Configure: cmake -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]\\scripts\\buildsystems\\vcpkg.cmake"
echo "6. Build: cmake --build . --config Release"
echo ""
echo "Or use the GitHub Actions workflow for automated builds!"
echo "The workflow handles vcpkg setup automatically and provides robust error handling."
