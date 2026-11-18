#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace routing {

/// Reads an entire file into memory. The map files are hundreds of megabytes, and one
/// bulk read followed by pointer scanning is far faster than formatted line input.
[[nodiscard]] inline std::vector<char> readWholeFile(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) throw std::runtime_error{"kan ikke åpne " + path.string()};

    const std::streamsize size = stream.tellg();
    if (size < 0) throw std::runtime_error{"kan ikke lese størrelsen på " + path.string()};

    std::vector<char> buffer(static_cast<std::size_t>(size) + 1, '\n');
    stream.seekg(0);
    stream.read(buffer.data(), size);
    if (!stream) throw std::runtime_error{"kan ikke lese " + path.string()};
    return buffer;
}

/// Splits a text buffer into lines without allocating.
class LineScanner {
public:
    explicit LineScanner(std::string_view text) noexcept
        : cursor_{text.data()}, end_{text.data() + text.size()} {}

    [[nodiscard]] bool next(std::string_view& line) noexcept {
        while (cursor_ < end_) {
            const char* start = cursor_;
            while (cursor_ < end_ && *cursor_ != '\n') ++cursor_;
            line = std::string_view{start, static_cast<std::size_t>(cursor_ - start)};
            if (cursor_ < end_) ++cursor_;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty()) return true;
        }
        return false;
    }

private:
    const char* cursor_;
    const char* end_;
};

/// Returns the next whitespace-separated field and advances `line` past it.
[[nodiscard]] inline std::string_view nextField(std::string_view& line) noexcept {
    std::size_t i = 0;
    while (i < line.size() && static_cast<unsigned char>(line[i]) <= ' ') ++i;
    const std::size_t start = i;
    while (i < line.size() && static_cast<unsigned char>(line[i]) > ' ') ++i;
    const std::string_view field = line.substr(start, i - start);
    line.remove_prefix(i);
    return field;
}

template <class T>
[[nodiscard]] T parseInteger(std::string_view text) {
    T value{};
    const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || pointer != text.data() + text.size())
        throw std::runtime_error{"ugyldig heltall: '" + std::string{text} + "'"};
    return value;
}

/// Parses a decimal degree such as "-16.9173984" into units of 1e-7 degrees.
/// Doing the fixed-point conversion by hand keeps it exact and avoids 16 million
/// floating-point parses while loading the map.
[[nodiscard]] inline std::int32_t parseFixed7(std::string_view text) {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }

    std::int64_t whole = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        whole = whole * 10 + (text[i] - '0');
        ++i;
    }

    std::int64_t fraction = 0;
    int digits = 0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && digits < 7) {
            fraction = fraction * 10 + (text[i] - '0');
            ++i;
            ++digits;
        }
    }
    while (digits < 7) {
        fraction *= 10;
        ++digits;
    }

    const std::int64_t value = whole * 10'000'000 + fraction;
    return static_cast<std::int32_t>(negative ? -value : value);
}

} // namespace routing
