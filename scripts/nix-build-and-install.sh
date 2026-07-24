#!/usr/bin/env bash

# Rouen Build and Install script for macOS using Nix
# This script builds the Rouen Nix flake and installs the macOS App Bundle
# and the command line binary locally.

set -euo pipefail

# ANSI color codes for pretty output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print helper functions
info() { echo -e "${BLUE}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARNING]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# Usage message
usage() {
    cat << EOF
Usage: $0 [options]

Options:
  -u, --user         Install to user directories (default)
                     App: ~/Applications/Rouen.app
                     CLI: ~/.local/bin/rouen
                     (No sudo/root privileges required)

  -s, --system       Install to system directories
                     App: /Applications/Rouen.app
                     CLI: /usr/local/bin/rouen
                     (Requires sudo privileges)

  -n, --no-build     Skip building and install the existing './result' directory directly.

  -h, --help         Show this help message.

If no options are provided, the script runs interactively if run in a terminal,
defaulting to a user installation.
EOF
    exit 0
}

# Parse command line options
INSTALL_MODE="prompt"
SKIP_BUILD=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -u|--user)
            INSTALL_MODE="user"
            shift
            ;;
        -s|--system)
            INSTALL_MODE="system"
            shift
            ;;
        -n|--no-build)
            SKIP_BUILD=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# 1. Platform Check
info "Checking environment..."
if [[ "$(uname)" != "Darwin" ]]; then
    error "This script is designed for macOS. Rouen on Linux does not use App Bundles."
fi

# 2. Nix Check
if ! command -v nix &> /dev/null; then
    echo ""
    echo -e "${RED}Nix is not installed on this system.${NC}"
    echo "To install Nix, run the official installer:"
    echo "  curl -L https://nixos.org/nix/install | sh"
    echo ""
    error "Nix not found. Please install Nix first."
fi

# 3. Interactive Mode Selection (if prompt)
if [[ "$INSTALL_MODE" == "prompt" ]]; then
    if [[ -t 0 ]]; then
        echo -e "\nWhere would you like to install Rouen?"
        echo "1) User directories (Recommended - no sudo needed)"
        echo "   - App: ~/Applications/Rouen.app"
        echo "   - CLI: ~/.local/bin/rouen"
        echo "2) System directories (Requires sudo)"
        echo "   - App: /Applications/Rouen.app"
        echo "   - CLI: /usr/local/bin/rouen"
        echo "3) Cancel"
        read -rp "Enter choice [1-3]: " choice
        
        case "$choice" in
            1) INSTALL_MODE="user" ;;
            2) INSTALL_MODE="system" ;;
            *) info "Installation cancelled."; exit 0 ;;
        esac
    else
        INSTALL_MODE="user"
    fi
fi

# Set installation paths based on mode
if [[ "$INSTALL_MODE" == "user" ]]; then
    APP_DIR="$HOME/Applications"
    BIN_DIR="$HOME/.local/bin"
    SUDO_CMD=""
    info "Configured for User Installation:"
    echo "  App destination: $APP_DIR/Rouen.app"
    echo "  CLI destination: $BIN_DIR/rouen"
else
    APP_DIR="/Applications"
    BIN_DIR="/usr/local/bin"
    SUDO_CMD="sudo"
    info "Configured for System Installation (requires sudo):"
    echo "  App destination: $APP_DIR/Rouen.app"
    echo "  CLI destination: $BIN_DIR/rouen"
fi

# 4. Build phase
if [[ "$SKIP_BUILD" == "true" ]]; then
    if [[ ! -d "result" ]]; then
        error "No existing './result' directory found. Please run without --no-build."
    fi
    info "Skipping build, installing existing result..."
else
    info "Building Rouen with Nix..."
    # Enable experimental features inline to ensure compatibility
    nix build --extra-experimental-features "nix-command flakes" --cores 2 --max-jobs 2 --print-build-logs
    success "Nix build completed successfully."
fi

# Verify build outputs
SRC_APP="result/Applications/Rouen.app"
SRC_BIN="result/bin/rouen"

if [[ ! -d "$SRC_APP" ]]; then
    error "Build output App Bundle not found at $SRC_APP."
fi

if [[ ! -L "$SRC_BIN" && ! -f "$SRC_BIN" ]]; then
    error "Build output binary not found at $SRC_BIN."
fi

# Resolve actual store path of binary
REAL_BIN_PATH="$(cd result/bin && pwd -P)/rouen"

# 5. Application Installation
info "Installing App Bundle..."
# Ensure destination directories exist
if [[ "$INSTALL_MODE" == "user" ]]; then
    mkdir -p "$APP_DIR"
fi

ENV_BACKUP=""
cleanup_env_backup() {
    if [[ -n "$ENV_BACKUP" && -f "$ENV_BACKUP" ]]; then
        rm -f "$ENV_BACKUP"
    fi
}
trap cleanup_env_backup EXIT

