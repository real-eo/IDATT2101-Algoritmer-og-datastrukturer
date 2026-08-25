//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <iostream>
#include "algoritme.hpp"


int main() {
    // Data structure to hold the stock price changes
    std::vector<signed char> kursforandringer;                                          // ? Named in Norwegian as "kursforandringer" is one word

    // Populate the vector with stock price changes
    generateData(kursforandringer, 10);                                                // Generate 100 random stock price changes

    // Output the generated stock price changes
    std::cout << "Generated stock price changes:\n";
    for (signed char change : kursforandringer) {
        std::cout << static_cast<int>(change) << " "; // Cast to int for better readability
    }
    std::cout << "\n\n";
    
    // Find the maximum profit possible with a single buy and sell operation
    Subarray result = maxSubarray(kursforandringer);

    // Output the result
    std::cout << "Start index: " << result.start << "\n";
    std::cout << "End index: " << result.end << "\n";

    return 0;
}
