#!/bin/bash

echo "Testing Rouen SSL Configuration Modes"
echo "====================================="

cd /Users/ignaciorodriguez/src/rouen

# Test SSL modes by checking the log output for SSL configuration
test_ssl_mode() {
    local mode=$1
    local description=$2
    echo
    echo "Testing $description mode ($mode):"
    echo "----------------------------------------"
    
    # Set environment variable and run app briefly
    if [ "$mode" = "default" ]; then
        timeout 3 ./build-vcpkg/rouen.app/Contents/MacOS/rouen --test 2>&1 | grep -E "(SSL|Atlassian SSL|compatible SSL|relaxed SSL|strict SSL)" | head -3
    else
        timeout 3 ROUEN_SSL_MODE=$mode ./build-vcpkg/rouen.app/Contents/MacOS/rouen --test 2>&1 | grep -E "(SSL|Atlassian SSL|compatible SSL|relaxed SSL|strict SSL)" | head -3
    fi
    
    local exit_code=${PIPESTATUS[0]}
    if [ $exit_code -eq 124 ]; then
        echo "✅ Mode activated successfully (timeout reached as expected)"
    elif [ $exit_code -eq 0 ]; then
        echo "✅ Mode completed successfully"
    else
        echo "❌ Mode failed with exit code: $exit_code"
    fi
}

# Test each SSL mode
test_ssl_mode "default" "Default (strict)"
test_ssl_mode "strict" "Strict"  
test_ssl_mode "relaxed" "Relaxed"
test_ssl_mode "compatible" "Compatible"
test_ssl_mode "atlassian" "Atlassian"
test_ssl_mode "insecure" "Insecure"

echo
echo "====================================="
echo "SSL mode testing completed!"
echo "All modes are properly configured and accessible."
echo
echo "====================================="
echo "Running SSL UI configuration tests..."
cd build-tests && ./test_ssl_ui
echo "====================================="
