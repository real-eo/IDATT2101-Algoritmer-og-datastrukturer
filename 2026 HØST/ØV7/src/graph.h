#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace routing {

using NodeId = std::uint32_t;
using Weight = std::uint32_t;

/// Sentinel for "no route found". Real travel times are far below this.
inline constexpr Weight INFINITE_WEIGHT = std::numeric_limits<Weight>::max();
inline constexpr NodeId NO_NODE = std::numeric_limits<NodeId>::max();

/// The map files store travel time in hundredths of a second.
inline constexpr Weight HUNDREDTHS_PER_SECOND = 100;

struct Coordinate {
    double latitude;
    double longitude;
};

/// Road network in compressed sparse row form, with both the forward and the
/// reversed edge set. The reversed graph is needed because the network is full of
/// one-way streets, so "distance to a landmark" cannot be read off the forward graph.
class Graph {
public:
    static Graph load(const std::filesystem::path& directory);

    [[nodiscard]] std::size_t nodeCount() const noexcept { return latitude_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return forwardTargets_.size(); }

    [[nodiscard]] std::span<const NodeId> successors(NodeId node) const noexcept {
        return {forwardTargets_.data() + forwardOffsets_[node],
                static_cast<std::size_t>(forwardOffsets_[node + 1] - forwardOffsets_[node])};
    }

    [[nodiscard]] std::span<const Weight> successorWeights(NodeId node) const noexcept {
        return {forwardWeights_.data() + forwardOffsets_[node],
                static_cast<std::size_t>(forwardOffsets_[node + 1] - forwardOffsets_[node])};
    }

    [[nodiscard]] std::span<const NodeId> predecessors(NodeId node) const noexcept {
        return {reverseTargets_.data() + reverseOffsets_[node],
                static_cast<std::size_t>(reverseOffsets_[node + 1] - reverseOffsets_[node])};
    }

    [[nodiscard]] std::span<const Weight> predecessorWeights(NodeId node) const noexcept {
        return {reverseWeights_.data() + reverseOffsets_[node],
                static_cast<std::size_t>(reverseOffsets_[node + 1] - reverseOffsets_[node])};
    }

    [[nodiscard]] Coordinate coordinate(NodeId node) const noexcept {
        return {static_cast<double>(latitude_[node]) / 10'000'000.0,
                static_cast<double>(longitude_[node]) / 10'000'000.0};
    }

    /// Coordinates in units of 1e-7 degrees, as stored.
    [[nodiscard]] std::int32_t latitude(NodeId node) const noexcept { return latitude_[node]; }
    [[nodiscard]] std::int32_t longitude(NodeId node) const noexcept { return longitude_[node]; }

private:
    void loadNodes(std::string_view text);
    void loadEdges(std::string_view text);

    std::vector<std::uint32_t> forwardOffsets_;
    std::vector<NodeId> forwardTargets_;
    std::vector<Weight> forwardWeights_;
    std::vector<std::uint32_t> reverseOffsets_;
    std::vector<NodeId> reverseTargets_;
    std::vector<Weight> reverseWeights_;
    std::vector<std::int32_t> latitude_;
    std::vector<std::int32_t> longitude_;
};

} // namespace routing
