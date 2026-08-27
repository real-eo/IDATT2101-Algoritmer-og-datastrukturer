//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <stdexcept>
#include <iostream>


// Function to calculate the power of a number using recursion
constexpr double pow(double base, int exponent) {                                                 
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

    // If exponent is even:  
    else if (exponent % 2 == 0) {                                                       // ? Mark as likely to be true for optimization
        return pow(base * base, exponent >> 1);                                         // ? Use bitwise right shift to divide exponent by 2
    } 
    
    // If exponent is odd:
    else {                                                                              // ? Mark as likely to be true for optimization
        return (base * pow(base*base, (exponent - 1) >> 1));                            // ? Use bitwise right shift to divide exponent by 2
    }
}


int main() {
    // Method 2: 
    std::cout << "Method 2: Second Recursive Power Function" << "\n";
    
    // Test cases
    try {
        std::cout << "2^3 = " << pow(2, 3) << "\n"; // Expected output: 8
        std::cout << "5^4 = " << pow(5, 4) << "\n"; // Expected output: 625
        std::cout << "3^1 = " << pow(3, 1) << "\n"; // Expected output: 3
        std::cout << "2^0 = " << pow(2, 0) << "\n"; // Should throw an exception
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }

    return 0;
}