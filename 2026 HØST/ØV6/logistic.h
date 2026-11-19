// logistic.h - fixed-point logistic helpers.
//
// squash(d)  = 4096 / (1 + e^(-d/256))   logit -> 12-bit probability
// stretch(p) = ln(p / (1 - p)) * 256     the inverse
//
// The mixer works in the stretch domain, where contributions from different
// models can simply be added.
//
// Both tables are built at compile time. A function-local `static` table would
// be initialised once at run time but still carry a thread-safe guard check on
// every call, and stretch() is called roughly a hundred times per coded bit.
#pragma once

#include <array>
#include <cstdint>

namespace compression {

namespace detail {

inline constexpr std::array<int, 33> SQUASH_TABLE = {
    1,    2,    4,    6,    10,   17,   27,   45,   74,   120,  194,  311,  488,  747,  1102, 1546, 2048,
    2550, 2994, 3349, 3608, 3785, 3902, 3976, 4022, 4051, 4069, 4079, 4086, 4090, 4092, 4094, 4095};

constexpr int squashValue(int d) {
    if (d > 2047) {
        return 4095;
    }
    if (d < -2047) {
        return 1;
    }
    const int weight = d & 127;
    const int index = (d >> 7) + 16;
    return (SQUASH_TABLE[index] * (128 - weight) + SQUASH_TABLE[index + 1] * weight + 64) >> 7;
}

constexpr std::array<std::int16_t, 4096> makeStretchTable() {
    std::array<std::int16_t, 4096> values{};
    int previous = 0;
    for (int d = -2047; d <= 2047; ++d) {
        const int value = squashValue(d);
        for (int j = previous; j <= value; ++j) {
            values[j] = static_cast<std::int16_t>(d);
        }
        previous = value + 1;
    }
    for (int j = previous; j < 4096; ++j) {
        values[j] = 2047;
    }
    return values;
}

inline constexpr auto STRETCH_TABLE = makeStretchTable();

} // namespace detail

inline int squash(int d) { return detail::squashValue(d); }

// stretch(0) == stretch(1) == -2047, so callers never need to clamp p to >= 1
// before calling this.
inline int stretch(int p) { return detail::STRETCH_TABLE[p]; }

inline int clampProbability(int p) { return p < 1 ? 1 : (p > 4095 ? 4095 : p); }

} // namespace compression
