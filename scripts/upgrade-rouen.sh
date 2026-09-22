#!/usr/bin/env bash
# ==============================================================================
# Rouen Resilient Self-Upgrade & Service Restoration Script (POSIX / macOS / Linux)
# ==============================================================================
set -euo pipefail

SOURCE="${1:-}"
INSTALL_DIR="${2:-}"
IS_DETACHED="${3:-}"

if [[ -z "$INSTALL_DIR" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    INSTALL_DIR="$(dirname "$SCRIPT_DIR")"
fi

LOG_FILE="$INSTALL_DIR/rouen-upgrade.log"

log() {
    local level="${2:-INFO}"
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] [$level] $1"
    echo "$msg"
    echo "$msg" >> "$LOG_FILE" 2>/dev/null || true
}

# Detach to background process if not already detached
if [[ "$IS_DETACHED" != "--detached" ]]; then
    echo ""
    echo "========================================================================"
    echo "       ROUEN RESILIENT UPGRADE & RECONNECTION PIPELINE                  "
    echo "========================================================================"
    echo " Target Directory : $INSTALL_DIR"
    echo " Upgrade Source   : ${SOURCE:-Latest Release / Local Package}"
    echo ""
    echo " [NOTICE] Worker running detached in background to prevent lockout."
    echo "          Mesh connection and services will restore automatically."
    echo "========================================================================"
    echo ""
    nohup /bin/bash "$0" "$SOURCE" "$INSTALL_DIR" "--detached" > /dev/null 2>&1 &
    exit 0
fi

log "=== Starting Rouen Detached Upgrade Pipeline ==="
log "Install Directory: $INSTALL_DIR"

STAGE_DIR="$(mktemp -d /tmp/rouen_upgrade_stage_XXXXXX)"
BACKUP_DIR="$INSTALL_DIR/backup_$(date '+%Y%m%d_%H%M%S')"

cleanup() {
    rm -rf "$STAGE_DIR" 2>/dev/null || true
}
trap cleanup EXIT

# 1. Resolve source
if [[ "$SOURCE" =~ ^https?:// ]]; then
    log "Downloading package from $SOURCE..."
    curl -sSL "$SOURCE" -o "$STAGE_DIR/package.zip"
    unzip -q "$STAGE_DIR/package.zip" -d "$STAGE_DIR/extracted"
elif [[ -n "$SOURCE" && -d "$SOURCE" ]]; then
    log "Using local directory source: $SOURCE"
    cp -R "$SOURCE"/* "$STAGE_DIR/"
elif [[ -n "$SOURCE" && -f "$SOURCE" && "$SOURCE" =~ \.zip$ ]]; then
    log "Using local zip file: $SOURCE"
    unzip -q "$SOURCE" -d "$STAGE_DIR/extracted"
elif [[ -n "$SOURCE" && -f "$SOURCE" ]]; then
    log "Using standalone binary: $SOURCE"
    cp "$SOURCE" "$STAGE_DIR/rouen"
    chmod +x "$STAGE_DIR/rouen"
fi

# Locate new binary
NEW_BIN="$(find "$STAGE_DIR" -name "rouen" -type f | head -n 1)"
if [[ -z "$NEW_BIN" || ! -f "$NEW_BIN" ]]; then
    log "Pre-flight validation failed: No valid rouen binary found in upgrade source." "ERROR"
    exit 1
fi
chmod +x "$NEW_BIN"
log "Found staged executable: $NEW_BIN"

# 2. Safety Backup
mkdir -p "$BACKUP_DIR"
log "Creating safety backup at $BACKUP_DIR..."
for f in "$INSTALL_DIR"/rouen "$INSTALL_DIR"/Contents/MacOS/rouen "$INSTALL_DIR"/.env "$INSTALL_DIR"/Contents/Resources/.env; do
    if [[ -f "$f" ]]; then
        cp -a "$f" "$BACKUP_DIR/" 2>/dev/null || true
    fi
done

# 3. Preserve .env configuration
ENV_FILE=""
if [[ -f "$INSTALL_DIR/Contents/Resources/.env" ]]; then
    ENV_FILE="$INSTALL_DIR/Contents/Resources/.env"
elif [[ -f "$INSTALL_DIR/.env" ]]; then
    ENV_FILE="$INSTALL_DIR/.env"
fi

if [[ -n "$ENV_FILE" && -f "$ENV_FILE" ]]; then
    if ! grep -q "ROUEN_MESH_AUTO_CONNECT" "$ENV_FILE"; then
        echo "ROUEN_MESH_AUTO_CONNECT=1" >> "$ENV_FILE"
        log "Appended ROUEN_MESH_AUTO_CONNECT=1 to $ENV_FILE"
    fi
fi

# 4. Gracefully terminate running Rouen
log "Terminating running Rouen instances..."
pkill -x rouen 2>/dev/null || true
sleep 2

# 5. Swap binary
TARGET_BIN="$INSTALL_DIR/rouen"
if [[ -d "$INSTALL_DIR/Contents/MacOS" ]]; then
    TARGET_BIN="$INSTALL_DIR/Contents/MacOS/rouen"
fi

log "Deploying new binary to $TARGET_BIN..."
cp -f "$NEW_BIN" "$TARGET_BIN"
chmod +x "$TARGET_BIN"

# If on macOS, re-sign ad-hoc
if [[ "$(uname)" == "Darwin" ]]; then
    codesign --force --sign - "$TARGET_BIN" 2>/dev/null || true
fi

# 6. Relaunch
log "Launching updated Rouen binary: $TARGET_BIN --mesh..."
nohup "$TARGET_BIN" --mesh > /dev/null 2>&1 &
RELAUNCH_PID=$!

sleep 3
if ps -p "$RELAUNCH_PID" > /dev/null 2>&1; then
    log "=== UPGRADE SUCCESSFUL! Rouen running with PID $RELAUNCH_PID ===" "SUCCESS"
    log "Mesh connection and services restored." "SUCCESS"
else
    log "ERROR: New Rouen binary failed to stay alive! Rolling back..." "ERROR"
    if [[ -f "$BACKUP_DIR/rouen" ]]; then
        cp -f "$BACKUP_DIR/rouen" "$TARGET_BIN"
        chmod +x "$TARGET_BIN"
        nohup "$TARGET_BIN" --mesh > /dev/null 2>&1 &
        log "Rollback succeeded. Previous version restored." "SUCCESS"
    fi
    exit 1
fi
