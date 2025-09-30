#!/bin/bash
# Simple script to source secrets and validate environment
# Usage: source ./scripts/source-secrets.sh

# Source the secrets file
if [[ -f "$HOME/.secrets" ]]; then
    echo "Loading secrets from $HOME/.secrets"
    set -a  # Automatically export all variables
    source "$HOME/.secrets"
    set +a  # Turn off automatic export
    echo "✓ Secrets loaded successfully"
else
    echo "❌ Warning: $HOME/.secrets not found"
fi

# Source project .env file if it exists
if [[ -f ".env" ]]; then
    echo "Loading project environment from .env"
    set -a
    source ".env"
    set +a
    echo "✓ Project environment loaded"
fi

# Validate critical variables
echo "=== Environment Validation ==="
if [[ -n "$OPENWEATHER_KEY" ]]; then
    echo "✓ OPENWEATHER_KEY is set (${#OPENWEATHER_KEY} characters)"
else
    echo "❌ OPENWEATHER_KEY is not set"
fi

if [[ -n "$GIT_PATH" ]]; then
    echo "✓ GIT_PATH is set: $GIT_PATH"
else
    echo "❌ GIT_PATH is not set"
fi