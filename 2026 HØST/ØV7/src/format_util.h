#pragma once

#include <cstdio>
#include <format>
#include <string>
#include <utility>

namespace routing {

/// `std::print` does not link on every MinGW toolchain, so formatted output goes
/// through `std::format` and a single `fwrite` instead.
template <class... Args>
void write(std::format_string<Args...> format, Args&&... args) {
    const std::string text = std::format(format, std::forward<Args>(args)...);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

template <class... Args>
void writeError(std::format_string<Args...> format, Args&&... args) {
    const std::string text = std::format(format, std::forward<Args>(args)...);
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
}

} // namespace routing
