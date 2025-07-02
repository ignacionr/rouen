#!/bin/bash
# Test script for Rouen Nix migration
# This script verifies that the migration works correctly

set -e

echo "=== Testing Rouen Nix Migration ==="
echo ""

# Clean any existing builds
echo "1. Cleaning existing builds..."
rm -rf build-nix build-test-* || true

echo "2. Testing normal build (should work)..."
nix-shell --run "cmake -B build-normal -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug"
echo "✓ Normal build configuration succeeded"

echo "3. Building project..."
nix-shell --run "cmake --build build-normal --parallel"
echo "✓ Normal build succeeded"

echo "4. Testing network isolation (should fail fast)..."
if nix-shell --run "cmake -B build-isolated -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DFETCHCONTENT_FULLY_DISCONNECTED=ON" 2>&1 | grep -q "ImGui requires FetchContent but FETCHCONTENT_FULLY_DISCONNECTED=ON"; then
    echo "✓ Network isolation test succeeded (failed as expected with clear error)"
else
    echo "✗ Network isolation test failed (should have failed with clear error message)"
    exit 1
fi

echo "5. Verifying executable..."
if [[ -f "build-normal/rouen.app/Contents/MacOS/rouen" ]]; then
    echo "✓ macOS executable found"
elif [[ -f "build-normal/rouen" ]]; then
    echo "✓ Linux executable found"
else
    echo "✗ No executable found"
    exit 1
fi

echo "6. Cleaning up..."
rm -rf build-normal build-isolated

echo ""
echo "=== All Tests Passed! ==="
echo "✓ Nix migration is working correctly"
echo "✓ Normal builds work with FetchContent"
echo "✓ Network isolation fails fast with clear error"
echo "✓ CI will behave correctly"
