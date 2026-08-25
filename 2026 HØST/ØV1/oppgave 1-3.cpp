//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include "algoritme.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>


int main() {
    // Benchmark with different problem sizes 
    std::cout << "---------------------------\n";
    std::cout << std::left << std::setw(14) << "Size (n)" << "Runtime (ms)\n";
    std::cout << "---------------------------\n";

    // Test: n = 10M, 20M, 40M, and 80M to see scaling clearly
    std::vector<std::size_t> sizes = {10'000'000, 20'000'000, 40'000'000, 80'000'000};
    std::vector<signed char> kursforandringer;

    for (std::size_t n : sizes) {
        // Generate data before timing to avoid contaminating the measurements
        generateData(kursforandringer, n);

        // Start the clock
        auto startTime = std::chrono::high_resolution_clock::now();

        // NOTE: We save the result to a volatile variable to prevent the compiler from removing the function call during optimization 
        volatile Subarray result = maxSubarray(kursforandringer);

        // Stop the clock
        auto stopTime = std::chrono::high_resolution_clock::now();
        
        // Calculate duration in milliseconds
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count();
        std::cout << std::left << std::setw(14) << n << duration << " ms\n";

        // Use result to avoid `-Wunused-but-set-variable` warning during compilation
        (void)result;
    }
    

    return 0;
}
