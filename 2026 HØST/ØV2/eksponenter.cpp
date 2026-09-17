//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <vector>
#include <chrono>
#include <cmath>



// Function to calculate the power of a number using recursion
constexpr double metode1(double base, int exponent) {
    // If exponent is 0 or negative: Error
    if (exponent <= 0) [[unlikely]] {                                                   // ? Mark as unlikely to be true for optimization
        // We could expand the function to handle 0, but for the sake of the
        // exercise, we'll follow the instructions slavishly, and instead add
        // handling to prevent the function from returning unexpected results
        throw std::invalid_argument("Exponent must be a positive integer.");
    }

    // If exponent is 1: x
    if (exponent == 1) [[unlikely]] {                                                   // ? Mark as unlikely to be true for optimization
        return base;
    }

    // If exponent is > 1: x * x^(n-1) 
    return base * metode1(base, exponent - 1);
}



// Function to calculate the power of a number using recursion
constexpr double metode2(double base, int exponent) {                                                 
    // If exponent is 0 or negative: Error
    if (exponent <= 0) [[unlikely]] {                                                   // ? Mark as unlikely to be true for optimization   
        // We could expand the function to handle 0, but for the sake of the
        // exercise, we'll follow the instructions slavishly, and instead add
        // handling to prevent the function from returning unexpected results
        throw std::invalid_argument("Exponent must be a positive integer.");
    } 

    // If exponent is 1: x
    if (exponent == 1) [[unlikely]] {                                                   // ? Mark as unlikely to be true for optimization
        return base;
    }

    // If exponent is even: (x * x)^(n/2)
    else if (exponent % 2 == 0) {
        return metode2(base * base, exponent >> 1);                                     // ? Use bitwise right shift to divide exponent by 2
    } 
    
    // If exponent is odd: x * (x * x)^((n-1)/2)
    else {
        return (base * metode2(base*base, (exponent - 1) >> 1));                        // ? Use bitwise right shift to divide exponent by 2
    }
}



// Function to calculate the power of a number using the standard library. We 
// add a wrapper to match overhead of the first function call. This is also why I
// decided not to inline the function, as it could be optimized away by the compiler.
constexpr double metode3(double base, int exponent) {
    return std::pow(base, exponent);
}



// Helper function to validate the correctness of the three methods
void validateMethods() {
    // Constants for testing
    static const double  TEST_BASE         = 5.0;
    static const int     TEST_EXPONENT     = 11;
    static const double  EXPECTED_RESULT   = 48828125.0;                                       // 5^11 = 48828125

    // Method 1: 
    assert(EXPECTED_RESULT == metode1(TEST_BASE, TEST_EXPONENT));                       // Assert that the result is as expected
    std::cout << "Method 1 passed!" << "\n";
    
    // Method 2: 
    assert(EXPECTED_RESULT == metode2(TEST_BASE, TEST_EXPONENT));                       // Assert that the result is as expecte
    std::cout << "Method 2 passed!" << "\n";

    // Method 3: 
    assert(EXPECTED_RESULT == metode3(TEST_BASE, TEST_EXPONENT));                       // Assert that the result is as expected
    std::cout << "Method 3 passed!" << "\n\n";
}



// Use a template function to time the execution of a method
template <typename Function>
[[nodiscard]] double timeMethod(Function method, double base, int exponent) {
    // Initialize containers
    constexpr std::size_t BATCH_SIZE = 1'000;

    // NOTE: We save the result to a volatile variable to prevent the compiler from removing the function call during optimization 
    volatile double result = 0.0;
    std::size_t runCount = 0;

    // Start the clock
    const auto START_TIME = std::chrono::steady_clock::now();

    // Run the method until 1 second has passed and average the time taken for each run
    do {
        // For fast methods, the clock call and loop overhead dominate the actual power 
        // calculation, so we instead batch the calls and check the clock less often
        for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
            result = method(base, exponent);
            ++runCount;
        }
    } while (
        std::chrono::steady_clock::now() - START_TIME < std::chrono::seconds(1)
    );

    // Stop the clock
    const auto END_TIME = std::chrono::steady_clock::now();
    
    // Use result to avoid `-Wunused-but-set-variable` warning during compilation
    (void)result;
    
    // Calculate the total time taken in nanoseconds and return the average time per run
    const double totalNanoseconds = std::chrono::duration<double, std::nano>(END_TIME - START_TIME).count();
    return totalNanoseconds / static_cast<double>(runCount);
}



int main() {
    // Validate the correctness of the three methods
    validateMethods();

    // Benchmark with different problem sizes 
    std::cout << "----------------------------------------------------------------\n";
    std::cout << std::left
            << std::setw(14) << "Size (n)"
            << std::setw(18) << "Metode 1 (ns)"
            << std::setw(18) << "Metode 2 (ns)"
            << "Metode 3 (ns)\n";
    std::cout << "----------------------------------------------------------------\n";

    // Test increasing problem sizes to see scaling clearly
    const std::vector<int> exponents = {1'000, 2'000, 4'000, 8'000, 16'000, 32'000};
    
    for (std::size_t n : exponents) {
        std::cout << std::left << std::setw(14) << n
                               << std::setw(18) << timeMethod(metode1, 1.000001, static_cast<int>(n))
                               << std::setw(18) << timeMethod(metode2, 1.000001, static_cast<int>(n))
                                                << timeMethod(metode3, 1.000001, static_cast<int>(n))
                  << '\n';
    }
    
    return 0;
}