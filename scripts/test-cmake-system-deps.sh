#!/bin/bash
# Test CMake configuration with system dependencies

echo "=== TESTING CMAKE CONFIGURATION WITH SYSTEM DEPENDENCIES ==="

# Create a test build directory
TEST_BUILD_DIR="build-test-system"
rm -rf "$TEST_BUILD_DIR"
mkdir -p "$TEST_BUILD_DIR"

cd "$TEST_BUILD_DIR"

# Set environment for system dependencies (simulate what our script does)
export OPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl"
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="/opt/homebrew:/opt/homebrew/opt/openssl:$CMAKE_PREFIX_PATH"

echo "Environment variables set:"
echo "  OPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR"
echo "  PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
echo "  CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH"
echo ""

# Test CMake configuration without vcpkg
echo "Testing CMake configuration without vcpkg toolchain..."
if cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_FIND_FRAMEWORK=LAST \
  -DCMAKE_FIND_APPBUNDLE=LAST \
  -DOPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl" \
  -DCURL_ROOT="/opt/homebrew" \
  -DSDL2_DIR="/opt/homebrew/lib/cmake/SDL2"; then
  
  echo "✅ CMake configuration successful"
  
  # Check if key variables were set correctly
  echo ""
  echo "Key CMake variables:"
  grep -E "(CMAKE_TOOLCHAIN_FILE|SQLite3_LIBRARIES|TINYXML2_)" CMakeCache.txt | head -10 || echo "Variables not found in cache"
  
else
  echo "❌ CMake configuration failed"
  echo ""
  echo "CMake error output:"
  cat CMakeFiles/CMakeError.log 2>/dev/null || echo "No CMakeError.log found"
fi

cd ..
rm -rf "$TEST_BUILD_DIR"

echo ""
echo "Test completed."