# Backup existing .env file if it exists
EXISTING_ENV_PATH="$APP_DIR/Rouen.app/Contents/MacOS/.env"
if [[ -f "$EXISTING_ENV_PATH" ]]; then
    info "Found existing .env configuration. Backing up..."
    ENV_BACKUP=$(mktemp)
    if [[ -n "$SUDO_CMD" ]]; then
        $SUDO_CMD cp "$EXISTING_ENV_PATH" "$ENV_BACKUP"
        $SUDO_CMD chmod u+w "$ENV_BACKUP"
        $SUDO_CMD chown "$(id -u):$(id -g)" "$ENV_BACKUP" 2>/dev/null || true
    else
        cp "$EXISTING_ENV_PATH" "$ENV_BACKUP"
    fi
fi

# Remove existing installation
if [[ -d "$APP_DIR/Rouen.app" ]]; then
    warn "Removing existing installation at $APP_DIR/Rouen.app..."
    # Previous installs copied from the Nix store may be read-only.
    # Make existing bundle writable/unlocked before deletion.
    $SUDO_CMD chflags -R nouchg "$APP_DIR/Rouen.app" 2>/dev/null || true
    $SUDO_CMD chmod -R u+w "$APP_DIR/Rouen.app" 2>/dev/null || true
    $SUDO_CMD find "$APP_DIR/Rouen.app" -type d -exec chmod u+rwx {} + 2>/dev/null || true
    if ! $SUDO_CMD rm -rf "$APP_DIR/Rouen.app"; then
        error "Failed to remove existing app bundle at $APP_DIR/Rouen.app. Ensure you own it (or run --system)."
    fi
fi

# Copy App Bundle
info "Copying $SRC_APP to $APP_DIR/Rouen.app..."
$SUDO_CMD cp -R "$SRC_APP" "$APP_DIR/"

# Make files writable (copied files from Nix store are read-only)
info "Setting writable permissions on installed App Bundle..."
$SUDO_CMD chmod -R u+w "$APP_DIR/Rouen.app"

# Restore .env file if backup exists
if [[ -n "$ENV_BACKUP" && -f "$ENV_BACKUP" ]]; then
    info "Restoring .env configuration..."
    $SUDO_CMD cp "$ENV_BACKUP" "$APP_DIR/Rouen.app/Contents/MacOS/.env"
    $SUDO_CMD chmod u+w "$APP_DIR/Rouen.app/Contents/MacOS/.env"
fi

success "App Bundle installed to $APP_DIR/Rouen.app"

# 6. CLI Binary Symlink
info "Installing command-line binary..."
# Ensure destination bin directory exists
if [[ "$INSTALL_MODE" == "user" ]]; then
    mkdir -p "$BIN_DIR"
fi

# Link binary
info "Symlinking rouen binary to $BIN_DIR/rouen..."
$SUDO_CMD ln -sfn "$REAL_BIN_PATH" "$BIN_DIR/rouen"

success "CLI binary symlinked to $BIN_DIR/rouen"

# 7. Post-Installation Verification and path checks
echo ""
echo -e "${GREEN}===========================================${NC}"
echo -e "${GREEN}🎉 Rouen has been successfully installed! 🎉${NC}"
echo -e "${GREEN}===========================================${NC}"
echo ""

# Check if bin directory is in PATH
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    warn "The directory $BIN_DIR is not in your PATH."
    echo "To run the 'rouen' command from your terminal, add it to your shell configuration file."
    
    # Determine the user's shell configuration file
    SHELL_NAME=$(basename "$SHELL")
    if [[ "$SHELL_NAME" == "zsh" ]]; then
        CONF_FILE="$HOME/.zshrc"
    elif [[ "$SHELL_NAME" == "bash" ]]; then
        CONF_FILE="$HOME/.bash_profile"
        [[ ! -f "$CONF_FILE" ]] && CONF_FILE="$HOME/.bashrc"
    else
        CONF_FILE=""
    fi
    
    if [[ -n "$CONF_FILE" ]]; then
        echo -e "You can do this by running:"
        echo -e "  ${BLUE}echo 'export PATH=\"\$PATH:$BIN_DIR\"' >> $CONF_FILE${NC}"
        echo -e "Then restart your terminal or run: ${BLUE}source $CONF_FILE${NC}"
    else
        echo -e "Add ${BLUE}export PATH=\"\$PATH:$BIN_DIR\"${NC} to your shell profile."
    fi
    echo ""
fi

info "You can open Rouen in two ways:"
echo -e "  1. From Finder/Launchpad: Click on ${BLUE}Rouen${NC} in your ${BLUE}$APP_DIR${NC} folder."
echo -e "  2. From Terminal: Run ${BLUE}rouen${NC} (once PATH is set up)."
echo ""
