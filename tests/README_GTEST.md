# Google Test Integration for Rouen Project

This directory contains unit tests for the Rouen project using Google Test framework with C++23.

## Test Structure

### Google Test Based Tests (Recommended)
- `test_fetch_ssl.cpp` - HTTP/SSL configuration testing with fixtures and parameterized tests
- `test_math_operations.cpp` - Advanced math operations with mocking and performance tests

### Legacy Console Tests (Maintained for compatibility)
- `test_currency_fix.cpp` - Bybit currency conversion functionality
- `test_example_math.cpp` - Basic math operations demonstration

## Building and Running Tests

### Prerequisites
```bash
# Install dependencies (already done if you've built the main project)
./vcpkg/vcpkg install
```

### Build Tests
```bash
# Configure with vcpkg
mkdir -p build-vcpkg && cd build-vcpkg
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake

# Build all tests
make -j$(nproc)
```

### Running Tests

#### All Tests
```bash
# Run complete test suite (Google Test + Legacy)
make run_all_tests

# Run through CTest framework (includes XML output)
make run_ctest
```

#### Google Test Suite Only
```bash
# Run only Google Test based tests
make run_gtest_only
```

#### Individual Tests
```bash
# Google Test targets
make run_fetch_ssl_test      # HTTP/SSL configuration tests
make run_math_operations_test # Advanced math with mocking

# Legacy test targets
make run_currency_test       # Bybit currency conversion
make run_math_test          # Basic math example
```

#### Direct Execution
```bash
# Run tests directly with full output
./tests/test_fetch_ssl --gtest_output=xml:test_results.xml
./tests/test_math_operations --gtest_filter="*Prime*"
```

## Google Test Features Demonstrated

### 1. Test Fixtures (`TEST_F`)
- Setup and teardown for each test
- Shared test data and helper functions
- Environment variable management

### 2. Parameterized Tests (`TEST_P`)
- Data-driven testing with multiple input values
- Automatic test case generation
- Clear test case naming

### 3. Exception Testing
- `EXPECT_THROW` and `EXPECT_NO_THROW`
- Specific exception type verification
- Error condition testing

### 4. Floating Point Comparisons
- `EXPECT_NEAR` for floating point precision
- Custom epsilon values
- Safe numerical comparisons

### 5. Google Mock Integration
- Mock object creation with `MOCK_METHOD`
- Expectation setting with `EXPECT_CALL`
- Return value specification with `WillOnce`

### 6. Death Tests
- Testing program termination scenarios
- Assertion failure verification
- Safe testing of fatal conditions

### 7. Performance Testing
- Execution time measurement
- Performance regression detection
- Timeout verification

## Test Organization Best Practices

### Naming Conventions
- Test files: `test_<module_name>.cpp`
- Test classes: `<ModuleName>Test`
- Test cases: Descriptive names explaining what is tested

### Test Categories
- **Unit Tests**: Test individual functions/classes in isolation
- **Integration Tests**: Test component interactions
- **Performance Tests**: Verify timing and resource usage
- **Mock Tests**: Test with external dependencies mocked

### Environment Setup
Tests automatically handle:
- Environment variable cleanup
- SSL configuration isolation
- Resource initialization/cleanup

## Google Test Command Line Options

```bash
# Filter tests by name pattern
./test_executable --gtest_filter="*SSL*"

# List all available tests
./test_executable --gtest_list_tests

# Generate XML output for CI/CD
./test_executable --gtest_output=xml:results.xml

# Repeat tests multiple times
./test_executable --gtest_repeat=100

# Shuffle test execution order
./test_executable --gtest_shuffle

# Break on first failure
./test_executable --gtest_break_on_failure
```

## Adding New Tests

### 1. Create Test File
```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../src/your_module.hpp"

class YourModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }
    
    void TearDown() override {
        // Cleanup code
    }
};

TEST_F(YourModuleTest, TestSomething) {
    EXPECT_EQ(expected, actual);
}
```

### 2. Update CMakeLists.txt
```cmake
add_gtest_executable(test_your_module test_your_module.cpp)

# Add to comprehensive test runner
add_custom_target(run_your_module_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_your_module
    DEPENDS test_your_module
    COMMENT "Running your module tests"
)
```

### 3. Update Test Runners
Add your test to the appropriate `run_all_tests` or `run_gtest_only` targets.

## CI/CD Integration

Tests are designed to work with continuous integration:

```bash
# In CI pipeline
cmake --build . --target run_ctest
```

This generates XML reports compatible with most CI systems (GitHub Actions, Jenkins, etc.).

## Debugging Tests

### Verbose Output
```bash
./test_executable --gtest_output=verbose
```

### Debug Specific Test
```bash
gdb ./test_executable
(gdb) run --gtest_filter="YourTest.SpecificCase"
```

### Memory Leak Detection
```bash
valgrind --leak-check=full ./test_executable
```

## Test Coverage

To generate coverage reports (requires gcov/lcov):

```bash
# Build with coverage flags
cmake .. -DCMAKE_CXX_FLAGS="--coverage"
make

# Run tests
make run_all_tests

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```
