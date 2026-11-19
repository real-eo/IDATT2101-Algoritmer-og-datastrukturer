// match_model.h - Lempel-Ziv style long-range predictor.
//
// Finds the most recent occurrence of the last `minLength` bytes and predicts
// that whatever followed it will follow again.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.h"
#include "counter.h"

namespace compression {

class MatchModel {
public:
    static constexpr int BUCKET_COUNT = 32;
    static constexpr int MAX_LENGTH = 65535;

    MatchModel(int minLength, int hashBits)
        : minLength_(minLength), hashBits_(hashBits), table_(static_cast<std::size_t>(1) << hashBits, -1) {
        for (auto& counter : counters_) {
            counter = AdaptiveCounter::INITIAL;
        }
    }

    // Called once per byte, after the byte has been appended to `history`.
    void advance(const std::vector<std::uint8_t>& history) {
        const int position = static_cast<int>(history.size());
        if (length_ > 0) {
            if (history[pointer_] == history[position - 1]) {
                if (length_ < MAX_LENGTH) {
                    ++length_;
                }
                ++pointer_;
            } else {
                length_ = 0;
            }
        }
        if (position >= minLength_) {
            std::uint32_t hash = 0;
            for (int k = 0; k < minLength_; ++k) {
                hash = hash * 0x2F0B4A13u + history[position - 1 - k] + 1u;
            }
            hash = hash32(hash) >> (32 - hashBits_);
            if (length_ == 0) {
                const int candidate = table_[hash];
                if (candidate > 0) {
                    int matched = 0;
                    while (matched < MAX_LENGTH && candidate - 1 - matched >= 0 &&
                           history[candidate - 1 - matched] == history[position - 1 - matched]) {
                        ++matched;
                    }
                    if (matched >= minLength_) {
                        length_ = matched;
                        pointer_ = candidate;
                    }
                }
            }
            table_[hash] = position;
        }
        expected_ = length_ > 0 ? history[pointer_] : -1;
    }

    void updateBit(int bit, int bitPosition) {
        if (expected_ >= 0 && ((expected_ >> (7 - bitPosition)) & 1) != bit) {
            expected_ = -1; // the match missed: stay silent for the rest of the byte
        }
    }

    int predictedBit(int bitPosition) const { return expected_ < 0 ? -1 : (expected_ >> (7 - bitPosition)) & 1; }

    // Same bucketing as the original model, kept so the fallback stays exact.
    int lengthBucket() const {
        const int length = length_;
        if (length < 16) return length;
        if (length < 32) return 16 + (length - 16) / 4;
        if (length < 512) return 20 + length / 64;
        return 31;
    }

    std::uint32_t& counter(int index) { return counters_[index]; }
    int counterIndex() const { return counterIndex_; }
    void setCounterIndex(int index) { counterIndex_ = index; }

private:
    int minLength_;
    int hashBits_;
    std::vector<int> table_;
    int pointer_ = 0;
    int length_ = 0;
    int expected_ = -1;
    int counterIndex_ = -1;
    std::uint32_t counters_[BUCKET_COUNT * 2]{};
};

} // namespace compression