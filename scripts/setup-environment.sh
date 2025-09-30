#!/bin/bash
# Profile setup script for Rouen project
# This script should be sourced by your shell profile (.zshrc, .bash_profile, etc.)

# Function to source secrets file if it exists
source_secrets() {
    local secrets_file="$HOME/.secrets"
    if [[ -f "$secrets_file" ]]; then
        echo "Loading secrets from $secrets_file"
        # Export each line as an environment variable
        while IFS='=' read -r key value; do
            # Skip empty lines and comments
            [[ -z "$key" || "$key" =~ ^[[:space:]]*# ]] && continue
            # Remove leading/trailing whitespace
            key=$(echo "$key" | xargs)
            value=$(echo "$value" | xargs)
            if [[ -n "$key" && -n "$value" ]]; then
                export "$key"="$value"
                echo "Exported: $key"
            fi
        done < "$secrets_file"
    else
        echo "Warning: Secrets file not found at $secrets_file"
    fi
}

# Function to source project-specific .env file
source_project_env() {
    local project_dir="$1"
    local env_file="$project_dir/.env"
    if [[ -f "$env_file" ]]; then
        echo "Loading project environment from $env_file"
        while IFS='=' read -r key value; do
            # Skip empty lines and comments
            [[ -z "$key" || "$key" =~ ^[[:space:]]*# ]] && continue
            # Remove leading/trailing whitespace
            key=$(echo "$key" | xargs)
            value=$(echo "$value" | xargs)
            if [[ -n "$key" && -n "$value" ]]; then
                export "$key"="$value"
                echo "Exported: $key"
            fi
        done < "$env_file"
    fi
}

# Main setup function
setup_rouen_environment() {
    echo "=== Setting up Rouen development environment ==="
    
    # First load global secrets
    source_secrets
    
    # Then load project-specific environment if we're in a project directory
    if [[ -f "$(pwd)/.env" ]]; then
        source_project_env "$(pwd)"
    fi
    
    # If we're in the rouen project specifically
    if [[ -f "$(pwd)/CMakeLists.txt" ]] && grep -q "project(rouen" "$(pwd)/CMakeLists.txt" 2>/dev/null; then
        echo "Detected Rouen project directory"
        source_project_env "$(pwd)"
        
        # Set up additional project-specific environment
        export ROUEN_PROJECT_ROOT="$(pwd)"
        
        # Add any additional project-specific setup here
        echo "ROUEN_PROJECT_ROOT set to: $ROUEN_PROJECT_ROOT"
    fi
    
    echo "=== Environment setup complete ==="
}

# Function to validate critical environment variables
validate_environment() {
    echo "=== Validating Rouen environment ==="
    
    local required_vars=("OPENWEATHER_KEY")
    local missing_vars=()
    
    for var in "${required_vars[@]}"; do
        if [[ -z "${!var}" ]]; then
            missing_vars+=("$var")
        else
            echo "✓ $var is set"
        fi
    done
    
    if [[ ${#missing_vars[@]} -gt 0 ]]; then
        echo "❌ Missing required environment variables:"
        printf '   %s\n' "${missing_vars[@]}"
        echo "Please ensure these are set in your ~/.secrets file"
        return 1
    else
        echo "✅ All required environment variables are set"
        return 0
    fi
}

# Export functions so they can be called from shell
export -f source_secrets
export -f source_project_env
export -f setup_rouen_environment
export -f validate_environment

# Auto-setup if sourced (not executed)
if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
    setup_rouen_environment
fi