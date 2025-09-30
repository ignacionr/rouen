# Environment Setup for Rouen Project

This document describes how to properly set up your environment to work with the Rouen project, including secrets management and development environment provisioning.

## Overview

The Rouen project uses multiple environment configuration approaches to support different development workflows:

1. **Global secrets file** (`~/.secrets`) - For sensitive data like API keys
2. **Project .env file** (`/Users/inz/src/rouen/.env`) - For project-specific configuration
3. **Nix environment** - For reproducible development dependencies
4. **Shell profile integration** - For automatic environment setup

## Files Structure

```
~/.secrets                              # Global secrets (user-specific)
/Users/inz/src/rouen/.env              # Project environment variables
/Users/inz/src/rouen/scripts/
├── source-secrets.sh                   # Manual secret loading script
├── setup-environment.sh               # Comprehensive environment setup
└── nix-setup-secrets.sh               # Nix-specific environment setup
```

## Setup Instructions

### 1. Global Secrets File

Ensure your `~/.secrets` file contains the required environment variables:

```bash
OPENWEATHER_KEY=043320c8c1f9062188bd1a6f484f7b39
```

### 2. Project Environment File

Create a local `.env` file from the template:

```bash
# Copy the template (first time setup)
cp .env.template .env

# Edit with your actual values
# The .env file should contain:
GIT_PATH=/usr/bin/git
OPENWEATHER_KEY=your_actual_api_key_here
```

**Important:** The `.env` file is gitignored and contains your actual secrets. Never commit this file to version control.

### 3. Shell Profile Integration

Add the following to your shell profile (`.zshrc`, `.bash_profile`, etc.):

```bash
# Function to automatically load Rouen environment when entering project directory
auto_load_rouen_env() {
    if [[ -f "$(pwd)/CMakeLists.txt" ]] && grep -q "project(rouen" "$(pwd)/CMakeLists.txt" 2>/dev/null; then
        if [[ -f "$(pwd)/scripts/source-secrets.sh" ]]; then
            echo "Auto-loading Rouen environment..."
            source "$(pwd)/scripts/source-secrets.sh"
        fi
    fi
}

# Optional: Auto-run when changing directories
# chpwd() { auto_load_rouen_env }  # For zsh
# PROMPT_COMMAND="auto_load_rouen_env; $PROMPT_COMMAND"  # For bash
```

### 4. Manual Environment Loading

You can manually load the environment at any time:

```bash
# From the project root directory
source scripts/source-secrets.sh
```

### 5. Nix Development Environment

When using Nix (recommended for reproducible builds):

```bash
# Enter Nix shell (automatically loads secrets)
nix-shell

# Or with flakes
nix develop
```

The Nix environment is configured to automatically:
- Load secrets from `~/.secrets`
- Load project environment from `.env`
- Set up all required development dependencies
- Configure the build environment

## Environment Validation

To verify your environment is properly configured:

```bash
# Manual validation
source scripts/source-secrets.sh

# Check specific variables
echo "OPENWEATHER_KEY: $OPENWEATHER_KEY"
echo "GIT_PATH: $GIT_PATH"

# Full validation with Nix
nix-shell --run "echo 'OPENWEATHER_KEY:' $OPENWEATHER_KEY"
```

## Build Integration

The environment variables are automatically available to:

1. **CMake configuration** - Through the config service
2. **Runtime application** - Via the ConfigService (supports .env files and environment variables)
3. **Build scripts** - All shell scripts inherit the environment

### Smart .env File Discovery

The application uses intelligent .env file discovery to work in both development and production:

1. **Executable directory** - For deployed applications
2. **Current working directory** - For development runs
3. **Parent directories** - For running from build directories (up to 3 levels)

This means the application will find your `.env` file whether you run it from:
- The project root: `./build/rouen`
- A build directory: `cd build && ./rouen`
- As a deployed app bundle: `./rouen.app/Contents/MacOS/rouen`

## Security Notes

- The `~/.secrets` file should have restricted permissions: `chmod 600 ~/.secrets`
- Never commit the `.secrets` file to version control
- The project `.env` file may contain non-sensitive overrides and can be committed
- Sensitive values are masked in debug output by the config service

## Troubleshooting

### Environment Variables Not Loading

1. Check file permissions: `ls -la ~/.secrets`
2. Verify file format (no spaces around `=`)
3. Ensure you're sourcing the scripts correctly
4. Check for shell-specific issues

### Nix Environment Issues

1. Ensure Nix is properly installed
2. Try rebuilding the environment: `nix-shell --pure`
3. Check for flake updates: `nix flake update`

### Build Issues

1. Verify all environment variables are loaded
2. Check the config service logs
3. Ensure the `.env` file is in the project root

## Best Practices

1. **Always validate environment** before building
2. **Use Nix for reproducible builds** in CI/CD
3. **Keep secrets separate** from project configuration
4. **Document any new required variables** in this file
5. **Test environment setup** on fresh systems