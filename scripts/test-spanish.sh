#!/bin/zsh

# The phrase to say
text="A ver si nos entendemos"

# 1. Get the list of voices
# 2. Match lines containing 'es_ES' or 'es_MX'
# 3. Extract the full voice name string up until the language code column
say -v "?" | grep -E 'es_(ES|MX)' | while read -r line; do
    # Extract the name using Zsh regex (matches everything up to the es_XX code)
    if [[ "$line" =~ ^(.*[^[:space:]])[[:space:]]+es_(ES|MX) ]]; then
        voice="${match[1]}"
        
        echo "Speaking with: $voice"
        say -v "$voice" "$text"
        
        # Brief pause between voices
        sleep 0.5
    fi
done

