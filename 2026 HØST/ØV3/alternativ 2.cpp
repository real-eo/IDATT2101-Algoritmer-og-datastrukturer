//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <algorithm>
#include <execution>
#include <iostream>
#include <numeric>
#include <vector>
#include <random>
#include <limits>


// * Testing functions
namespace Test {
    [[nodiscard("Returns the pass-status of the checksum test.")]] 
    bool checksum(const std::vector<int>& containerReference, uint64_t expected) {
        return true;
    }

    [[nodiscard("Returns the pass-status of the sequence test.")]] 
    bool sequence(const std::vector<int>& containerReference) {
        return true;
    }
}


// * Helper functions
// Populates a vector with random integers
void generateData(std::vector<int>& containerReference,  std::size_t count) {
    // Initialize a thread safe static generator to avoid reinitialization each function call
    static thread_local std::mt19937 generator(std::random_device{}());                 // ? Since std::mt19937 is mutable, marking it as just static 
                                                                                        // ? makes it not thread-safe. Using thread_local ensures that 
                                                                                        // ? each thread has its own instance of the generator, 
                                                                                        // ? preventing race conditions. We also use immediately-invoked 
                                                                                        // ? temporary construction to initialize a random seed from 
                                                                                        // ? std::random_device since it's only used once.
    // Generate random integers in the range [0, std::numeric_limits<int>::max()]
    static thread_local std::uniform_int_distribution<int> distribution(                // ? We can use the std::numeric_limits<int>::max() here even
        0, std::numeric_limits<int>::max()                                              // ? though we have 50 million elements because 50 million *
    );                                                                                  // ? 2^31-1 (max) is only about 0.582% of the maximum value of 
                                                                                        // ? a 64-bit integer, so it won't ever overflow when summing.
    // Resize the vector to avoid reallocation during population
    containerReference.resize(count);                                                   // NOTE: By using resize(), we have to iterate over the vector
                                                                                        // twice, once for resizing and once for populating. However,
    // Populate the vector with random numbers                                          // this is a trade-off worth doing for keeping the code simple 
    std::ranges::generate(containerReference, [&] {                                     // and more idiomatic. The preformance gain for 50 million ints
        return distribution(generator);                                                 // is negligible; ~200MB of memory with typical memory bandwidth
    });                                                                                 // of 20-30GB/s means that the time taken to populate the vector 
}                                                                                       // is in the order of milliseconds.

// Summs up all the integers in a vector and returns the sum
[[nodiscard("The sum should be used as the function is clean.")]] 
inline uint64_t sum(const std::vector<int>& containerReference) {
    return std::reduce(                                                                 // ? Use parallel unsequenced execution policy to leverage 
        std::execution::par_unseq,                                                      // ? multiple cores and SIMD instructions for better performance
        containerReference.begin(), containerReference.end(), 
        uint64_t{0}                                                                     // ? Initial value must be uint64_t to avoid implicit conversion
    );                                                                                  // ? which would lead to overflow for large sums. 
}


// * Sorting functions
void shellSort() {

}


int main() {
    // Initialize a vector to hold the random integers
    std::vector<int> data;
    constexpr std::size_t NUMBER_COUNT = 50'000'000;                                    // 50 million

    // Populate the vector with random numbers
    generateData(data, NUMBER_COUNT);

    // Save the sum of the vector before sorting to verify correctness
    const uint64_t EXPECTED_SUM = sum(data);

    // Sort the vector using Shell sort
    shellSort();

    // Verify the correctness of the sorting algorithm
    std::cout << "Checksum test ";
    std::cout << (Test::checksum(data, EXPECTED_SUM) ? "passed" : "failed") << "!\n";

    std::cout << "Sequence test ";
    std::cout << (Test::sequence(data) ? "passed" : "failed") << "!\n";
    
    return 0;
}