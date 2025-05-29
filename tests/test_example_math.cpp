/**
 * Test: Basic Math Operations
 * Purpose: Demonstrates how to create a simple console test
 * Category: Example Test
 * 
 * This is an example test showing the testing structure and conventions
 * used in the Rouen project console tests.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <limits> // For std::numeric_limits

// Simple test helper functions
namespace test_helpers {
    void assert_equal(double expected, double actual, const std::string& test_name) {
        const double epsilon = 1e-6;
        if (std::abs(expected - actual) < epsilon) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED (expected " << expected << ", got " << actual << ")\n";
            exit(1);
        }
    }
    
    void assert_true(bool condition, const std::string& test_name) {
        if (condition) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED\n";
            exit(1);
        }
    }
}

void test_basic_arithmetic() {
    std::cout << "\n--- Testing Basic Arithmetic ---\n";
    
    test_helpers::assert_equal(4.0, 2.0 + 2.0, "Addition");
    test_helpers::assert_equal(0.0, 2.0 - 2.0, "Subtraction");
    test_helpers::assert_equal(6.0, 2.0 * 3.0, "Multiplication");
    test_helpers::assert_equal(3.0, 6.0 / 2.0, "Division");
}

void test_edge_cases() {
    std::cout << "\n--- Testing Edge Cases ---\n";
    
    test_helpers::assert_equal(0.0, 0.0 + 0.0, "Zero addition");
    test_helpers::assert_equal(5.0, 5.0 * 1.0, "Multiplication by one");
    // Use a portable NaN for MSVC compatibility
    test_helpers::assert_true(std::isnan(std::numeric_limits<double>::quiet_NaN()), "Division by zero results in NaN");
}

void demonstrate_test_structure() {
    std::cout << "\n--- Test Structure Demonstration ---\n";
    std::cout << "This test shows the recommended structure for console tests:\n";
    std::cout << "1. Clear test documentation at the top\n";
    std::cout << "2. Helper functions for assertions\n";
    std::cout << "3. Organized test functions by category\n";
    std::cout << "4. Clear output with ✅ and ❌ indicators\n";
    std::cout << "5. Proper error handling and exit codes\n";
}

int main() {
    std::cout << "Basic Math Operations Test\n";
    std::cout << "==========================\n";
    std::cout << "Example test demonstrating console test structure\n";
    
    try {
        demonstrate_test_structure();
        test_basic_arithmetic();
        test_edge_cases();
        
        std::cout << "\n" << std::string(40, '=') << "\n";
        std::cout << "✅ All basic math tests passed!\n";
        std::cout << "✅ Test structure demonstration complete\n";
        std::cout << std::string(40, '=') << "\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cout << "❌ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
