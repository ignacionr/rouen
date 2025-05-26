#!/bin/bash

# New Test Creator for Rouen Console Tests
# Usage: ./new_test.sh test_name "Test Description"

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

if [ $# -ne 2 ]; then
    echo -e "${RED}Usage: $0 <test_name> \"<Test Description>\"${NC}"
    echo -e "${YELLOW}Example: $0 test_json_parser \"JSON Parser Functionality\"${NC}"
    exit 1
fi

TEST_NAME="$1"
TEST_DESCRIPTION="$2"
TEST_FILE="test_${TEST_NAME}.cpp"

# Check if test already exists
if [ -f "$TEST_FILE" ]; then
    echo -e "${RED}Test file $TEST_FILE already exists!${NC}"
    exit 1
fi

# Create test from template
echo -e "${YELLOW}Creating new test: $TEST_FILE${NC}"
cp test_template.cpp "$TEST_FILE"

# Replace placeholders in the new test file
sed -i.bak "s/\[Test Name\]/$TEST_DESCRIPTION/g" "$TEST_FILE"
sed -i.bak "s/\[Brief description of what this test validates\]/$TEST_DESCRIPTION/g" "$TEST_FILE"
rm "${TEST_FILE}.bak"

echo -e "${GREEN}✅ Test file created: $TEST_FILE${NC}"
echo -e "${YELLOW}Next steps:${NC}"
echo "1. Edit $TEST_FILE to implement your test logic"
echo "2. Add the test to CMakeLists.txt:"
echo "   add_executable(test_${TEST_NAME} ${TEST_FILE})"
echo "3. Update the run_tests target in CMakeLists.txt"
echo "4. Add the test to run_tests.sh"
echo "5. Update README.md with test description"
echo ""
echo -e "${YELLOW}Quick add to CMakeLists.txt:${NC}"
echo "# Test for $TEST_DESCRIPTION"
echo "add_executable(test_${TEST_NAME}"
echo "    ${TEST_FILE}"
echo ")"
