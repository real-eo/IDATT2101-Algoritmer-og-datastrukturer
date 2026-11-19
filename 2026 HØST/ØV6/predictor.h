// predictor.h - the context-mixing model.
//
// For every bit:
//   1. Each context model looks up its context and contributes three inputs:
//      a direct counter, a bit-history state (via a StateMap) and a byte
//      prediction (the byte that came last in this context, and how often).
//   2. The match model contributes two inputs when a match is active.
//   3. A two-layer mixer combines everything into one probability.
//   4. Three APMs readjust it, and the result goes to the arithmetic coder.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "adaptive_probability_map.h"
#include "bit_history.h"
#include "common.h"
#include "counter.h"
#include "logistic.h"
#include "match_model.h"
#include "mixer.h"

namespace compression {

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