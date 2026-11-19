// adaptive_probability_map.h - secondary symbol estimation (SSE / APM).
//
// Each context owns a learned curve of 33 points over stretch(p). Systematic
// errors in the mixer are corrected by interpolating on that curve.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.h"
#include "logistic.h"

namespace compression {

class AdaptiveProbabilityMap {
public:
    explicit AdaptiveProbabilityMap(int contextCount) : table_(static_cast<std::size_t>(contextCount) * 33) {
        for (int context = 0; context < contextCount; ++context) {
            for (int point = 0; point < 33; ++point) {
                table_[static_cast<std::size_t>(context) * 33 + point] =
                    static_cast<std::uint16_t>(squash((point - 16) * 128) * 16);
            }
        }
    }

    int adjust(int probability, int context) {
        const int s = stretch(probability) + 2048;
        const int low = s >> 7;
        const int weight = s & 127;
        const int base = context * 33 + low;
        index_ = base + (weight >> 6); // the nearest point is the one we train
        return clampProbability((table_[base] * (128 - weight) + table_[base + 1] * weight) >> 11);
    }

    void update(int bit) {
        const int target = (bit << 16) + (bit << parameters::APM_RATE) - bit - bit;
        table_[index_] = static_cast<std::uint16_t>(table_[index_] + ((target - table_[index_]) >> parameters::APM_RATE));
    }

private:
    std::vector<std::uint16_t> table_;
    int index_ = 0;
};

} // namespace compression