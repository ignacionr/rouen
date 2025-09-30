#!/bin/bash
# Nix environment setup script that loads secrets
# This script is designed to work with the Nix development environment

echo "=== Nix + Secrets Environment Setup ==="

# Function to load secrets file
load_secrets() {
    local secrets_file="$HOME/.secrets"
    if [[ -f "$secrets_file" ]]; then
        echo "Loading secrets from $secrets_file"
        # Use a more robust parsing approach
        while IFS= read -r line; do
            # Skip empty lines and comments
            [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
            
            # Check if line contains an equals sign
            if [[ "$line" =~ ^[^=]+= ]]; then
                # Split on first equals sign
                key="${line%%=*}"
                value="${line#*=}"
                
                # Trim whitespace
                key="$(echo "$key" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                value="$(echo "$value" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                
                if [[ -n "$key" ]]; then
                    export "$key"="$value"
                    echo "Exported: $key"
                fi
            fi
        done < "$secrets_file"
    else
        echo "Warning: Secrets file not found at $secrets_file"
    fi
}

# Function to validate environment
validate_nix_environment() {
    echo "=== Validating Nix Development Environment ==="
    
    # Check for required tools
    local tools=("cmake" "pkg-config" "curl")
    for tool in "${tools[@]}"; do
        if command -v "$tool" &> /dev/null; then
            echo "✓ $tool is available"
        else
            echo "❌ $tool is not available"
        fi
    done
    
    # Check for required environment variables
    if [[ -n "$OPENWEATHER_KEY" ]]; then
        echo "✓ OPENWEATHER_KEY is set"
    else
        echo "❌ OPENWEATHER_KEY is not set"
    fi
    
    # Check NIX_PATH or flake availability
    if [[ -n "$NIX_PATH" ]] || [[ -f "flake.nix" ]]; then
        echo "✓ Nix environment detected"
    else
        echo "❌ Nix environment not properly configured"
    fi
}

# Main execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Script is being executed
    load_secrets
    validate_nix_environment
else
    # Script is being sourced
    load_secrets
fi