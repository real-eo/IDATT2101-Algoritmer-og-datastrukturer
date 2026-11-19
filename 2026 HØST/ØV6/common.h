// common.h - constants, hashing and file helpers shared by both programs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace compression {

// Both the encoder and the decoder must agree on every value here, otherwise
// the arithmetic coder desynchronises and the output is garbage.
namespace parameters {
constexpr int MIN_TABLE_BITS = 16;
constexpr int MAX_TABLE_BITS = 22;
constexpr int TABLE_BITS_MARGIN = 4; // headroom above ceil(log2(fileLength))
constexpr int MATCH_MIN = 6;
constexpr int MATCH_HASH_BITS = 22;
constexpr int MIXER_LEARNING_RATE = 1;
constexpr int MIXER2_LEARNING_RATE = 1;
constexpr int MIXER_SHIFT = 13;
constexpr int INITIAL_WEIGHT = 90000;
constexpr int APM_RATE = 6;
constexpr int COUNTER_LIMIT_LOW = 1023;
constexpr int COUNTER_LIMIT_HIGH = 400;
} // namespace parameters

inline std::uint32_t hash32(std::uint64_t a, std::uint64_t b = 0) {
    std::uint64_t h = a * 0x9E3779B97F4A7C15ULL + b * 0xD6E8FEB86659FD93ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return static_cast<std::uint32_t>(h);
}

inline std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Could not open %s\n", path.c_str());
        std::exit(1);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline void writeFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Could not write %s\n", path.c_str());
        std::exit(1);
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

// The original length is stored as a varint: 7 bits per byte, high bit = "more".
inline void writeVarInt(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 128) {
        out.push_back(static_cast<std::uint8_t>(value | 128));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

inline std::uint64_t readVarInt(const std::vector<std::uint8_t>& in, std::size_t& pos) {
    std::uint64_t value = 0;
    for (int shift = 0; pos < in.size() && shift < 64; shift += 7) {
        const std::uint8_t byte = in[pos++];
        value |= static_cast<std::uint64_t>(byte & 127) << shift;
        if ((byte & 128) == 0) {
            break;
        }
    }
    return value;
}

} // namespace compression