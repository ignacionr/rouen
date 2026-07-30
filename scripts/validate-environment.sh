#!/bin/bash
# Environment validation script for Rouen project
# Usage: ./scripts/validate-environment.sh

set -e

echo "=== Rouen Environment Validation ==="
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Validation functions
check_file() {
    local file="$1"
    local description="$2"
    
    if [[ -f "$file" ]]; then
        echo -e "${GREEN}✓${NC} $description: $file"
        return 0
    else
        echo -e "${RED}✗${NC} $description: $file (NOT FOUND)"
        return 1
    fi
}

check_env_var() {
    local var_name="$1"
    local var_value="${!var_name}"
    
    if [[ -n "$var_value" ]]; then
        local masked_value
        if [[ ${#var_value} -gt 8 ]]; then
            masked_value="${var_value:0:4}****${var_value: -4}"
        else
            masked_value="****"
        fi
        echo -e "${GREEN}✓${NC} $var_name is set: $masked_value"
        return 0
    else
        echo -e "${RED}✗${NC} $var_name is not set"
        return 1
    fi
}

check_command() {
    local cmd="$1"
    local description="$2"
    
    if command -v "$cmd" &> /dev/null; then
        echo -e "${GREEN}✓${NC} $description: $(command -v "$cmd")"
        return 0
    else
        echo -e "${RED}✗${NC} $description: $cmd not found"
        return 1
    fi
}

# Start validation
validation_errors=0

echo "1. Checking configuration files..."
check_file "$HOME/.secrets" "Global secrets file" || ((validation_errors++))

# Check for .env file, suggest creating from template if missing
if [[ -f ".env" ]]; then
    echo -e "${GREEN}✓${NC} Project environment file: .env"
else
    echo -e "${RED}✗${NC} Project environment file: .env (NOT FOUND)"
    if [[ -f ".env.template" ]]; then
        echo -e "${YELLOW}💡${NC} Hint: Copy .env.template to .env and configure your secrets"
    fi
    ((validation_errors++))
fi

check_file ".env.template" "Environment template file" || ((validation_errors++))
check_file "scripts/source-secrets.sh" "Secrets loading script" || ((validation_errors++))
check_file "scripts/nix-setup-secrets.sh" "Nix secrets setup script" || ((validation_errors++))

echo ""
echo "2. Loading environment..."

# Load environment
if [[ -f "scripts/source-secrets.sh" ]]; then
    echo "Loading secrets..."
    source scripts/source-secrets.sh > /dev/null 2>&1
    echo -e "${GREEN}✓${NC} Environment loaded successfully"
else
    echo -e "${RED}✗${NC} Cannot load environment - script missing"
    ((validation_errors++))
fi

echo ""
echo "3. Checking environment variables..."
check_env_var "OPENWEATHER_KEY" || ((validation_errors++))
check_env_var "GIT_PATH" || ((validation_errors++))

echo ""
echo "4. Checking development tools..."
check_command "cmake" "CMake build system"
check_command "git" "Git version control"
check_command "pkg-config" "Package configuration tool"

echo ""
echo "5. Checking optional tools..."
if command -v nix &> /dev/null; then
    echo -e "${GREEN}✓${NC} Nix package manager: $(command -v nix)"
    
    # Test Nix environment
    if nix-shell --run "echo 'Nix shell test successful'" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Nix shell environment works"
    else
        echo -e "${YELLOW}⚠${NC} Nix shell environment may have issues"
    fi
else
    echo -e "${YELLOW}⚠${NC} Nix package manager not found (optional)"
fi

echo ""
echo "6. Checking project structure..."
check_file "CMakeLists.txt" "Main CMake configuration" || ((validation_errors++))
check_file "src/main_wnd.hpp" "Main window header" || ((validation_errors++))
check_file "src/hosts/weather_host.hpp" "Weather host (uses OPENWEATHER_KEY)" || ((validation_errors++))

echo ""
echo "=== Validation Summary ==="
if [[ $validation_errors -eq 0 ]]; then
    echo -e "${GREEN}✅ All checks passed! Your environment is properly configured.${NC}"
    echo ""
    echo "The application will automatically load environment variables from:"
    echo "  1. Your .env file (project directory)"
    echo "  2. System environment variables (as fallback)"
    echo "  3. ~/.secrets file (via setup scripts)"
    echo ""
    echo "You can now build and run the project:"
    echo "  nix develop --command bash -c 'cmake -G Ninja -B build && cmake --build build'"
    echo "  ./build/rouen.app/Contents/MacOS/rouen  # macOS"
    echo "  ./build/rouen  # Linux"
    echo ""
    echo "The application will work correctly even without Nix environment!"
    exit 0
else
    echo -e "${RED}❌ $validation_errors error(s) found in your environment setup.${NC}"
    echo ""
    echo "Please refer to docs/ENVIRONMENT_SETUP.md for setup instructions."
    exit 1
fi