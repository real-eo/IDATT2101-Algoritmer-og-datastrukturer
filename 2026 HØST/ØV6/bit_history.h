// bit_history.h - bit-history states and the StateMap that learns them.
//
// A state (one byte) remembers roughly how many zeros and ones a context has
// seen: (n0, n1). A one increments n1 and partially forgets n0, and vice versa.
// The StateMap then learns which probability each state really corresponds to,
// which beats any fixed formula.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "counter.h"

namespace compression {

class BitHistory {
public:
    static constexpr int LIMIT = 30;
    static constexpr int MAX_STATES = 256;

    BitHistory() {
        std::array<std::array<int, LIMIT + 1>, LIMIT + 1> index{};
        for (auto& row : index) {
            row.fill(-1);
        }

        // Breadth-first from (0, 0) gives a fixed, deterministic numbering.
        std::vector<std::pair<int, int>> queue{{0, 0}};
        index[0][0] = 0;
        for (std::size_t k = 0; k < queue.size(); ++k) {
            const auto [zeros, ones] = queue[k];
            zeroCount_[k] = static_cast<std::uint8_t>(zeros);
            oneCount_[k] = static_cast<std::uint8_t>(ones);
            for (int bit = 0; bit < 2; ++bit) {
                const int nextZeros = bit != 0 ? forget(zeros) : (zeros < LIMIT ? zeros + 1 : LIMIT);
                const int nextOnes = bit != 0 ? (ones < LIMIT ? ones + 1 : LIMIT) : forget(ones);
                if (index[nextZeros][nextOnes] < 0) {
                    if (queue.size() < MAX_STATES) {
                        index[nextZeros][nextOnes] = static_cast<int>(queue.size());
                        queue.emplace_back(nextZeros, nextOnes);
                    } else {
                        index[nextZeros][nextOnes] = 0; // table full: fold into the root state
                    }
                }
                next_[k][bit] = static_cast<std::uint8_t>(index[nextZeros][nextOnes]);
            }
        }
        stateCount_ = static_cast<int>(queue.size());
    }

    int stateCount() const { return stateCount_; }
    int zeroCount(int state) const { return zeroCount_[state]; }
    int oneCount(int state) const { return oneCount_[state]; }
    int next(int state, int bit) const { return next_[state][bit]; }

private:
    static int forget(int n) { return n > 2 ? n / 2 + 1 : n; }

    std::uint8_t next_[MAX_STATES][2]{};
    std::uint8_t zeroCount_[MAX_STATES]{};
    std::uint8_t oneCount_[MAX_STATES]{};
    int stateCount_ = 0;
};

inline const BitHistory& bitHistory() {
    static const BitHistory instance;
    return instance;
}

class StateMap {
public:
    StateMap() {
        const BitHistory& history = bitHistory();
        for (int state = 0; state < BitHistory::MAX_STATES; ++state) {
            int p = 1 << 21;
            if (state < history.stateCount()) {
                const int zeros = history.zeroCount(state);
                const int ones = history.oneCount(state);
                p = ((ones * 2 + 1) << 21) / (zeros + ones + 1);
            }
            if (p > (1 << 22) - 1) {
                p = (1 << 22) - 1;
            }
            table_[state] = static_cast<std::uint32_t>(p) << 10;
        }
    }

    int probability(int state) const { return AdaptiveCounter::probability(table_[state]); }
    void update(int state, int bit) { AdaptiveCounter::update(table_[state], bit, 1023); }

private:
    std::uint32_t table_[BitHistory::MAX_STATES]{};
};

} // namespace compression