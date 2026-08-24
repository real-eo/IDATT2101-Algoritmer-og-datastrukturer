//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#ifndef ALGORITME_HPP
#define ALGORITME_HPP

#include <vector>


// Struct to hold the indicies of the maximum subarray
struct Subarray {
    std::size_t start;
    std::size_t end;

};

void generateData(std::vector<signed char>& containerReference, std::size_t count);
Subarray maxSubarray(const std::vector<signed char>& kursforandringer);


#endif // ALGORITME_HPP