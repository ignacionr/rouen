#!/bin/bash
# Quick install script - copies the built app to ~/Applications without rebuilding

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
INSTALL_DIR="$HOME/Applications"

echo "🚀 Quick Install to $INSTALL_DIR/Rouen.app"
echo "================================================"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Error: Build directory not found at $BUILD_DIR"
    echo "   Please run a build first (e.g., 'nix develop -c sh -c \"cmake --build build -j2\"')"
    exit 1
fi

# Check if rouen.app exists in build directory
if [ ! -d "$BUILD_DIR/rouen.app" ]; then
    echo "❌ Error: rouen.app not found in $BUILD_DIR"
    echo "   Please run a build first (e.g., 'nix develop -c sh -c \"cmake --build build -j2\"')"
    exit 1
fi

# Create Applications directory if it doesn't exist
if [ ! -d "$INSTALL_DIR" ]; then
    echo "📁 Creating $INSTALL_DIR..."
    mkdir -p "$INSTALL_DIR"
fi

# Check if Rouen is running
if pgrep -x "rouen" > /dev/null; then
    echo "⚠️  Warning: Rouen is currently running"
    echo "   Please quit Rouen before installing to avoid issues."
    read -p "   Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "❌ Installation cancelled"
        exit 1
    fi
fi

# Backup existing .env file if it exists
EXISTING_ENV_PATH="$INSTALL_DIR/Rouen.app/Contents/MacOS/.env"
ENV_BACKUP=""
if [ -f "$EXISTING_ENV_PATH" ]; then
    echo "💾 Backing up existing .env configuration..."
    ENV_BACKUP=$(mktemp)
    cp "$EXISTING_ENV_PATH" "$ENV_BACKUP"
fi

# Remove old version if it exists
if [ -d "$INSTALL_DIR/Rouen.app" ]; then
    echo "🗑️  Removing old version..."
    rm -rf "$INSTALL_DIR/Rouen.app"
fi

# Copy the new version
echo "📦 Copying rouen.app from $BUILD_DIR..."
cp -R "$BUILD_DIR/rouen.app" "$INSTALL_DIR/Rouen.app"

# Restore .env file if backup exists
if [ -n "$ENV_BACKUP" ] && [ -f "$ENV_BACKUP" ]; then
    echo "♻️  Restoring .env configuration..."
    cp "$ENV_BACKUP" "$INSTALL_DIR/Rouen.app/Contents/MacOS/.env"
    chmod u+w "$INSTALL_DIR/Rouen.app/Contents/MacOS/.env"
    rm -f "$ENV_BACKUP"
fi

echo ""
echo "✅ Installation complete!"
echo "   Rouen.app installed to: $INSTALL_DIR/Rouen.app"
echo ""
echo "🎯 Next steps:"
echo "   1. Launch Rouen from: $INSTALL_DIR/Rouen.app"
echo "   2. Or use Spotlight: Cmd+Space, type 'Rouen'"
echo ""
