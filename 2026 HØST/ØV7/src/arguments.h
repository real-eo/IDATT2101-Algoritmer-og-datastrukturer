#pragma once

#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace routing {

/// Windows gir `main` argumentene i ANSI-kodepagen, så alt utenfor ASCII
/// (æ, ø, å, ...) kommer inn feilkodet. Her leses kommandolinja på nytt som
/// UTF-16 og konverteres til UTF-8, slik at resten av programmet bare ser UTF-8.
[[nodiscard]] inline std::vector<std::string> commandLineArguments(const int argc, char** argv) {
#if defined(_WIN32)
    int wideCount = 0;
    LPWSTR* wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wideArguments != nullptr) {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(wideCount));
        for (int index = 0; index < wideCount; ++index) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wideArguments[index], -1, nullptr, 0, nullptr, nullptr);
            std::string text(static_cast<std::size_t>(size > 1 ? size - 1 : 0), '\0');
            if (size > 1)
                WideCharToMultiByte(CP_UTF8, 0, wideArguments[index], -1, text.data(), size, nullptr, nullptr);
            arguments.push_back(std::move(text));
        }
        LocalFree(wideArguments);
        return arguments;
    }
#endif
    return std::vector<std::string>{argv, argv + argc};
}

/// Skriver norske tegn riktig til konsollen på Windows.
inline void useUtf8Console() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
}

} // namespace routing
