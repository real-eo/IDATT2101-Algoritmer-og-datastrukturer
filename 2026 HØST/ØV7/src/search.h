#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "graph.h"
#include "indexed_heap.h"
#include "landmarks.h"

namespace routing {

enum class Algorithm {
    Dijkstra,
    Alt,
};

[[nodiscard]] std::string_view algorithmName(Algorithm algorithm) noexcept;

struct SearchResult {
    Algorithm algorithm = Algorithm::Dijkstra;
    NodeId source = NO_NODE;
    NodeId target = NO_NODE;
    /// Travel time in hundredths of a second, exactly as accumulated by the search.
    Weight travelTime = INFINITE_WEIGHT;
    /// Nodes taken out of the priority queue.
    std::size_t processedNodes = 0;
    /// Nodes that were queued at least once.
    std::size_t queuedNodes = 0;
    /// Wall-clock time for the search itself, excluding file reading.
    std::chrono::nanoseconds elapsed{};
    /// Route from source to target, inclusive.
    std::vector<NodeId> route;

    [[nodiscard]] bool found() const noexcept { return travelTime != INFINITE_WEIGHT; }
};

/// Reusable scratch space so repeated searches do not reallocate 8-million-entry
/// arrays. One instance per thread.
class SearchEngine {
public:
    explicit SearchEngine(const Graph& graph, const LandmarkTable* landmarks = nullptr);

    /// Runs Dijkstra (estimate = 0) or ALT on the same code path, so the two
    /// algorithms can only differ in how many nodes they visit, never in the
    /// travel time they report.
    [[nodiscard]] SearchResult search(NodeId source, NodeId target, Algorithm algorithm);

    /// Dijkstra on the reversed graph, used to fill one landmark column.
    /// Returns the travel time from every node to `source`.
    void distancesToNode(NodeId source, std::span<Weight> distances);

    /// Dijkstra on the forward graph, used to fill one landmark column.
    /// Returns the travel time from `source` to every node.
    void distancesFromNode(NodeId source, std::span<Weight> distances);

    /// Nearest points of interest of a given category, by travel time.
    struct PointOfInterest {
        NodeId node;
        Weight travelTime;
    };

    [[nodiscard]] std::vector<PointOfInterest> nearestPointsOfInterest(
        NodeId source, std::span<const NodeId> candidates, std::size_t wanted);

private:
    void resetScratch();
    [[nodiscard]] Weight estimate(NodeId node, NodeId target) const noexcept;

    const Graph& graph_;
    const LandmarkTable* landmarks_;

    std::vector<Weight> distance_;
    std::vector<Weight> estimate_;
    std::vector<NodeId> parent_;
    std::vector<std::uint8_t> state_;
    IndexedMinHeap queue_;
    std::vector<NodeId> touched_;
};

} // namespace routing
