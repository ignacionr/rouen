#!/bin/bash
set -e

# macOS ARM64 Build Setup Script
# This script handles vcpkg installation with fallback to system dependencies

WORKSPACE="${GITHUB_WORKSPACE:-$(pwd)}"
VCPKG_DIR="$WORKSPACE/vcpkg"
VCPKG_INSTALLED_DIR="$WORKSPACE/vcpkg_installed"

echo "=== MACOS ARM64 BUILD SETUP ==="
echo "Workspace: $WORKSPACE"
echo "vcpkg Directory: $VCPKG_DIR"
echo "Installed Directory: $VCPKG_INSTALLED_DIR"

# Set environment variables for ARM64
export MACOSX_DEPLOYMENT_TARGET=11.0
export CMAKE_OSX_ARCHITECTURES=arm64
export CC=clang
export CXX=clang++
export CFLAGS="-arch arm64 -mmacosx-version-min=11.0"
export CXXFLAGS="-arch arm64 -mmacosx-version-min=11.0 -std=c++23"
export LDFLAGS="-arch arm64"

# Function to install system dependencies
install_system_deps() {
    echo "Installing system dependencies with Homebrew..."
    if ! command -v brew >/dev/null 2>&1; then
        echo "ERROR: Homebrew not available"
        return 1
    fi
    
    brew update
    brew install openssl curl sqlite sdl2 sdl2_image tinyxml2 pkg-config
    
    # Set up environment for system dependencies
    export OPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl"
    export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
    export CMAKE_PREFIX_PATH="/opt/homebrew:/opt/homebrew/opt/openssl:$CMAKE_PREFIX_PATH"
    
    echo "OPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl" >> $GITHUB_ENV
    echo "PKG_CONFIG_PATH=/opt/homebrew/opt/openssl/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH" >> $GITHUB_ENV
    echo "CMAKE_PREFIX_PATH=/opt/homebrew:/opt/homebrew/opt/openssl:$CMAKE_PREFIX_PATH" >> $GITHUB_ENV
}

# Function to setup vcpkg
setup_vcpkg() {
    echo "Setting up vcpkg..."
    
    # Clean up any existing installation
    rm -rf "$VCPKG_DIR" "$VCPKG_INSTALLED_DIR"
    
    # Clone and bootstrap vcpkg
    git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_DIR"
    cd "$VCPKG_DIR"
    ./bootstrap-vcpkg.sh
    
    if [ ! -f "vcpkg" ]; then
        echo "ERROR: vcpkg executable not created"
        return 1
    fi
    
    echo "vcpkg bootstrapped successfully"
    return 0
}

# Function to install vcpkg packages
install_vcpkg_packages() {
    echo "Installing vcpkg packages..."
    cd "$VCPKG_DIR"
    
    # Use custom triplet if available
    triplet_arg="arm64-osx"
    if [ -f "$WORKSPACE/arm64-osx-custom.cmake" ]; then
        cp "$WORKSPACE/arm64-osx-custom.cmake" "$VCPKG_DIR/triplets/community/"
        triplet_arg="arm64-osx-custom"
    fi
    
    # Try with macOS-specific manifest first
    if [ -f "$WORKSPACE/vcpkg-macos.json" ]; then
        echo "Using macOS-specific manifest..."
        cp "$WORKSPACE/vcpkg-macos.json" "$WORKSPACE/vcpkg.json"
    fi
    
    # Install with retries
    local attempt=1
    local max_attempts=2
    
    while [ $attempt -le $max_attempts ]; do
        echo "Installation attempt $attempt of $max_attempts..."
        
        if ./vcpkg install --triplet "$triplet_arg" --x-install-root="$VCPKG_INSTALLED_DIR"; then
            echo "✅ vcpkg installation successful"
            ./vcpkg integrate install
            return 0
        else
            echo "⚠️ vcpkg installation failed on attempt $attempt"
            if [ $attempt -eq $max_attempts ]; then
                return 1
            fi
            attempt=$((attempt + 1))
            sleep 5
        fi
    done
    
    return 1
}

# Main execution
echo "Step 1: Setting up vcpkg..."
if ! setup_vcpkg; then
    echo "❌ vcpkg setup failed"
    exit 1
fi

echo "Step 2: Installing packages..."
if install_vcpkg_packages; then
    echo "✅ vcpkg packages installed successfully"
    
    # Verify installation
    cd "$VCPKG_DIR"
    echo "Installed packages:"
    ./vcpkg list
else
    echo "⚠️ vcpkg package installation failed, falling back to system dependencies..."
    
    if install_system_deps; then
        echo "✅ System dependencies installed successfully"
        
        # Install minimal vcpkg packages that don't conflict
        cd "$VCPKG_DIR"
        echo "Installing minimal vcpkg packages..."
        if ./vcpkg install tinyxml2 glaze gtest --triplet arm64-osx --x-install-root="$VCPKG_INSTALLED_DIR"; then
            echo "✅ Minimal vcpkg packages installed"
            ./vcpkg integrate install
        else
            echo "⚠️ Even minimal vcpkg installation failed"
        fi
    else
        echo "❌ System dependency installation also failed"
        exit 1
    fi
fi

echo ""
echo "=== SETUP COMPLETE ==="
echo "vcpkg directory: $VCPKG_DIR"
echo "Installed packages directory: $VCPKG_INSTALLED_DIR"
echo "Environment variables set for ARM64 build"
echo ""
