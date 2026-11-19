// mixer.h - two-layer logistic mixer.
//
// Layer one holds several weight sets chosen by different contexts; each set
// produces one prediction. Layer two combines those predictions. Weights are
// trained with a gradient step: w += learningRate * error * input.
//
// The input count is a template parameter so the dot product and the weight
// update unroll into straight-line code. With a runtime count the compiler has
// to keep the loop, and these two loops are the hottest code in the program.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.h"
#include "logistic.h"

namespace compression {

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

} // namespace compression
