# Rouen Project Environment Auto-loading
# Add this to your ~/.zshrc or ~/.bash_profile to automatically load 
# the Rouen environment when entering the project directory

# Function to detect and load Rouen environment
rouen_auto_env() {
    # Check if we're in a Rouen project directory
    if [[ -f "$(pwd)/CMakeLists.txt" ]] && grep -q "project(rouen" "$(pwd)/CMakeLists.txt" 2>/dev/null; then
        # Check if environment is already loaded
        if [[ -z "$ROUEN_ENV_LOADED" ]]; then
            if [[ -f "$(pwd)/scripts/source-secrets.sh" ]]; then
                echo "🚀 Rouen project detected - loading environment..."
                source "$(pwd)/scripts/source-secrets.sh"
                export ROUEN_ENV_LOADED=1
                export ROUEN_PROJECT_ROOT="$(pwd)"
                
                # Show quick help
                echo ""
                echo "Available commands:"
                echo "  ./scripts/validate-environment.sh  - Validate environment setup"
                echo "  nix develop                        - Enter Nix development shell"
                echo "  cmake -B build && cmake --build build  - Build the project"
                echo ""
            fi
        fi
    else
        # Clear environment loading flag when leaving project
        if [[ -n "$ROUEN_ENV_LOADED" ]]; then
            unset ROUEN_ENV_LOADED
            unset ROUEN_PROJECT_ROOT
        fi
    fi
}

# Auto-load on directory change (zsh)
if [[ -n "$ZSH_VERSION" ]]; then
    autoload -U add-zsh-hook
    add-zsh-hook chpwd rouen_auto_env
    rouen_auto_env  # Run once on shell startup
fi

# Auto-load on directory change (bash)
if [[ -n "$BASH_VERSION" ]]; then
    rouen_auto_env  # Run once on shell startup
    
    # Hook into PROMPT_COMMAND for bash
    rouen_prompt_command() {
        rouen_auto_env
    }
    
    if [[ -z "$PROMPT_COMMAND" ]]; then
        PROMPT_COMMAND="rouen_prompt_command"
    else
        PROMPT_COMMAND="rouen_prompt_command; $PROMPT_COMMAND"
    fi
fi

# Manual command to load environment
alias rouen-env="source scripts/source-secrets.sh && echo 'Rouen environment loaded manually'"
alias rouen-validate="./scripts/validate-environment.sh"
alias rouen-nix="nix develop"