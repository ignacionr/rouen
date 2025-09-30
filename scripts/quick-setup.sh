#!/bin/bash
# Quick setup script for new Rouen developers
# This script helps set up the development environment step by step

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Rouen Development Environment Quick Setup ===${NC}"
echo ""

# Function to prompt for input
prompt_input() {
    local prompt="$1"
    local var_name="$2"
    local is_secret="${3:-false}"
    
    echo -n -e "${YELLOW}$prompt${NC} "
    if [[ "$is_secret" == "true" ]]; then
        read -s value
        echo ""
    else
        read value
    fi
    
    eval "$var_name='$value'"
}

# Check if secrets file exists
if [[ -f "$HOME/.secrets" ]]; then
    echo -e "${GREEN}✓${NC} Found existing secrets file: $HOME/.secrets"
    
    # Check if it contains OPENWEATHER_KEY
    if grep -q "OPENWEATHER_KEY" "$HOME/.secrets"; then
        echo -e "${GREEN}✓${NC} OPENWEATHER_KEY found in secrets file"
        secrets_configured=true
    else
        echo -e "${YELLOW}⚠${NC} OPENWEATHER_KEY not found in secrets file"
        secrets_configured=false
    fi
else
    echo -e "${YELLOW}⚠${NC} No secrets file found at $HOME/.secrets"
    secrets_configured=false
fi

# Setup secrets if needed
if [[ "$secrets_configured" != "true" ]]; then
    echo ""
    echo -e "${BLUE}Setting up secrets configuration...${NC}"
    echo ""
    echo "Rouen needs an OpenWeather API key for weather functionality."
    echo "You can get a free API key at: https://openweathermap.org/api"
    echo ""
    
    prompt_input "Enter your OpenWeather API key (or press Enter to skip):" openweather_key true
    
    if [[ -n "$openweather_key" ]]; then
        # Create or update secrets file
        if [[ -f "$HOME/.secrets" ]]; then
            # Update existing file
            if grep -q "OPENWEATHER_KEY" "$HOME/.secrets"; then
                # Replace existing key
                sed -i.bak "s/^OPENWEATHER_KEY=.*/OPENWEATHER_KEY=$openweather_key/" "$HOME/.secrets"
            else
                # Add new key
                echo "OPENWEATHER_KEY=$openweather_key" >> "$HOME/.secrets"
            fi
        else
            # Create new file
            echo "OPENWEATHER_KEY=$openweather_key" > "$HOME/.secrets"
            chmod 600 "$HOME/.secrets"
        fi
        
        # Create local .env file from template if it doesn't exist
        if [[ ! -f ".env" ]] && [[ -f ".env.template" ]]; then
            echo "Creating local .env file from template..."
            cp .env.template .env
            # Update the API key in the local .env file
            sed -i.bak "s/your_openweather_api_key_here/$openweather_key/" .env
            echo -e "${GREEN}✓${NC} Local .env file created from template"
        fi
        
        echo -e "${GREEN}✓${NC} Secrets file created/updated at $HOME/.secrets"
    else
        echo -e "${YELLOW}⚠${NC} Skipping API key setup. Weather functionality will be limited."
    fi
fi

echo ""
echo -e "${BLUE}Checking development tools...${NC}"

# Check for Nix
if command -v nix &> /dev/null; then
    echo -e "${GREEN}✓${NC} Nix is installed"
    
    echo ""
    echo -e "${BLUE}Testing Nix environment...${NC}"
    
    # Test nix-shell
    if nix-shell --run "echo 'Nix shell test successful'" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Nix shell works (using shell.nix)"
        nix_method="nix-shell"
    elif nix develop --command echo "Nix develop test successful" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Nix flakes work (using flake.nix)"
        nix_method="nix develop"
    else
        echo -e "${RED}✗${NC} Nix environment has issues"
        nix_method=""
    fi
else
    echo -e "${YELLOW}⚠${NC} Nix not found. You'll need to install dependencies manually."
    nix_method=""
fi

# Check for basic tools
echo ""
echo -e "${BLUE}Checking basic development tools...${NC}"

tools_needed=()
for tool in git cmake; do
    if command -v "$tool" &> /dev/null; then
        echo -e "${GREEN}✓${NC} $tool is available"
    else
        echo -e "${RED}✗${NC} $tool is missing"
        tools_needed+=("$tool")
    fi
done

echo ""
echo -e "${BLUE}Running environment validation...${NC}"

# Run validation script
if [[ -f "scripts/validate-environment.sh" ]]; then
    if [[ -n "$nix_method" ]]; then
        echo "Running validation in Nix environment..."
        if [[ "$nix_method" == "nix-shell" ]]; then
            nix-shell --run "./scripts/validate-environment.sh" || true
        else
            nix develop --command ./scripts/validate-environment.sh || true
        fi
    else
        echo "Running validation with system tools..."
        ./scripts/validate-environment.sh || true
    fi
else
    echo -e "${RED}✗${NC} Validation script not found"
fi

echo ""
echo -e "${BLUE}=== Setup Summary ===${NC}"

if [[ -f "$HOME/.secrets" ]] && grep -q "OPENWEATHER_KEY" "$HOME/.secrets"; then
    echo -e "${GREEN}✓${NC} Environment variables configured"
else
    echo -e "${YELLOW}⚠${NC} Environment variables need attention"
fi

if [[ -n "$nix_method" ]]; then
    echo -e "${GREEN}✓${NC} Nix development environment available"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "1. Enter Nix environment: $nix_method"
    echo "2. Build the project: cmake -B build && cmake --build build"
    echo "3. Run the application: ./build/rouen.app/Contents/MacOS/rouen (macOS) or ./build/rouen (Linux)"
elif [[ ${#tools_needed[@]} -eq 0 ]]; then
    echo -e "${GREEN}✓${NC} Basic development tools available"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "1. Install additional dependencies (SDL2, OpenSSL, etc.)"
    echo "2. Build the project: cmake -B build && cmake --build build"
    echo "3. Run the application"
else
    echo -e "${YELLOW}⚠${NC} Some development tools are missing: ${tools_needed[*]}"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "1. Install missing tools: ${tools_needed[*]}"
    echo "2. Install additional dependencies (SDL2, OpenSSL, etc.)"
    echo "3. Build the project"
fi

echo ""
echo -e "${BLUE}For detailed setup instructions, see: docs/ENVIRONMENT_SETUP.md${NC}"
echo -e "${BLUE}For project documentation, see: README.md${NC}"