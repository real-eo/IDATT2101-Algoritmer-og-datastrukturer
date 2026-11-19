// header.h - all headers combined into one file as per the assignment.
//
// Merged from: common.h, counter.h, logistic.h, mixer.h,
//              adaptive_probability_map.h, arithmetic_coder.h,
//              bit_history.h, match_model.h, predictor.h
//
// Sections appear in dependency order:
//   parameters/helpers -> counter -> logistic -> mixer -> APM
//   -> arithmetic coder -> bit history -> match model -> predictor
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace compression {



// common.h - constants, hashing and file helpers shared by both programs.
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



// counter.h - adaptive bit counter.
//
// 32 bits: the top 22 hold P(bit == 1), the low 10 hold the observation count.
// The step size is 1 / (n + 1.5), so fresh contexts learn fast and mature ones
// stay stable. The count stops at a per-context limit so the counter can still
// follow a change in the data.
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



// mixer.h - two-layer logistic mixer.
//
// Layer one holds several weight sets chosen by different contexts; each set
// produces one prediction. Layer two combines those predictions. Weights are
// trained with a gradient step: w += learningRate * error * input.
//
// The input count is a template parameter so the dot product and the weight
// update unroll into straight-line code. With a runtime count the compiler has
// to keep the loop, and these two loops are the hottest code in the program.
template <int InputCount>
class Mixer {
public:
    Mixer(int weightSetCount, int outputCount, int learningRate)
        : weights_(static_cast<std::size_t>(InputCount) * weightSetCount, parameters::INITIAL_WEIGHT / InputCount),
          selected_(outputCount),
          predictions_(outputCount),
          learningRate_(learningRate) {}

    void add(int value) { inputs_[inputIndex_++] = value; }

    int mix(int output, int weightSet) {
        const int offset = weightSet * InputCount;
        selected_[output] = offset;
        const int* weights = &weights_[offset];
        std::int64_t sum = 0;
        for (int i = 0; i < InputCount; ++i) {
            sum += static_cast<std::int64_t>(inputs_[i]) * weights[i];
        }
        const int d = std::clamp(static_cast<int>(sum >> 16), -2047, 2047);
        predictions_[output] = squash(d);
        return d; // still in the stretch domain, feeds the next layer
    }

    void update(int bit) {
        for (std::size_t k = 0; k < selected_.size(); ++k) {
            const int error = ((bit << 12) - predictions_[k]) * learningRate_;
            int* weights = &weights_[selected_[k]];
            for (int i = 0; i < InputCount; ++i) {
                weights[i] += static_cast<int>((static_cast<std::int64_t>(inputs_[i]) * error +
                                                (1 << (parameters::MIXER_SHIFT - 1))) >>
                                               parameters::MIXER_SHIFT);
            }
        }
        inputIndex_ = 0;
    }

private:
    std::vector<int> weights_;
    std::array<int, InputCount> inputs_{};
    std::vector<int> selected_;
    std::vector<int> predictions_;
    int inputIndex_ = 0;
    int learningRate_;
};



// adaptive_probability_map.h - secondary symbol estimation (SSE / APM).
//
// Each context owns a learned curve of 33 points over stretch(p). Systematic
// errors in the mixer are corrected by interpolating on that curve.
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



// arithmetic_coder.h - binary arithmetic coder.
//
// The interval [low, high] is split by the 12-bit probability that the next bit
// is 1. The half matching the actual bit becomes the new interval. When the top
// bytes of low and high agree, that byte is fully determined and can be emitted.
class ArithmeticEncoder {
public:
    explicit ArithmeticEncoder(std::vector<std::uint8_t>& output) : output_(output) {}

    void encode(int bit, int probability) {
        const std::uint32_t mid =
            low_ + static_cast<std::uint32_t>((static_cast<std::uint64_t>(high_ - low_) *
                                               static_cast<std::uint32_t>(probability)) >> 12);
        if (bit != 0) {
            high_ = mid;
        } else {
            low_ = mid + 1;
        }
        while (((low_ ^ high_) & 0xFF000000u) == 0) {
            output_.push_back(static_cast<std::uint8_t>(high_ >> 24));
            low_ <<= 8;
            high_ = (high_ << 8) | 255u;
        }
    }

    void finish() { output_.push_back(static_cast<std::uint8_t>(low_ >> 24)); }

private:
    std::uint32_t low_ = 0;
    std::uint32_t high_ = 0xFFFFFFFFu;
    std::vector<std::uint8_t>& output_;
};

class ArithmeticDecoder {
public:
    ArithmeticDecoder(const std::vector<std::uint8_t>& input, std::size_t start)
        : input_(input), position_(start) {
        for (int k = 0; k < 4; ++k) {
            value_ = (value_ << 8) | static_cast<std::uint32_t>(nextByte());
        }
    }

    int decode(int probability) {
        const std::uint32_t mid =
            low_ + static_cast<std::uint32_t>((static_cast<std::uint64_t>(high_ - low_) *
                                               static_cast<std::uint32_t>(probability)) >> 12);
        const int bit = value_ <= mid ? 1 : 0;
        if (bit != 0) {
            high_ = mid;
        } else {
            low_ = mid + 1;
        }
        while (((low_ ^ high_) & 0xFF000000u) == 0) {
            low_ <<= 8;
            high_ = (high_ << 8) | 255u;
            value_ = (value_ << 8) | static_cast<std::uint32_t>(nextByte());
        }
        return bit;
    }

private:
    // Past the end we pretend the file is padded with 0xFF, which keeps the
    // final value inside [low, high] so finish() only needs one byte.
    int nextByte() { return position_ < input_.size() ? input_[position_++] : 255; }

    std::uint32_t low_ = 0;
    std::uint32_t high_ = 0xFFFFFFFFu;
    std::uint32_t value_ = 0;
    const std::vector<std::uint8_t>& input_;
    std::size_t position_;
};



// bit_history.h - bit-history states and the StateMap that learns them.
//
// A state (one byte) remembers roughly how many zeros and ones a context has
// seen: (n0, n1). A one increments n1 and partially forgets n0, and vice versa.
// The StateMap then learns which probability each state really corresponds to,
// which beats any fixed formula.
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



// match_model.h - Lempel-Ziv style long-range predictor.
//
// Finds the most recent occurrence of the last `minLength` bytes and predicts
// that whatever followed it will follow again.
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



// predictor.h - the context-mixing model.
//
// For every bit:
//   1. Each context model looks up its context and contributes three inputs:
//      a direct counter, a bit-history state (via a StateMap) and a byte
//      prediction (the byte that came last in this context, and how often).
//   2. The match model contributes two inputs when a match is active.
//   3. A two-layer mixer combines everything into one probability.
//   4. Three APMs readjust it, and the result goes to the arithmetic coder.
class Predictor {
public:
    explicit Predictor(std::uint64_t fileLength)
        : tableBits_(tableBitsFor(fileLength)),
          tableMask_((1u << tableBits_) - 1),
          tableSize_(static_cast<std::size_t>(1) << tableBits_),
          runBits_(tableBits_ - 2),
          mixer_(WEIGHT_SET_COUNT, LAYER1_OUTPUTS, parameters::MIXER_LEARNING_RATE),
          mixer2_(1, 1, parameters::MIXER2_LEARNING_RATE),
          apm1_(256),
          apm2_(65536),
          apm3_(65536),
          matchModel_(parameters::MATCH_MIN, parameters::MATCH_HASH_BITS) {
        const std::size_t slotCount = tableSize_ * CONTEXT_COUNT;
#if defined(_WIN32)
        Slot* raw = static_cast<Slot*>(_aligned_malloc(slotCount * sizeof(Slot), 2 * 1024 * 1024));
        if (raw == nullptr) {
            std::fprintf(stderr, "Could not allocate the slot table\n");
            std::exit(1);
        }
        slots_.reset(raw);
#else
        slots_.reset(static_cast<Slot*>(std::aligned_alloc(2 * 1024 * 1024, slotCount * sizeof(Slot))));
#endif
        std::fill_n(slots_.get(), slotCount, Slot{AdaptiveCounter::INITIAL, 0, 0, 0});
        runTables_.assign((static_cast<std::size_t>(1) << runBits_) * CONTEXT_COUNT, 0);
        for (int i = 0; i < CONTEXT_COUNT; ++i) {
            for (auto& bucket : runCounters_[i]) {
                for (auto& counter : bucket) {
                    counter = AdaptiveCounter::INITIAL;
                }
            }
        }
        history_.reserve(static_cast<std::size_t>(fileLength));
        startByte();
        compute();
    }

    int probability() const { return prediction_; }

    void update(int bit) {
        const BitHistory& history = bitHistory();
        for (int i = 0; i < CONTEXT_COUNT; ++i) {
            Slot& slot = *activeSlot_[i];
            AdaptiveCounter::update(slot.counter, bit, COUNTER_LIMITS[i]);
            stateMaps_[i].update(slot.state, bit);
            slot.state = static_cast<std::uint8_t>(history.next(slot.state, bit));
            if (activeRunIndex_[i] >= 0) {
                AdaptiveCounter::update(runCounters_[i][activeRunIndex_[i] >> 1][activeRunIndex_[i] & 1], bit, 1023);
            }
        }
        if (matchModel_.counterIndex() >= 0) {
            AdaptiveCounter::update(matchModel_.counter(matchModel_.counterIndex()), bit, 1023);
        }
        mixer_.update(bit);
        mixer2_.update(bit);
        apm1_.update(bit);
        apm2_.update(bit);
        apm3_.update(bit);

        matchModel_.updateBit(bit, bitPosition_);

        partialByte_ = (partialByte_ << 1) | bit;
        ++bitPosition_;
        if (bitPosition_ == 8) {
            const std::uint8_t byte = static_cast<std::uint8_t>(partialByte_);
            updateRunPredictions(byte);
            finishByte(byte);
            partialByte_ = 1;
            bitPosition_ = 0;
            startByte();
        }
        compute();
    }

private:
    // Same order as the original model: the mixer weight sets and the counter
    // limits are indexed by these values.
    enum ContextId : int {
        ORDER_0 = 0,
        ORDER_1,
        ORDER_2,
        ORDER_3,
        ORDER_4,
        ORDER_6,
        ORDER_8,
        WORD_0,
        WORD_0_1,
        ABOVE_1,
        COLUMN,
        ORDER_32,
        INDIRECT,
        LINE_HASH,
        WORD_1_1,
        WORD_0_2,
        TEXT_WORD,
        TEXT_ORDER_2,
        TWO_LINES,
        TEXT_ORDER_6,
        ORDER_5,
        ORDER_12,
        SPARSE_1_3,
        SPARSE_2_4,
        ORDER_16,
        SPARSE_1_4,
        WORD_0_1_2,
        ABOVE_2,
        SPARSE_1_2_4,
        SPARSE_1_3_5,
        WORD_1_2,
        LINE_HASH_2,
        CONTEXT_COUNT
    };

    static constexpr int INPUTS_PER_CONTEXT = 3;
    static constexpr int MIXER_INPUTS = CONTEXT_COUNT * INPUTS_PER_CONTEXT + 2 + 1;
    static constexpr int MATCH_STATE_COUNT = 9;
    static constexpr int LAYER1_OUTPUTS = 7;
    static constexpr int ORDER_STATE_COUNT = 8;

    // Layer-one weight sets: [match state x c0] [c1] [bit position x c2] [order state x match state]
    static constexpr int SET_MATCH = 0;
    static constexpr int SET_ORDER1 = SET_MATCH + MATCH_STATE_COUNT * 256;
    static constexpr int SET_BITPOS = SET_ORDER1 + 256;
    static constexpr int SET_ORDERS = SET_BITPOS + 8 * 128;
    static constexpr int SET_C0 = SET_ORDERS + ORDER_STATE_COUNT * MATCH_STATE_COUNT;
    static constexpr int SET_C1C0 = SET_C0 + 256;
    static constexpr int SET_C2C0 = SET_C1C0 + 65536;
    static constexpr int WEIGHT_SET_COUNT = SET_C2C0 + 16384;

    // Counter limit per context, in the same order as ContextId.
    static constexpr int COUNTER_LIMITS[CONTEXT_COUNT] = {
        parameters::COUNTER_LIMIT_LOW,      // ORDER_0
        parameters::COUNTER_LIMIT_LOW,      // ORDER_1
        parameters::COUNTER_LIMIT_LOW / 2,  // ORDER_2
        parameters::COUNTER_LIMIT_HIGH * 2, // ORDER_3
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_4
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_6
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_8
        parameters::COUNTER_LIMIT_HIGH * 2, // WORD_0
        parameters::COUNTER_LIMIT_HIGH * 2, // WORD_0_1
        parameters::COUNTER_LIMIT_HIGH * 2, // ABOVE_1
        parameters::COUNTER_LIMIT_HIGH * 2, // COLUMN
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_32
        parameters::COUNTER_LIMIT_HIGH * 2, // INDIRECT
        parameters::COUNTER_LIMIT_HIGH * 2, // LINE_HASH
        parameters::COUNTER_LIMIT_HIGH * 2, // WORD_1_1
        parameters::COUNTER_LIMIT_HIGH * 2, // WORD_0_2
        parameters::COUNTER_LIMIT_HIGH * 2, // TEXT_WORD
        parameters::COUNTER_LIMIT_HIGH * 2, // TEXT_ORDER_2
        parameters::COUNTER_LIMIT_HIGH * 2, // TWO_LINES
        parameters::COUNTER_LIMIT_HIGH * 2, // TEXT_ORDER_6
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_5
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_12
        parameters::COUNTER_LIMIT_HIGH,     // SPARSE_1_3
        parameters::COUNTER_LIMIT_HIGH,     // SPARSE_2_4
        parameters::COUNTER_LIMIT_HIGH,     // ORDER_16
        parameters::COUNTER_LIMIT_HIGH,     // SPARSE_1_4
        parameters::COUNTER_LIMIT_HIGH,     // WORD_0_1_2
        parameters::COUNTER_LIMIT_HIGH,     // ABOVE_2
        parameters::COUNTER_LIMIT_HIGH,     // SPARSE_1_2_4
        parameters::COUNTER_LIMIT_HIGH,     // SPARSE_1_3_5
        parameters::COUNTER_LIMIT_HIGH,     // WORD_1_2
        parameters::COUNTER_LIMIT_HIGH      // LINE_HASH_2
    };

    static int tableBitsFor(std::uint64_t length) {
        int bits = 0;
        while (bits < 40 && (std::uint64_t(1) << bits) < length) {
            ++bits;
        }
        return std::clamp(bits + parameters::TABLE_BITS_MARGIN, parameters::MIN_TABLE_BITS,
                          parameters::MAX_TABLE_BITS);
    }

    static bool isLetter(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128; }
    static int toLower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

    // 1 + lengthBucket / 4 for 9 states, 1 + lengthBucket * 15 / 32 for 16.
    static int matchStateFor(int lengthBucket) {
        return 1 + (lengthBucket * (MATCH_STATE_COUNT - 1)) / 32;
    }

    int tableBits_;
    std::uint32_t tableMask_;
    std::size_t tableSize_;
    int runBits_;

    Mixer<MIXER_INPUTS> mixer_;
    Mixer<LAYER1_OUTPUTS + 1> mixer2_;
    AdaptiveProbabilityMap apm1_, apm2_, apm3_;
    MatchModel matchModel_;

    std::vector<std::uint8_t> history_;
    std::uint64_t recentBytes_ = 0; // the last 8 bytes, newest in the low byte
    int bitPosition_ = 0;
    int partialByte_ = 1; // bits seen so far, with a leading 1

    std::uint32_t contextHash_[CONTEXT_COUNT]{};
    std::uint32_t contextBase_[CONTEXT_COUNT]{}; // contextHash_ masked, refreshed once per byte
    std::uint32_t order2Hash_ = 0;               // hash32 of the last two bytes, refreshed once per byte
    // One 8-byte slot per context: counter (4 B) + bit-history state (1 B) + owner
    // check (1 B) + padding. Keeping them in a single cache line means the per-bit
    // lookup touches one line instead of two, and the 32 slots for a given bit are
    // contiguous, so the whole loop walks 256 bytes of sequential memory.
    struct Slot {
        std::uint32_t counter;
        std::uint8_t state;
        std::uint8_t check;
        std::uint16_t padding;
    };
    static_assert(sizeof(Slot) == 8, "Slot must stay 8 bytes so a context lookup is one cache line");
    // The slot table is hundreds of megabytes and is accessed at random, so it
    // misses the TLB as well as the cache. Backing it with large pages cuts the
    // number of TLB entries by 512x. Falls back to a normal allocation.
    struct LargePageDeleter {
        void operator()(Slot* pointer) const {
#if defined(_WIN32)
            _aligned_free(pointer);
#else
            std::free(pointer);
#endif
        }
    };
    std::unique_ptr<Slot[], LargePageDeleter> slots_; // CONTEXT_COUNT blocks of tableSize_
    StateMap stateMaps_[CONTEXT_COUNT];
    std::vector<std::uint32_t> runTables_; // CONTEXT_COUNT blocks of (1 << runBits_)
    std::uint32_t runCounters_[CONTEXT_COUNT][32][2]{};
    Slot* activeSlot_[CONTEXT_COUNT]{};
    std::uint32_t* activeRun_[CONTEXT_COUNT]{};
    int activeRunIndex_[CONTEXT_COUNT]{};

    std::uint32_t word0_ = 0, word1_ = 0, word2_ = 0;
    int lineStart_ = 0, previousLineStart_ = 0;
    std::uint32_t lineHash_ = 0, previousLineHash_ = 0, lineHash2_ = 0;
    int lineType_ = 0; // 0 = unknown, 1 = '\', 2 = '<', 3 = text, 4 = '#'/'%'
    std::uint64_t textBytes_ = 0;
    std::uint32_t textWord_ = 0;
    std::array<std::uint16_t, 65536> indirect_{};

    int prediction_ = 2048;

    // Recompute every context hash at the start of a byte.
    void startByte() {
        const int position = static_cast<int>(history_.size());
        const std::uint64_t c1 = recentBytes_ & 0xFF;
        const std::uint64_t c2 = recentBytes_ & 0xFFFF;
        const int column = position - lineStart_;
        int above = 0;
        if (previousLineStart_ + column < lineStart_) {
            above = history_[previousLineStart_ + column];
        }

        std::uint64_t order32 = 32;
        std::uint64_t order12 = 12;
        std::uint64_t order16 = 16;
        for (int k = 1; k <= 32 && k <= position; ++k) {
            order32 = (order32 + history_[position - k] + 1) * 0x100000001B3ULL;
            if (k <= 12) {
                order12 = (order12 + history_[position - k] + 1) * 0x100000001B3ULL;
            }
            if (k <= 16) {
                order16 = (order16 + history_[position - k] + 1) * 0x100000001B3ULL;
            }
        }

        order2Hash_ = hash32(recentBytes_ & 0xFFFF);

        // The first argument keeps the contexts from colliding with each other.
        contextHash_[ORDER_0] = 0;
        contextHash_[ORDER_1] = hash32(1, c1);
        contextHash_[ORDER_2] = hash32(2, c2);
        contextHash_[ORDER_3] = hash32(3, recentBytes_ & 0xFFFFFF);
        contextHash_[ORDER_4] = hash32(4, recentBytes_ & 0xFFFFFFFF);
        contextHash_[ORDER_6] = hash32(6, recentBytes_ & 0xFFFFFFFFFFFFULL);
        contextHash_[ORDER_8] = hash32(8, recentBytes_);
        contextHash_[WORD_0] = hash32(10, word0_ + (word0_ != 0 ? 0 : c1 << 24));
        contextHash_[WORD_0_1] = hash32(11, word0_ + (static_cast<std::uint64_t>(word1_) << 32));
        contextHash_[ABOVE_1] = hash32(12, (static_cast<std::uint64_t>(above) << 8) | c1);
        contextHash_[COLUMN] =
            hash32(13, (static_cast<std::uint64_t>(std::min(column, 63)) << 8) | static_cast<std::uint32_t>(above));
        contextHash_[ORDER_32] = hash32(24, order32);
        contextHash_[INDIRECT] = hash32(21, (static_cast<std::uint64_t>(indirect_[c2]) << 16) | c2);
        contextHash_[LINE_HASH] = hash32(18, (static_cast<std::uint64_t>(lineHash_) << 8) | c1);
        contextHash_[WORD_1_1] = hash32(19, word1_ + (c1 << 32));
        contextHash_[WORD_0_2] = hash32(17, word0_ + (static_cast<std::uint64_t>(word2_) << 32));
        contextHash_[TEXT_WORD] =
            hash32(37, textWord_ + ((textBytes_ & 0xFF) << 32) + (static_cast<std::uint64_t>(lineType_) << 40));
        contextHash_[TEXT_ORDER_2] = hash32(34, (textBytes_ & 0xFFFF) | (static_cast<std::uint64_t>(lineType_) << 16) |
                                                    (static_cast<std::uint64_t>(std::min(column, 3)) << 20));
        contextHash_[TWO_LINES] =
            hash32(29, (static_cast<std::uint64_t>(hash32(previousLineHash_, lineHash2_)) << 32) | lineHash_);
        contextHash_[TEXT_ORDER_6] = hash32(36, textBytes_ & 0xFFFFFFFFFFFFULL);
        contextHash_[ORDER_5] = hash32(5, recentBytes_ & 0xFFFFFFFFFFULL);
        contextHash_[ORDER_12] = hash32(12, order12);
        contextHash_[SPARSE_1_3] = hash32(40, (recentBytes_ & 0xFF) | ((recentBytes_ >> 8) & 0xFF00));
        contextHash_[SPARSE_2_4] = hash32(41, ((recentBytes_ >> 8) & 0xFF) | ((recentBytes_ >> 16) & 0xFF00));
        contextHash_[ORDER_16] = hash32(16, order16);
        contextHash_[SPARSE_1_4] = hash32(42, (recentBytes_ & 0xFF) | ((recentBytes_ >> 16) & 0xFF00));
        contextHash_[WORD_0_1_2] = hash32(43, hash32(word0_, word1_) + (static_cast<std::uint64_t>(word2_) << 32));
        contextHash_[ABOVE_2] = hash32(44, (static_cast<std::uint64_t>(above) << 16) | c2);
        contextHash_[SPARSE_1_2_4] = hash32(45, (recentBytes_ & 0xFF) | ((recentBytes_ >> 8) & 0xFF00) | ((recentBytes_ >> 16) & 0xFF0000));
        contextHash_[SPARSE_1_3_5] = hash32(46, (recentBytes_ & 0xFF) | ((recentBytes_ >> 16) & 0xFF00) | ((recentBytes_ >> 24) & 0xFF0000));
        contextHash_[WORD_1_2] = hash32(47, hash32(word1_, word2_));
        contextHash_[LINE_HASH_2] = hash32(48, (static_cast<std::uint64_t>(lineHash_) << 16) | c2);

        for (int i = 0; i < CONTEXT_COUNT; ++i) {
            activeRun_[i] = &runTables_[(static_cast<std::size_t>(i) << runBits_) +
                                        ((contextHash_[i] * 0x2F0B4A13u + static_cast<std::uint32_t>(i)) >>
                                         (32 - runBits_))];
            contextBase_[i] = contextHash_[i] & tableMask_;
        }
    }

    // One context model's contribution. I is a compile-time constant, so the
    // table base, the counter limit and the ORDER_0 special case all fold away.
    //
    // Split into two phases so the 32 slot lookups are issued back to back. Each
    // lookup is a DRAM miss, and a miss takes hundreds of cycles; interleaving the
    // dependent work with the loads serialises them. Resolving every slot first
    // lets the memory-level parallelism overlap the misses instead.
    template <std::size_t I>
    void resolveSlot() {
        constexpr int ID = static_cast<int>(I);
        const std::uint32_t index =
            (ID == ORDER_0) ? static_cast<std::uint32_t>(partialByte_)
                            : (contextBase_[ID] + static_cast<std::uint32_t>(partialByte_) * 0x9E3779B1u) & tableMask_;
        std::size_t slot = static_cast<std::size_t>(ID) * tableSize_ + index;
        if constexpr (ID != ORDER_0) { // ORDER_0 is indexed directly and never collides
            // Two-slot bucket (slot, slot ^ 1) with an 8-bit check from the top of the
            // context hash. A slot is only trusted if its check matches; otherwise the
            // less-trained slot of the pair is reset and taken over. Without this, two
            // contexts that hash to the same slot silently share one counter and state.
            const std::uint8_t check = static_cast<std::uint8_t>(contextHash_[ID] >> 24);
            if (slots_[slot].check != check) {
                const std::size_t other = slot ^ 1;
                if (slots_[other].check == check) {
                    slot = other;
                } else {
                    if ((slots_[other].counter & 1023) < (slots_[slot].counter & 1023)) {
                        slot = other;
                    }
                    slots_[slot].counter = AdaptiveCounter::INITIAL;
                    slots_[slot].state = 0;
                    slots_[slot].check = check;
                }
            }
        }
        activeSlot_[ID] = &slots_[slot];
    }

    template <std::size_t I>
    void contextInput(int& seenOrders) {
        constexpr int ID = static_cast<int>(I);
        Slot& active = *activeSlot_[ID];
        if constexpr (ID >= ORDER_2 && ID <= ORDER_8) {
            if (active.state != 0) {
                ++seenOrders;
            }
        }
        mixer_.add(stretch(AdaptiveCounter::probability(active.counter)));
        mixer_.add(stretch(stateMaps_[ID].probability(active.state)));

        // Byte prediction: only useful while the bits so far still agree.
        activeRunIndex_[ID] = -1;
        int runInput = 0;
        const std::uint32_t run = *activeRun_[ID];
        if ((run >> 16) == (contextHash_[ID] >> 16) && (run & 0xFF00) != 0) {
            const int byte = static_cast<int>(run & 0xFF);
            if (((byte | 256) >> (8 - bitPosition_)) == partialByte_) {
                const int count = static_cast<int>((run >> 8) & 0xFF);
                const int bucket = count < 16 ? count : (count < 64 ? 16 + (count - 16) / 4 : 31);
                const int predicted = (byte >> (7 - bitPosition_)) & 1;
                activeRunIndex_[ID] = bucket * 2 + predicted;
                runInput = stretch(AdaptiveCounter::probability(runCounters_[ID][bucket][predicted]));
            }
        }
        mixer_.add(runInput);
    }

    // Produce the prediction for the next bit.
    void compute() {
        int seenOrders = 0;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (resolveSlot<I>(), ...);
            (contextInput<I>(seenOrders), ...);
        }(std::make_index_sequence<CONTEXT_COUNT>{});

        const int predicted = matchModel_.predictedBit(bitPosition_);
        int lengthBucket = 0;
        if (predicted >= 0) {
            lengthBucket = matchModel_.lengthBucket();
            matchModel_.setCounterIndex(lengthBucket * 2 + predicted);
            mixer_.add(
                stretch(clampProbability(AdaptiveCounter::probability(matchModel_.counter(matchModel_.counterIndex())))));
            mixer_.add(predicted != 0 ? lengthBucket * 32 : -lengthBucket * 32);
        } else {
            matchModel_.setCounterIndex(-1);
            mixer_.add(0);
            mixer_.add(0);
        }
        mixer_.add(256); // bias

        const int matchState = predicted < 0 ? 0 : matchStateFor(lengthBucket);
        const int c1 = static_cast<int>(recentBytes_ & 0xFF);
        const int c2 = static_cast<int>((recentBytes_ >> 8) & 0xFF);
        mixer2_.add(mixer_.mix(0, SET_MATCH + matchState * 256 + partialByte_));
        mixer2_.add(mixer_.mix(1, SET_ORDER1 + c1));
        mixer2_.add(mixer_.mix(2, SET_BITPOS + bitPosition_ * 128 + (c2 & 127)));
        mixer2_.add(mixer_.mix(3, SET_ORDERS + std::min(seenOrders, ORDER_STATE_COUNT - 1) * MATCH_STATE_COUNT +
                                      matchState));
        mixer2_.add(mixer_.mix(4, SET_C0 + partialByte_));
        mixer2_.add(mixer_.mix(5, SET_C1C0 + (c1 << 8) + partialByte_));
        mixer2_.add(mixer_.mix(6, SET_C2C0 + static_cast<int>(order2Hash_ & (16384 - 1))));
        mixer2_.add(256);
        const int mixed = clampProbability(squash(mixer2_.mix(0, 0)));

        // SSE: readjust with order 0, order 1 and a hashed order 2.
        const int a1 = apm1_.adjust(mixed, partialByte_);
        const int a2 = apm2_.adjust(mixed, partialByte_ | (c1 << 8));
        const int a3 = apm3_.adjust(
            mixed, static_cast<int>((order2Hash_ & 0xFF00) | static_cast<std::uint32_t>(partialByte_)));
        prediction_ = clampProbability((2 * mixed + a1 + 3 * a2 + 2 * a3 + 4) >> 3);
    }

    // Each context remembers the last byte it produced and how often in a row.
    void updateRunPredictions(std::uint8_t byte) {
        for (int i = 0; i < CONTEXT_COUNT; ++i) {
            std::uint32_t& run = *activeRun_[i];
            const std::uint32_t check = contextHash_[i] >> 16;
            if ((run >> 16) == check && (run & 0xFF) == byte) {
                std::uint32_t count = (run >> 8) & 0xFF;
                if (count < 255) {
                    ++count;
                }
                run = (check << 16) | (count << 8) | byte;
            } else {
                run = (check << 16) | (1u << 8) | byte;
            }
        }
    }

    // Update everything that depends on whole bytes.
    void finishByte(std::uint8_t byte) {
        history_.push_back(byte);
        const std::uint32_t pair = static_cast<std::uint32_t>(recentBytes_ & 0xFFFF);
        indirect_[pair] = static_cast<std::uint16_t>((indirect_[pair] << 8) | byte);
        recentBytes_ = (recentBytes_ << 8) | byte;
        const int position = static_cast<int>(history_.size());

        if (isLetter(byte)) {
            word0_ = hash32(word0_ + static_cast<std::uint32_t>(toLower(byte)), 7);
        } else if (word0_ != 0) {
            word2_ = word1_;
            word1_ = word0_;
            word0_ = 0;
        }

        if (position - 1 == lineStart_) {
            lineType_ = byte == '\\' ? 1 : byte == '<' ? 2 : (byte == '#' || byte == '%') ? 4 : 3;
        }
        if (lineType_ == 3) {
            textBytes_ = (textBytes_ << 8) | byte;
            textWord_ = isLetter(byte) ? hash32(textWord_ + static_cast<std::uint32_t>(toLower(byte)), 11) : 0;
        }

        if (byte == '\n') {
            lineType_ = 0;
            previousLineStart_ = lineStart_;
            lineStart_ = position;
            lineHash2_ = previousLineHash_;
            previousLineHash_ = lineHash_;
            lineHash_ = 0;
        } else if (position - lineStart_ <= 16) {
            lineHash_ = hash32(lineHash_, byte);
        }

        matchModel_.advance(history_);
    }
};

} // namespace compression