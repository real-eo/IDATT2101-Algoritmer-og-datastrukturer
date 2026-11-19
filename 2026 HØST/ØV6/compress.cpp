// compress.cpp - compresses a file with context mixing and arithmetic coding.
// Usage: compress <input> [output]
// Without an output name, ".kmp" is appended: book.txt -> book.txt.kmp

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

/* // ! Single header to comply with the assignment. 
#include "arithmetic_coder.h"
#include "common.h"
#include "predictor.h"
*/

#include "header.h"

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "Usage: %s <input> [output]\n", argv[0]);
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = argc == 3 ? argv[2] : inputPath + ".kmp";

    const std::vector<std::uint8_t> input = compression::readFile(inputPath);
    std::vector<std::uint8_t> output;
    output.reserve(input.size() / 2 + 64);

    compression::writeVarInt(output, input.size());

    compression::ArithmeticEncoder encoder(output);
    compression::Predictor predictor(input.size());

    for (const std::uint8_t byte : input) {
        for (int bitIndex = 7; bitIndex >= 0; --bitIndex) {
            const int bit = (byte >> bitIndex) & 1;
            encoder.encode(bit, predictor.probability());
            predictor.update(bit);
        }
    }
    encoder.finish();

    compression::writeFile(outputPath, output);
    std::printf("%s -> %s: %zu -> %zu bytes (%.2f %%)\n", inputPath.c_str(), outputPath.c_str(), input.size(),
                output.size(),
                input.empty() ? 0.0 : 100.0 * static_cast<double>(output.size()) / static_cast<double>(input.size()));
    return 0;
}