#include "landmarks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include "search.h"

namespace routing {
namespace {

    constexpr std::array<char, 8> CACHE_MAGIC{'A', 'L', 'T', 'N', 'O', 'R', 'D', '1'};
    constexpr std::uint32_t CACHE_VERSION = 1;

    template <class T>
    void writeRaw(std::ofstream& stream, const T& value) {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <class T>
    void readRaw(std::ifstream& stream, T& value) {
        stream.read(reinterpret_cast<char*>(&value), sizeof(T));
        if (!stream) throw std::runtime_error{"uventet slutt på landemerkefila"};
    }

    template <class T>
    void writeVector(std::ofstream& stream, const std::vector<T>& values) {
        const std::uint64_t size = values.size();
        writeRaw(stream, size);
        stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(size * sizeof(T)));
    }

    template <class T>
    void readVector(std::ifstream& stream, std::vector<T>& values) {
        std::uint64_t size = 0;
        readRaw(stream, size);
        values.resize(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(size * sizeof(T)));
        if (!stream) throw std::runtime_error{"uventet slutt på landemerkefila"};
    }

} // namespace

std::vector<NodeId> selectLandmarkCandidates(const Graph& graph, const std::size_t count) {
    const std::size_t nodes = graph.nodeCount();
    if (nodes == 0 || count == 0) return {};

    // Eight compass directions across the map. Landmarks on the border give the
    // tightest bounds: the triangle inequality is sharpest when the landmark lies
    // roughly on the line through start and target, and a border landmark is on
    // that line for every trip that crosses the map.
    struct Direction {
        const char* name;
        std::int64_t (*project)(std::int32_t latitude, std::int32_t longitude);
    };

    constexpr std::array<Direction, 8> DIRECTIONS{{
        {"nord", [](std::int32_t lat, std::int32_t) { return static_cast<std::int64_t>(lat); }},
        {"sør", [](std::int32_t lat, std::int32_t) { return -static_cast<std::int64_t>(lat); }},
        {"øst", [](std::int32_t, std::int32_t lon) { return static_cast<std::int64_t>(lon); }},
        {"vest", [](std::int32_t, std::int32_t lon) { return -static_cast<std::int64_t>(lon); }},
        {"nordøst", [](std::int32_t lat, std::int32_t lon) { return static_cast<std::int64_t>(lat) + lon; }},
        {"nordvest", [](std::int32_t lat, std::int32_t lon) { return static_cast<std::int64_t>(lat) - lon; }},
        {"sørøst", [](std::int32_t lat, std::int32_t lon) { return -static_cast<std::int64_t>(lat) + lon; }},
        {"sørvest", [](std::int32_t lat, std::int32_t lon) { return -static_cast<std::int64_t>(lat) - lon; }},
    }};

    std::vector<NodeId> chosen;
    chosen.reserve(count);

    const auto addUnique = [&chosen](const NodeId node) {
        if (std::ranges::find(chosen, node) == chosen.end()) chosen.push_back(node);
    };

    // A landmark that cannot reach (or be reached from) the rest of the network
    // gives useless bounds, so only nodes with both in- and out-edges qualify.
    const auto isConnected = [&graph](const NodeId node) {
        return !graph.successors(node).empty() && !graph.predecessors(node).empty();
    };

    for (const Direction& direction : DIRECTIONS) {
        if (chosen.size() >= count) break;
        NodeId best = NO_NODE;
        std::int64_t bestScore = std::numeric_limits<std::int64_t>::min();
        for (NodeId node = 0; node < nodes; ++node) {
            if (!isConnected(node)) continue;
            const std::int64_t score = direction.project(graph.latitude(node), graph.longitude(node));
            if (score > bestScore) {
                bestScore = score;
                best = node;
            }
        }
        if (best != NO_NODE) addUnique(best);
    }

    // More landmarks than compass directions: farthest-point sampling on the
    // coordinates, which keeps them spread out instead of clustering.
    while (chosen.size() < count) {
        NodeId best = NO_NODE;
        std::int64_t bestDistance = -1;
        for (NodeId node = 0; node < nodes; ++node) {
            if (!isConnected(node)) continue;
            std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
            for (const NodeId landmark : chosen) {
                const std::int64_t dLat = static_cast<std::int64_t>(graph.latitude(node)) - graph.latitude(landmark);
                const std::int64_t dLon = static_cast<std::int64_t>(graph.longitude(node)) - graph.longitude(landmark);
                nearest = std::min(nearest, dLat * dLat + dLon * dLon);
            }
            if (nearest > bestDistance) {
                bestDistance = nearest;
                best = node;
            }
        }
        if (best == NO_NODE) break;
        addUnique(best);
    }

    return chosen;
}

LandmarkTable LandmarkTable::build(const Graph& graph, const std::size_t landmarkCount,
                                   const ProgressCallback& progress) {
    LandmarkTable table;
    table.nodeCount_ = graph.nodeCount();

    // Ask for extra candidates: a border node can sit on a tiny island or in a
    // cul-de-sac, and such a landmark gives useless bounds. Candidates are tried
    // in order of preference until enough of them reach most of the network.
    const std::vector<NodeId> candidates = selectLandmarkCandidates(graph, std::max<std::size_t>(landmarkCount * 3, 24));
    if (candidates.empty()) throw std::runtime_error{"fant ingen landemerker"};

    const std::size_t minimumReach = graph.nodeCount() / 2;
    SearchEngine engine{graph};
    std::vector<Weight> forward(graph.nodeCount(), INFINITE_WEIGHT);
    std::vector<Weight> backward(graph.nodeCount(), INFINITE_WEIGHT);

    for (const NodeId candidate : candidates) {
        if (table.nodes_.size() >= landmarkCount) break;

        engine.distancesFromNode(candidate, forward);
        engine.distancesToNode(candidate, backward);

        const auto reachable = [](const std::span<const Weight> distances) {
            return static_cast<std::size_t>(
                std::ranges::count_if(distances, [](const Weight weight) { return weight != INFINITE_WEIGHT; }));
        };
        if (reachable(forward) < minimumReach || reachable(backward) < minimumReach) continue;

        table.nodes_.push_back(candidate);
        table.fromLandmark_.insert(table.fromLandmark_.end(), forward.begin(), forward.end());
        table.toLandmark_.insert(table.toLandmark_.end(), backward.begin(), backward.end());
        if (progress) progress(table.nodes_.size(), landmarkCount);
    }

    if (table.nodes_.empty()) throw std::runtime_error{"fant ingen landemerker som dekker veinettet"};
    return table;
}

void LandmarkTable::save(const std::filesystem::path& path) const {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) throw std::runtime_error{"kan ikke skrive " + path.string()};

