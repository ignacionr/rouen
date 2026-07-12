# Installation Guide

Rouen provides convenient installation options for Windows, macOS, and Linux platforms with automated releases and dependency management.

---

## Windows Installation

### Option 1: MSI Installer (Recommended)
> **✅ User-Mode Installation**: A clean installer with automatic dependency management that does not require administrator privileges.

1. **Download** the latest `.msi` file from the [Releases page](https://github.com/ignacionr/rouen/releases).
2. **Run** the MSI installer.
3. The application installs to your local `AppData` folder and creates Start Menu and Desktop shortcuts.
4. **Launch** Rouen from your Start Menu or Desktop.

### Option 2: Portable ZIP Package
1. **Download** the `rouen-windows-x64.zip` file from the [Releases page](https://github.com/ignacionr/rouen/releases).
2. **Extract** the ZIP archive to any folder on your computer.
3. **Run** `rouen.exe` directly.

---

## macOS Installation

### Option 1: DMG Installer (Recommended)
1. **Download** the latest `.dmg` file from the [Releases page](https://github.com/ignacionr/rouen/releases).
2. **Mount** the DMG by double-clicking it.
3. **Drag** `Rouen.app` into your `/Applications` folder.
4. **Launch** Rouen from your Applications folder or Spotlight.

### Option 2: Automated Nix Build and Install
For developers and advanced users, Rouen provides a script to compile from source using Nix and register the app bundle.

1. **Install Nix** (if not already installed):
   ```bash
   curl -L https://nixos.org/nix/install | sh
   ```
2. **Run the build and install script** from the repository root:
   * **System-wide Installation** (installs to `/Applications/Rouen.app` and `/usr/local/bin/rouen`):
     ```bash
     ./scripts/nix-build-and-install.sh --system
     ```
      *(Requires sudo privileges to write to system directories)*
   * **User-only Installation** (installs to `~/Applications/Rouen.app` and `~/.local/bin/rouen`):
     ```bash
     ./scripts/nix-build-and-install.sh --user
     ```
      *(No root/sudo privileges required)*
   * Existing app-bundle environment config is preserved across reinstalls when present at `Rouen.app/Contents/MacOS/.env`.
   * **Interactive Mode**:
     ```bash
     ./scripts/nix-build-and-install.sh
     ```

---

## Linux Installation

### Portable TAR.GZ Package (Ubuntu 20.04+)
1. **Download** the latest `rouen-linux-x64.tar.gz` archive from the [Releases page](https://github.com/ignacionr/rouen/releases).
2. **Extract** the archive:
   ```bash
   tar -xzf rouen-linux-x64.tar.gz
   ```
3. **Install system dependencies**:
   ```bash
   sudo apt-get update
   sudo apt-get install libx11-6 libgl1-mesa-glx libasound2
   ```
4. **Run** using the launcher script:
   ```bash
   ./run-rouen.sh
   ```

---

## System Requirements

| Platform | Minimum Requirements | Recommended |
| :--- | :--- | :--- |
| **Windows** | Windows 10 (64-bit), DirectX 11 GPU, 4 GB RAM, 100 MB disk space | Windows 11, 8 GB RAM |
| **macOS** | Apple Silicon Mac (M1/M2/M3+), macOS 13.3 (Ventura), 4 GB RAM, 100 MB disk space | macOS 14+, 8 GB RAM |
| **Linux** | Ubuntu 20.04 LTS (or equivalent), X11 Display Server, OpenGL GPU, 4 GB RAM, 100 MB disk space | Ubuntu 22.04+ (or Wayland via XWayland), 8 GB RAM |
