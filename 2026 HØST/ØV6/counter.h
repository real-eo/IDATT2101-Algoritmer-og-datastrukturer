// counter.h - adaptive bit counter.
//
// 32 bits: the top 22 hold P(bit == 1), the low 10 hold the observation count.
// The step size is 1 / (n + 1.5), so fresh contexts learn fast and mature ones
// stay stable. The count stops at a per-context limit so the counter can still
// follow a change in the data.
#pragma once

#include <array>
#include <cstdint>

namespace compression {

namespace detail {

// 65536 / (i + 1.5), built at compile time. A function-local `static` would add
// a thread-safe guard check to every one of the ~500 million counter updates.
inline constexpr std::array<int, 1024> makeReciprocals() {
    std::array<int, 1024> values{};
    for (int i = 0; i < 1024; ++i) {
        values[i] = 65536 * 2 / (2 * i + 3);
    }
    return values;
}

inline constexpr auto RECIPROCALS = makeReciprocals();

} // namespace detail

class AdaptiveCounter {
public:
    static constexpr std::uint32_t INITIAL = 1u << 31; // p = 0.5, n = 0

    static int probability(std::uint32_t state) { return static_cast<int>(state >> 20); }

    static void update(std::uint32_t& state, int bit, int limit) {
        const int count = static_cast<int>(state & 1023);
        int p = static_cast<int>(state >> 10);
        const int target = bit != 0 ? (1 << 22) - 1 : 0;
        p += static_cast<int>((static_cast<std::int64_t>(target - p) * detail::RECIPROCALS[count]) >> 16);
        const int nextCount = count < limit ? count + 1 : count;
        state = (static_cast<std::uint32_t>(p) << 10) | static_cast<std::uint32_t>(nextCount);
    }
};

} // namespace compression
