#!/bin/bash
# Test script to verify environment variable loading

echo "=== Testing Rouen Environment Variable Loading ==="
echo ""

# Check if .env file exists
if [[ -f ".env" ]]; then
    echo "✓ .env file exists:"
    echo "  Content: $(cat .env)"
else
    echo "❌ .env file not found"
    exit 1
fi

echo ""

# Check current shell environment
echo "Current shell environment:"
echo "  OPENWEATHER_KEY: '${OPENWEATHER_KEY}'"
echo "  (Should be empty - we want app to read from .env file)"

echo ""

# Test the application
echo "Testing application startup (first 20 lines of output):"
timeout 5s ./build-test/rouen.app/Contents/MacOS/rouen --help 2>&1 | head -20

echo ""
echo "=== Test Complete ==="
echo "If you see '[CONFIG][DEBUG] Loaded from .env: OPENWEATHER_KEY = 0433...7b39' above,"
echo "then the application is successfully reading from the .env file!"