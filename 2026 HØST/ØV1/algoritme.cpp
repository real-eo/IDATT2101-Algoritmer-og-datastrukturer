//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include "algoritme.hpp"
#include <algorithm>
#include <random>


// Generate random stock price changes and populate the provided vector
void generateData(std::vector<signed char>& containerReference,  std::size_t count) {
    // Initialize the random number generator                                           // ? Mark static to avoid reinitialization each function call
    static std::random_device randomDevice;
    static std::mt19937 generator(randomDevice()); 
    static std::uniform_int_distribution<int> distribution(-128, 127);                  // ? `signed char` goes from -128 to 127

    // Resize the vector to avoid reallocation during population
    containerReference.resize(count);

    // Populate the vector with random stock price changes
    std::ranges::generate(containerReference, [&] {
        return static_cast<signed char>(distribution(generator));
    });
}


// Find the maximum profit possible given a single buy and sell operation (maximum subarray)
Subarray maxSubarray(const std::vector<signed char>& kursforandringer) {
    // Check if the input vector is empty
    if (kursforandringer.empty()) {
        return {0, 0};                                                                  // No profit can be made from an empty array
    }

    // 1) Intitialize `maxSubarraySum` and `currentSubarraySum` with the first element, 
    //    and implicitly cast the `signed char` to `int` to avoid overflow during summation.
    int maxSubarraySum = kursforandringer[0];                                           // ? Implicit cast
    int currentSubarraySum = kursforandringer[0];                                       // ? Implicit cast

    //    Initialize a `Subarray` object to store the start and end indices of the maximum subarray
    //    and a `std::size_t` variable to track the start index of the current subarray being evaluated.
    Subarray maxSubarray{0, 0};
    std::size_t currentStartIndex = 0;

    // 2) Walk along the changes in price, adding each change to the `currentSubarraySum` variable  
    for (std::size_t i = 1; i < kursforandringer.size(); ++i) {
        // 3.1) If `currentSubarraySum` is negative, reset `currentSubarraySum` and the start index. This is because
        //      a negativesum would never contribute to a maximum profit as it would reduce the overall profit. 
        if (currentSubarraySum < 0) {
            currentSubarraySum = 0;
            currentStartIndex = i;
        }
        
        // Evaluate the current price AFTER the potential reset of `currentSubarraySum` to ensure that each 
        // negative value becomes the start of a new candidate subarray. This is crucial for correctly 
        // finding the maximum subarray in cases where the array doesn't contain a profitable subarray. 
        currentSubarraySum += kursforandringer[i];                                       // ? Implicit cast

        // 3.2) If `currentSubarraySum` is greater than `maxSubarraySum`, update `maxSubarraySum` 
        //      with `currentSubarraySum`, and update the indices to reflect the new maximum subarray.
        if (currentSubarraySum > maxSubarraySum) {
            maxSubarraySum = currentSubarraySum;
            maxSubarray.start = currentStartIndex;
            maxSubarray.end = i;
        }
    }

    // 4) Return the indicies of the maximum subarray as a `Subarray` object.
    return maxSubarray; 
}
