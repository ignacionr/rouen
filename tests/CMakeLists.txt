# Tests CMake configuration for Rouen project
cmake_minimum_required(VERSION 3.16)

# Set C++23 standard for tests
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include parent directories for access to source files
include_directories(..)

# Test for Bybit currency conversion fix
add_executable(test_bybit_currency_fix 
    test_currency_fix.cpp
)

# Example test for basic math operations
add_executable(test_example_math
    test_example_math.cpp
)

# Set output directory for test executables
set_target_properties(test_bybit_currency_fix test_example_math PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Create a test runner that builds and runs all tests
add_custom_target(run_tests
    COMMAND echo "Running Rouen Console Tests..."
    COMMAND echo "================================"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    DEPENDS test_bybit_currency_fix test_example_math
    COMMENT "Running all console tests"
)

# Individual test targets for convenience
add_custom_target(run_currency_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    DEPENDS test_bybit_currency_fix
    COMMENT "Running Bybit currency conversion test"
)

add_custom_target(run_math_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    DEPENDS test_example_math
    COMMENT "Running basic math example test"
)

# Add a clean target for tests
add_custom_target(clean_tests
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/test_*
    COMMENT "Cleaning test executables"
)
