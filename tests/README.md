# Rouen Console Tests

This directory contains console-based tests for the Rouen application. These tests are designed to validate specific functionality and demonstrate fixes or features in a simple, readable format.

## Test Structure

Each test is a standalone C++ program that can be compiled and run independently. Tests focus on:
- Demonstrating bug fixes
- Validating core functionality
- Providing examples of proper usage
- Performance testing
- Integration testing

## Available Tests

### `test_currency_fix.cpp` - Bybit Currency Conversion Fix
Demonstrates the fix for the cryptocurrency currency conversion issue where different coins (BTC, ETH, USDT) were being summed without proper currency conversion.

**What it tests:**
- UNIFIED wallet properly uses USD equity values
- FUND wallet displays individual coin amounts without meaningless summation
- Portfolio totals only include meaningful USD values

### `test_example_math.cpp` - Basic Math Operations (Example)
A demonstration test showing the structure and conventions for console tests in the Rouen project.

**What it tests:**
- Basic arithmetic operations
- Test helper function usage
- Proper assertion patterns
- Console test structure

## Building and Running Tests

### Using CMake (Recommended)
```bash
# From the tests directory
mkdir build && cd build
cmake ..
make

# Run all tests
make run_tests

# Run specific test
make run_currency_test
```

### Manual compilation
```bash
# From the tests directory
g++ -std=c++23 -I.. test_currency_fix.cpp -o test_currency_fix
./test_currency_fix
```

### Using the test runner script
```bash
./run_tests.sh
```

## Adding New Tests

1. Create a new `.cpp` file in this directory
2. Add the test executable to `CMakeLists.txt`
3. Add a custom target for running the specific test
4. Update the `run_tests` target to include the new test
5. Update this README with test description

### Test Template
```cpp
/**
 * Test: [Brief description]
 * Purpose: [What this test validates]
 * Related Issue: [If fixing a bug, reference the issue]
 */

#include <iostream>
// Include necessary headers

int main() {
    std::cout << "Test Name: [Test Description]\n";
    std::cout << "============================\n";
    
    // Test implementation
    
    std::cout << "\n✅ Test completed successfully!\n";
    return 0;
}
```

## Test Categories

- **Bug Fix Tests**: Demonstrate that specific bugs have been resolved
- **Feature Tests**: Validate new features work correctly
- **Integration Tests**: Test interactions between components
- **Performance Tests**: Measure and validate performance characteristics
- **Example Tests**: Show proper usage of APIs or features

## Test Guidelines

- Keep tests simple and focused on one specific aspect
- Include clear output that explains what is being tested
- Use meaningful assertions and error messages
- Document the purpose and expected behavior
- Make tests self-contained when possible
