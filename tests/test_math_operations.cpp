/**
 * Google Test for Basic Math Operations
 * Purpose: Demonstrate Google Test usage with parameterized tests and fixtures
 * Category: Example/Tutorial Testing
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cmath>
#include <limits>
#include <chrono>

// Simple math utility class to demonstrate testing
class MathUtils {
public:
    static double add(double a, double b) {
        return a + b;
    }
    
    static double subtract(double a, double b) {
        return a - b;
    }
    
    static double multiply(double a, double b) {
        return a * b;
    }
    
    static double divide(double a, double b) {
        if (std::abs(b) < std::numeric_limits<double>::epsilon()) {
            throw std::invalid_argument("Division by zero");
        }
        return a / b;
    }
    
    static double power(double base, double exponent) {
        return std::pow(base, exponent);
    }
    
    static double sqrt_safe(double value) {
        if (value < 0) {
            throw std::invalid_argument("Square root of negative number");
        }
        return std::sqrt(value);
    }
    
    static bool is_prime(int n) {
        if (n <= 1) return false;
        if (n <= 3) return true;
        if (n % 2 == 0 || n % 3 == 0) return false;
        
        for (int i = 5; i * i <= n; i += 6) {
            if (n % i == 0 || n % (i + 2) == 0) {
                return false;
            }
        }
        return true;
    }
};

// Test fixture for math operations
class MathOperationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code for each test
        epsilon_ = 1e-6;
    }
    
    void TearDown() override {
        // Cleanup code for each test
    }
    
    // Helper function to compare floating point numbers
    void ExpectNearEqual(double expected, double actual, const std::string& operation = "") {
        EXPECT_NEAR(expected, actual, epsilon_) 
            << "Operation: " << operation 
            << " Expected: " << expected 
            << " Actual: " << actual;
    }
    
private:
    double epsilon_;
};

// Basic arithmetic tests
TEST_F(MathOperationsTest, BasicArithmeticOperations) {
    ExpectNearEqual(5.0, MathUtils::add(2.0, 3.0), "add(2, 3)");
    ExpectNearEqual(-1.0, MathUtils::subtract(2.0, 3.0), "subtract(2, 3)");
    ExpectNearEqual(6.0, MathUtils::multiply(2.0, 3.0), "multiply(2, 3)");
    ExpectNearEqual(0.666667, MathUtils::divide(2.0, 3.0), "divide(2, 3)");
}

// Test edge cases for arithmetic operations
TEST_F(MathOperationsTest, ArithmeticEdgeCases) {
    // Test with zero
    ExpectNearEqual(5.0, MathUtils::add(5.0, 0.0), "add with zero");
    ExpectNearEqual(0.0, MathUtils::multiply(100.0, 0.0), "multiply by zero");
    
    // Test with negative numbers
    ExpectNearEqual(-2.0, MathUtils::add(-5.0, 3.0), "add negative");
    ExpectNearEqual(-15.0, MathUtils::multiply(-3.0, 5.0), "multiply negative");
    
    // Test with floating point precision
    ExpectNearEqual(0.3, MathUtils::add(0.1, 0.2), "floating point precision");
}

// Test division by zero handling
TEST_F(MathOperationsTest, DivisionByZeroThrowsException) {
    EXPECT_THROW(MathUtils::divide(5.0, 0.0), std::invalid_argument);
    EXPECT_THROW(MathUtils::divide(-3.0, 0.0), std::invalid_argument);
    
    // Very small number should still work (not exactly zero)
    EXPECT_NO_THROW(MathUtils::divide(5.0, 1e-10));
}

// Test power function
TEST_F(MathOperationsTest, PowerFunction) {
    ExpectNearEqual(8.0, MathUtils::power(2.0, 3.0), "2^3");
    ExpectNearEqual(1.0, MathUtils::power(5.0, 0.0), "5^0");
    ExpectNearEqual(0.25, MathUtils::power(2.0, -2.0), "2^-2");
    ExpectNearEqual(2.0, MathUtils::power(4.0, 0.5), "4^0.5");
}

// Test square root function
TEST_F(MathOperationsTest, SquareRootFunction) {
    ExpectNearEqual(3.0, MathUtils::sqrt_safe(9.0), "sqrt(9)");
    ExpectNearEqual(0.0, MathUtils::sqrt_safe(0.0), "sqrt(0)");
    ExpectNearEqual(1.414214, MathUtils::sqrt_safe(2.0), "sqrt(2)");
    
    // Test exception for negative input
    EXPECT_THROW(MathUtils::sqrt_safe(-1.0), std::invalid_argument);
}

// Parameterized test for prime number checking
class PrimeNumberTest : public ::testing::TestWithParam<std::pair<int, bool>> {};

TEST_P(PrimeNumberTest, IsPrime) {
    auto [number, expected] = GetParam();
    EXPECT_EQ(expected, MathUtils::is_prime(number)) 
        << "Testing if " << number << " is prime";
}

// Prime number test cases
INSTANTIATE_TEST_SUITE_P(
    PrimeTests,
    PrimeNumberTest,
    ::testing::Values(
        std::make_pair(-1, false),  // Negative numbers
        std::make_pair(0, false),   // Zero
        std::make_pair(1, false),   // One
        std::make_pair(2, true),    // First prime
        std::make_pair(3, true),    // Second prime
        std::make_pair(4, false),   // First composite
        std::make_pair(5, true),    // Prime
        std::make_pair(6, false),   // Composite
        std::make_pair(7, true),    // Prime
        std::make_pair(8, false),   // Composite
        std::make_pair(9, false),   // Composite
        std::make_pair(11, true),   // Prime
        std::make_pair(13, true),   // Prime
        std::make_pair(15, false),  // Composite
        std::make_pair(17, true),   // Prime
        std::make_pair(25, false),  // Composite
        std::make_pair(29, true),   // Prime
        std::make_pair(97, true),   // Large prime
        std::make_pair(100, false)  // Large composite
    )
);

// Performance test for large numbers (if needed)
TEST_F(MathOperationsTest, LargeNumberPerformance) {
    // Test that operations complete in reasonable time
    auto start = std::chrono::high_resolution_clock::now();
    
    double result = MathUtils::multiply(123456.789, 987654.321);
    // Use EXPECT_DOUBLE_EQ for large numbers as suggested by Google Test
    EXPECT_DOUBLE_EQ(121932631112.63527, result);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should complete very quickly (less than 1ms)
    EXPECT_LT(duration.count(), 1000) << "Operation took too long: " << duration.count() << " microseconds";
}

// Test with Google Mock (demonstrating advanced features)
class MockCalculator {
public:
    MOCK_METHOD(double, calculate, (double a, double b));
    MOCK_METHOD(void, log_operation, (const std::string& operation));
};

TEST(MockCalculatorTest, MockExample) {
    MockCalculator mock_calc;
    
    // Set expectations
    EXPECT_CALL(mock_calc, calculate(2.0, 3.0))
        .WillOnce(testing::Return(5.0));
    
    EXPECT_CALL(mock_calc, log_operation("add"))
        .Times(1);
    
    // Use the mock
    double result = mock_calc.calculate(2.0, 3.0);
    mock_calc.log_operation("add");
    
    EXPECT_EQ(5.0, result);
}

// Death test example (testing that program terminates correctly)
TEST(MathDeathTest, AssertionFailure) {
    // This would test scenarios where the program should terminate
    // For demonstration purposes only - typically used for testing assertions
    EXPECT_NO_FATAL_FAILURE({
        // Code that should not cause fatal failure
        double result = MathUtils::add(1.0, 2.0);
        (void)result; // Suppress unused variable warning
    });
}
