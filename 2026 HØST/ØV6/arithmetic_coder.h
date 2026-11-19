// arithmetic_coder.h - binary arithmetic coder.
//
// The interval [low, high] is split by the 12-bit probability that the next bit
// is 1. The half matching the actual bit becomes the new interval. When the top
// bytes of low and high agree, that byte is fully determined and can be emitted.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compression {

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

} // namespace compression