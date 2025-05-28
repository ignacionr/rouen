#!/bin/bash

# Update vcpkg baseline script
# This script updates the vcpkg.json baseline to the latest commit

echo "Updating vcpkg baseline..."

# Check if vcpkg directory exists
if [ ! -d "vcpkg" ]; then
    echo "vcpkg directory not found. Cloning vcpkg..."
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
        ./bootstrap-vcpkg.bat
    else
        ./bootstrap-vcpkg.sh
    fi
    cd ..
fi

# Get the latest commit SHA from vcpkg
cd vcpkg
git pull origin master
LATEST_COMMIT=$(git rev-parse HEAD)
cd ..

echo "Latest vcpkg commit: $LATEST_COMMIT"

# Update vcpkg.json
if [ -f "vcpkg.json" ]; then
    # Check if we have jq for JSON manipulation
    if command -v jq &> /dev/null; then
        # Use jq to update the baseline
        jq --arg baseline "$LATEST_COMMIT" '.["builtin-baseline"] = $baseline' vcpkg.json > vcpkg.json.tmp
        mv vcpkg.json.tmp vcpkg.json
        echo "✅ Updated vcpkg.json baseline using jq"
    else
        # Fallback to sed if jq is not available
        if [[ "$OSTYPE" == "darwin"* ]]; then
            # macOS sed
            sed -i '' "s/\"builtin-baseline\": \".*\"/\"builtin-baseline\": \"$LATEST_COMMIT\"/" vcpkg.json
        else
            # Linux sed
            sed -i "s/\"builtin-baseline\": \".*\"/\"builtin-baseline\": \"$LATEST_COMMIT\"/" vcpkg.json
        fi
        echo "✅ Updated vcpkg.json baseline using sed"
    fi
    
    echo "New vcpkg.json baseline:"
    grep "builtin-baseline" vcpkg.json
else
    echo "❌ vcpkg.json not found in current directory"
    exit 1
fi

echo ""
echo "✅ vcpkg baseline updated successfully!"
echo "You can now build with the latest vcpkg packages."
echo ""
echo "To install dependencies:"
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    echo "  cd vcpkg && .\\vcpkg install --triplet=x64-windows"
else
    echo "  cd vcpkg && ./vcpkg install"
fi
