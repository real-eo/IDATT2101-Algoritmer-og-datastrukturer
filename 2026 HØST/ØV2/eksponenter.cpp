//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <stdexcept>
#include <iostream>
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
    else if (exponent % 2 == 0) {                                                       // ? Mark as likely to be true for optimization
        return metode2(base * base, exponent >> 1);                                     // ? Use bitwise right shift to divide exponent by 2
    } 
    
    // If exponent is odd: x * (x * x)^((n-1)/2)
    else {                                                                              // ? Mark as likely to be true for optimization
        return (base * metode2(base*base, (exponent - 1) >> 1));                        // ? Use bitwise right shift to divide exponent by 2
    }
}


int main() {
    // Control
    const double    TEST_BASE           = 5.0;
    const int       TEST_EXPONENT       = 11;
    const double    EXPECTED_RESULT     = 48828125.0;                                   // 5^11 = 48828125

    // Method 1: 
    std::cout << "Method 1: Recursive Power Function" << "\n";

    

    // Method 2: 
    std::cout << "Method 2: Optimized Recursive Power Function" << "\n";


    
    return 0;
}