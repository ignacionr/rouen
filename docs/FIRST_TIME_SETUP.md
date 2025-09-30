# First-time Setup for New Developers

Welcome to the Rouen project! This guide will help you get your development environment set up quickly.

## Quick Start

1. **Clone the repository** (if you haven't already):
   ```bash
   git clone https://github.com/ignacionr/rouen.git
   cd rouen
   ```

2. **Run the quick setup script**:
   ```bash
   ./scripts/quick-setup.sh
   ```
   This interactive script will:
   - Check your development environment
   - Help you configure API keys
   - Set up your local environment files
   - Validate your setup

3. **Set up your environment** (if not using the quick setup):
   ```bash
   # Copy environment template
   cp .env.template .env
   
   # Create your secrets file
   echo "OPENWEATHER_KEY=your_api_key_here" > ~/.secrets
   chmod 600 ~/.secrets
   
   # Load environment
   source scripts/source-secrets.sh
   ```

4. **Enter the Nix development environment** (recommended):
   ```bash
   nix develop  # or nix-shell for older Nix versions
   ```

5. **Build and run**:
   ```bash
   cmake -B build && cmake --build build
   ./build/rouen.app/Contents/MacOS/rouen  # macOS
   ./build/rouen  # Linux
   ```

## What You Need

### Required
- **Git** - Version control
- **Nix** (recommended) - For reproducible development environment
- **OpenWeather API Key** - For weather functionality (free at https://openweathermap.org/api)

### Alternative (if not using Nix)
- **CMake 3.30+**
- **C++23 compatible compiler**
- **SDL2, OpenSSL, SQLite** and other dependencies

## Environment Files

- **`.env.template`** - Template showing required environment variables (tracked in git)
- **`.env`** - Your local environment configuration (gitignored, contains secrets)
- **`~/.secrets`** - Global secrets file (gitignored, for sensitive data)

## Helpful Commands

```bash
# Validate your environment
./scripts/validate-environment.sh

# Load environment manually
source scripts/source-secrets.sh

# Quick setup for new machines
./scripts/quick-setup.sh

# Enter Nix environment
nix develop
```

## Getting Help

- **Full documentation**: See [README.md](../README.md)
- **Detailed environment setup**: See [ENVIRONMENT_SETUP.md](ENVIRONMENT_SETUP.md)
- **Project architecture**: See the main README

## Security Notes

- Never commit `.env` or `~/.secrets` files
- Use the `.env.template` file to see what variables are needed
- The quick setup script will help you configure secrets securely