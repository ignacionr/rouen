# Tests CMake configuration for Rouen project
cmake_minimum_required(VERSION 3.16)

# Project declaration
project(RouenTests LANGUAGES CXX)

# Set C++23 standard for tests
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Find Google Test
find_package(GTest CONFIG REQUIRED)

# Find required dependencies for tests
find_package(CURL REQUIRED)

# Include parent directories for access to source files
include_directories(..)

# Helper function to create GTest-based tests
function(add_gtest_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} 
        PRIVATE 
        GTest::gtest 
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
    )
    # Add test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Set properties for better output
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# Helper function to create GTest-based tests with HTTP dependencies
function(add_gtest_http_executable test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} 
        PRIVATE 
        GTest::gtest 
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        CURL::libcurl
    )
    # Add test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Set properties for better output
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# Create Google Test-based tests
add_gtest_http_executable(test_fetch_ssl test_fetch_ssl.cpp)
add_gtest_executable(test_math_operations test_math_operations.cpp)

# Legacy tests (keeping for compatibility)
# Test for Bybit currency conversion fix
add_executable(test_bybit_currency_fix 
    test_currency_fix.cpp
)
add_test(NAME BybitCurrencyFixTest COMMAND test_bybit_currency_fix)

# Example test for basic math operations
add_executable(test_example_math
    test_example_math.cpp
)
add_test(NAME ExampleMathTest COMMAND test_example_math)

# Set output directory for test executables
set_target_properties(test_bybit_currency_fix test_example_math PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Create a comprehensive test runner that builds and runs all tests
add_custom_target(run_all_tests
    COMMAND echo "Running Rouen Test Suite..."
    COMMAND echo "============================="
    COMMAND echo "Google Test Suite:"
    COMMAND echo "-------------------"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_fetch_ssl
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_math_operations
    COMMAND echo ""
    COMMAND echo "Legacy Console Tests:"
    COMMAND echo "---------------------"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    DEPENDS test_fetch_ssl test_math_operations test_bybit_currency_fix test_example_math
    COMMENT "Running complete Rouen test suite"
)

# Google Test specific targets
add_custom_target(run_gtest_only
    COMMAND echo "Running Google Test Suite Only..."
    COMMAND echo "=================================="
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_fetch_ssl
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_math_operations
    DEPENDS test_fetch_ssl test_math_operations
    COMMENT "Running Google Test suite only"
)

# Legacy test targets (keeping for compatibility)
add_custom_target(run_legacy_tests
    COMMAND echo "Running Legacy Console Tests..."
    COMMAND echo "================================"
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_bybit_currency_fix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_example_math
    DEPENDS test_bybit_currency_fix test_example_math
    COMMENT "Running legacy console tests"
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

# Individual Google Test targets
add_custom_target(run_fetch_ssl_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_fetch_ssl
    DEPENDS test_fetch_ssl
    COMMENT "Running HTTP Fetch SSL configuration tests"
)

add_custom_target(run_math_operations_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_math_operations
    DEPENDS test_math_operations
    COMMENT "Running advanced math operations tests"
)

# Enable CTest integration
add_custom_target(run_ctest
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --verbose
    DEPENDS test_fetch_ssl test_math_operations test_bybit_currency_fix test_example_math
    COMMENT "Running tests through CTest framework"
)

# Add a clean target for tests
add_custom_target(clean_tests
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/test_*
    COMMENT "Cleaning test executables"
)
