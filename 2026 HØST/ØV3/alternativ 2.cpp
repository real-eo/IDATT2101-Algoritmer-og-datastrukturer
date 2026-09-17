//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <functional>
#include <algorithm>
#include <execution>
#include <iostream>
#include <numeric>
#include <vector>
#include <random>
#include <limits>
#include <chrono>
#include <format>


// * Sorting function
// Sorts the vector in-place using Shell sort with a configurable gap divisor.
// The gap starts at n/2 and is divided by 'divisor' each pass, ending at 1.
void shellSort(std::vector<int>& containerReference, const double divisor = 2.2) {
    const std::size_t N = containerReference.size();
    std::size_t s = N / 2;

    while (s > 0) {
        // Gap-insertion sort: each element is moved left in gap-sized steps
        for (std::size_t i = s; i < N; ++i) {
            const int MOVE_VALUE = containerReference[i];                               // "flytt"
            std::size_t j = i;

            while (j >= s && MOVE_VALUE < containerReference[j - s]) {
                containerReference[j] = containerReference[j - s];
                j -= s;
            }
            containerReference[j] = MOVE_VALUE;
        }
        // Next gap: 1 -> 0 terminates the loop, 2 -> 1 (2 / 2.2 would truncate
        // to 0), and clamp to 1 so the sequence always passes through 1
        s = (s == 1) ? 0
          : (s == 2) ? 1
          : std::max<std::size_t>(1, static_cast<std::size_t>(s / divisor));
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

// Tests
namespace Test {
    [[nodiscard("Returns the pass-status of the checksum test.")]] 
    bool checksum(const std::vector<int>& containerReference, uint64_t expected) {
        // Check if the sum of the elements before and after sorting match to verify that no elements were lost or duplicated
        return sum(containerReference) == expected;
    }

    [[nodiscard("Returns the pass-status of the sequence test.")]]
    bool sequence(const std::vector<int>& containerReference) {
        // Check if the sequence is sorted in non-decreasing order
        return std::transform_reduce(                                                   // ? Use parallel unsequenced execution policy to leverage 
            std::execution::par_unseq,                                                  // ? multiple cores and SIMD instructions for better performance
            containerReference.begin(), containerReference.end() - 1,                   // Start and end indices, respectively, for the first operand
            containerReference.begin() + 1,                                             // Start index for the second operand
            true,                                                                       // ? Set the initial value to true since we're checking if
            std::logical_and{},                                                         // ? vec[n] <= vec[n+1] for all n connected in an AND-chain
            std::less_equal{});                                                         // Make std::transform_reduce use the <= operator
    }
}

// Benchmarks the Shell sort algorithm with a given divisor and prints the time taken and correctness of the sort
void benchmarkDivisor(
    const std::vector<int>& source,                                                     // ? Avoid PBV to prevent moving and let the function handle copying
    const double divisor, const uint64_t expectedSum
) {
    std::vector<int> data = source;                                                     // Copy the original data to avoid modifying it during sorting

    // Benchmark the sorting algorithm and measure the time taken
    const auto START_TIME = std::chrono::steady_clock::now();

    shellSort(data, divisor);                                                           // Sort the vector using Shell sort

    const auto END_TIME = std::chrono::steady_clock::now();

    // Validate the correctness of the sorting
    const bool ok = Test::sequence(data)                                                
                 && Test::checksum(data, expectedSum);                                  

    // Print the results in a formatted manner
    std::cout << std::format("divisor {:>4}: {:>10.1f} ms  {}",
        divisor,
        std::chrono::duration<double, std::milli>(END_TIME - START_TIME).count(),
        ok ? "OK" : "FAILED"
    );
}


int main() {
    // Initialize a vector to hold the random integers
    std::vector<int> data;
    constexpr std::size_t NUMBER_COUNT = 50'000'000;                                    // 50 million

    // Populate the vector with random numbers
    generateData(data, NUMBER_COUNT);

    // Save the sum of the vector before sorting to verify correctness
    const uint64_t EXPECTED_SUM = sum(data);

    // Benchmark the Shell sort algorithm with different divisors, and print the results
    std::cout << "Benchmarking Shell sort with different divisors:\n";
    for (const double divisor : {1.3, 1.5, 1.7, 2.0, 2.2, 2.5, 3.0}) {
        benchmarkDivisor(data, divisor, EXPECTED_SUM);
    }
    
    return 0;
}