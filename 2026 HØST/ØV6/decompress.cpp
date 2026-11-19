// decompress.cpp - restores a file produced by compress.
// Usage: decompress <input> [output]
// Without an output name, ".kmp" is stripped: book.txt.kmp -> book.txt
// If the target already exists, " (1)", " (2)" ... is inserted before the extension.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector> 

/* // ! Single header to comply with the assignment.
#include "arithmetic_coder.h"
#include "common.h"
#include "predictor.h"
*/

#include "header.h"

namespace {

bool fileExists(const std::string& path) { return static_cast<bool>(std::ifstream(path, std::ios::binary)); }

std::string availableName(const std::string& path) {
    if (!fileExists(path)) {
        return path;
    }

    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t start = separator == std::string::npos ? 0 : separator + 1;
    std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot <= start) {
        dot = path.size(); // no extension
    }

    const std::string stem = path.substr(0, dot);
    const std::string extension = path.substr(dot);
    for (int n = 1;; ++n) {
        const std::string candidate = stem + " (" + std::to_string(n) + ")" + extension;
        if (!fileExists(candidate)) {
            return candidate;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "Usage: %s <input> [output]\n", argv[0]);
        return 1;
    }

    const std::string inputPath = argv[1];
    std::string outputPath;
    if (argc == 3) {
        outputPath = argv[2];
    } else {
        constexpr const char* SUFFIX = ".kmp";
        const std::size_t suffixLength = std::char_traits<char>::length(SUFFIX);
        if (inputPath.size() <= suffixLength ||
            inputPath.compare(inputPath.size() - suffixLength, suffixLength, SUFFIX) != 0) {
            std::fprintf(stderr, "%s does not end in .kmp - supply an output name: %s <input> <output>\n",
                         inputPath.c_str(), argv[0]);
            return 1;
        }
        outputPath = availableName(inputPath.substr(0, inputPath.size() - suffixLength));
    }

    const std::vector<std::uint8_t> input = compression::readFile(inputPath);
    std::size_t position = 0;
    const std::uint64_t length = compression::readVarInt(input, position);

    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(length));

    compression::ArithmeticDecoder decoder(input, position);
    compression::Predictor predictor(length);

    for (std::uint64_t n = 0; n < length; ++n) {
        int byte = 0;
        for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
            const int bit = decoder.decode(predictor.probability());
            predictor.update(bit);
            byte = (byte << 1) | bit;
        }
        output.push_back(static_cast<std::uint8_t>(byte));
    }

    compression::writeFile(outputPath, output);
    std::printf("%s -> %s: %zu bytes\n", inputPath.c_str(), outputPath.c_str(), output.size());
    return 0;
}