    stream.write(CACHE_MAGIC.data(), static_cast<std::streamsize>(CACHE_MAGIC.size()));
    writeRaw(stream, CACHE_VERSION);
    writeRaw(stream, static_cast<std::uint64_t>(nodeCount_));
    writeVector(stream, nodes_);
    writeVector(stream, fromLandmark_);
    writeVector(stream, toLandmark_);

    if (!stream) throw std::runtime_error{"feil under skriving av " + path.string()};
}

LandmarkTable LandmarkTable::load(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) throw std::runtime_error{"kan ikke åpne " + path.string()};

    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!stream || magic != CACHE_MAGIC) throw std::runtime_error{"ugyldig landemerkefil: " + path.string()};

    std::uint32_t version = 0;
    readRaw(stream, version);
    if (version != CACHE_VERSION) throw std::runtime_error{"utdatert landemerkefil: " + path.string()};

    LandmarkTable table;
    std::uint64_t nodeCount = 0;
    readRaw(stream, nodeCount);
    table.nodeCount_ = static_cast<std::size_t>(nodeCount);
    readVector(stream, table.nodes_);
    readVector(stream, table.fromLandmark_);
    readVector(stream, table.toLandmark_);

    const std::size_t expected = table.nodes_.size() * table.nodeCount_;
    if (table.fromLandmark_.size() != expected || table.toLandmark_.size() != expected)
        throw std::runtime_error{"landemerkefila har feil størrelse: " + path.string()};

    return table;
}

std::size_t LandmarkTable::reachableFrom(const std::size_t landmark) const noexcept {
    const std::span<const Weight> distances = fromLandmark(landmark);
    return static_cast<std::size_t>(std::ranges::count_if(distances, [](const Weight w) { return w != INFINITE_WEIGHT; }));
}

std::size_t LandmarkTable::reachableTo(const std::size_t landmark) const noexcept {
    const std::span<const Weight> distances = toLandmark(landmark);
    return static_cast<std::size_t>(std::ranges::count_if(distances, [](const Weight w) { return w != INFINITE_WEIGHT; }));
}

} // namespace routing
