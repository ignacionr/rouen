#!/bin/bash

# Rouen Console Test Runner
# Builds and executes all console tests for the Rouen project

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BLUE}Rouen Console Test Runner${NC}"
echo -e "${BLUE}=========================${NC}"
echo ""

# Check if CMake build directory exists
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir build
fi

cd build

# Configure and build tests
echo -e "${YELLOW}Configuring tests with CMake...${NC}"
cmake .. > /dev/null 2>&1

echo -e "${YELLOW}Building tests...${NC}"
make > /dev/null 2>&1

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""

# Run all tests
echo -e "${BLUE}Running Tests${NC}"
echo -e "${BLUE}=============${NC}"

# Test counter
total_tests=0
passed_tests=0

# Function to run a single test
run_test() {
    local test_name="$1"
    local test_executable="$2"
    
    echo -e "\n${YELLOW}Running: $test_name${NC}"
    echo "----------------------------------------"
    
    total_tests=$((total_tests + 1))
    
    if [ -f "$test_executable" ]; then
        if ./"$test_executable"; then
            echo -e "${GREEN}✅ $test_name PASSED${NC}"
            passed_tests=$((passed_tests + 1))
        else
            echo -e "${RED}❌ $test_name FAILED${NC}"
        fi
    else
        echo -e "${RED}❌ $test_name EXECUTABLE NOT FOUND${NC}"
    fi
}

# Run individual tests
run_test "Bybit Currency Fix Test" "test_bybit_currency_fix"
run_test "Basic Math Example Test" "test_example_math"

# Add more tests here as they are created
# run_test "Another Test" "test_another_feature"

# Summary
echo ""
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}============${NC}"
echo -e "Total tests: $total_tests"
echo -e "Passed: ${GREEN}$passed_tests${NC}"
echo -e "Failed: ${RED}$((total_tests - passed_tests))${NC}"

if [ $passed_tests -eq $total_tests ]; then
    echo -e "\n${GREEN}🎉 All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some tests failed${NC}"
    exit 1
fi
