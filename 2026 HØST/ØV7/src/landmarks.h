#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

#include "graph.h"

namespace routing {

/// Preprocessed ALT data: for every landmark, the exact shortest travel time from
/// the landmark to every node and from every node to the landmark.
///
/// Both directions are needed because the road network is full of one-way streets
/// and roundabouts; "distance to the landmark" cannot be read off the forward graph.
class LandmarkTable {
public:
    using ProgressCallback = std::function<void(std::size_t landmark, std::size_t total)>;

    /// Picks landmarks at the extremes of the map and runs one Dijkstra per
    /// landmark and direction. This is the expensive step; the result is cached.
    [[nodiscard]] static LandmarkTable build(const Graph& graph, std::size_t landmarkCount,
                                             const ProgressCallback& progress = {});

    [[nodiscard]] static LandmarkTable load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;

    [[nodiscard]] std::size_t count() const noexcept { return nodes_.size(); }
    [[nodiscard]] NodeId node(std::size_t landmark) const noexcept { return nodes_[landmark]; }

    [[nodiscard]] std::span<const Weight> fromLandmark(std::size_t landmark) const noexcept {
        return {fromLandmark_.data() + landmark * nodeCount_, nodeCount_};
    }

    [[nodiscard]] std::span<const Weight> toLandmark(std::size_t landmark) const noexcept {
        return {toLandmark_.data() + landmark * nodeCount_, nodeCount_};
    }

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodeCount_; }

    /// Number of nodes each landmark can actually reach, for documentation.
    [[nodiscard]] std::size_t reachableFrom(std::size_t landmark) const noexcept;
    [[nodiscard]] std::size_t reachableTo(std::size_t landmark) const noexcept;

private:
    std::size_t nodeCount_ = 0;
    std::vector<NodeId> nodes_;
    std::vector<Weight> fromLandmark_;
    std::vector<Weight> toLandmark_;
};

/// Chooses landmarks spread around the border of the map: the extremes of
/// latitude, longitude and both diagonals. Landmarks on the border give the
/// strongest bounds, because the triangle inequality is tightest when the
/// landmark lies roughly on the line through start and target.
///
/// Returns more candidates than requested, in order of preference, so the caller
/// can skip ones that turn out to sit in a disconnected corner of the network.
[[nodiscard]] std::vector<NodeId> selectLandmarkCandidates(const Graph& graph, std::size_t count);

} // namespace routing
