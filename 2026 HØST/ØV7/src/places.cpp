#include "places.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "text_scan.h"

namespace routing {
namespace {

    /// The names in interessepkt.txt are UTF-8 and quoted. Only the quotes are
    /// stripped; the bytes are kept as they are so the names print correctly.
    [[nodiscard]] std::string unquote(std::string_view field) {
        if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
            field.remove_prefix(1);
            field.remove_suffix(1);
        }
        return std::string{field};
    }

    [[nodiscard]] char toLowerAscii(const char c) noexcept {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

} // namespace

PlaceIndex PlaceIndex::load(const std::filesystem::path& path) {
    const std::vector<char> buffer = readWholeFile(path);
    const std::string_view text{buffer.data(), buffer.size()};

    LineScanner scanner{text};
    std::string_view line;
    if (!scanner.next(line)) throw std::runtime_error{"interessepkt.txt er tom"};

    PlaceIndex index;
    index.places_.reserve(parseInteger<std::size_t>(nextField(line)));

    while (scanner.next(line)) {
        std::string_view rest = line;
        const NodeId node = parseInteger<NodeId>(nextField(rest));
        const std::uint8_t categories = parseInteger<std::uint8_t>(nextField(rest));
        // The name is the rest of the line and may contain spaces, so it is not
        // read as a single whitespace-delimited field.
        while (!rest.empty() && static_cast<unsigned char>(rest.front()) <= ' ') rest.remove_prefix(1);
        index.places_.push_back(Place{node, categories, unquote(rest)});
    }

    return index;
}

std::vector<NodeId> PlaceIndex::nodesWith(const std::uint8_t wanted) const {
    std::vector<NodeId> nodes;
    for (const Place& place : places_) {
        if ((place.categories & wanted) == wanted) nodes.push_back(place.node);
    }
    return nodes;
}

const Place* PlaceIndex::findByName(const std::string_view needle) const {
    if (needle.empty()) return nullptr;

    std::string lowered;
    lowered.reserve(needle.size());
    for (const char c : needle) lowered.push_back(toLowerAscii(c));

    for (const Place& place : places_) {
        std::string candidate;
        candidate.reserve(place.name.size());
        for (const char c : place.name) candidate.push_back(toLowerAscii(c));
        if (candidate.find(lowered) != std::string::npos) return &place;
    }
    return nullptr;
}

} // namespace routing
