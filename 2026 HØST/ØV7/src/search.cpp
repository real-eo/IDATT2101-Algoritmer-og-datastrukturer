#include "search.h"

#include <algorithm>
#include <limits>
#include <ranges>

namespace routing {
namespace {

    constexpr std::uint8_t STATE_UNSEEN = 0;
    constexpr std::uint8_t STATE_QUEUED = 1;
    constexpr std::uint8_t STATE_SETTLED = 2;

    /// Saturating addition: an unreachable node stays unreachable instead of wrapping.
    [[nodiscard]] constexpr Weight addWeight(const Weight left, const Weight right) noexcept {
        if (left == INFINITE_WEIGHT || right == INFINITE_WEIGHT) return INFINITE_WEIGHT;
        const std::uint64_t sum = static_cast<std::uint64_t>(left) + right;
        return sum >= INFINITE_WEIGHT ? INFINITE_WEIGHT : static_cast<Weight>(sum);
    }

} // namespace

std::string_view algorithmName(const Algorithm algorithm) noexcept {
    switch (algorithm) {
        case Algorithm::Dijkstra: return "Dijkstra";
        case Algorithm::Alt: return "ALT";
    }
    return "ukjent";
}

SearchEngine::SearchEngine(const Graph& graph, const LandmarkTable* landmarks)
    : graph_{graph},
      landmarks_{landmarks},
      distance_(graph.nodeCount(), INFINITE_WEIGHT),
      estimate_(graph.nodeCount(), 0),
      parent_(graph.nodeCount(), NO_NODE),
      state_(graph.nodeCount(), STATE_UNSEEN),
      queue_{graph.nodeCount()} {}

void SearchEngine::resetScratch() {
    for (const NodeId node : touched_) {
        distance_[node] = INFINITE_WEIGHT;
        estimate_[node] = 0;
        parent_[node] = NO_NODE;
        state_[node] = STATE_UNSEEN;
    }
    touched_.clear();
    queue_.clear();
}

Weight SearchEngine::estimate(const NodeId node, const NodeId target) const noexcept {
    if (landmarks_ == nullptr) return 0;

    Weight best = 0;
    for (std::size_t landmark = 0; landmark < landmarks_->count(); ++landmark) {
        const std::span<const Weight> fromLandmark = landmarks_->fromLandmark(landmark);
        const std::span<const Weight> toLandmark = landmarks_->toLandmark(landmark);

        // Triangle inequality through the landmark, in both directions. Each term
        // is a lower bound on the remaining travel time; the largest one wins.
        const Weight landmarkToTarget = fromLandmark[target];
        const Weight landmarkToNode = fromLandmark[node];
        if (landmarkToTarget != INFINITE_WEIGHT && landmarkToNode != INFINITE_WEIGHT && landmarkToTarget > landmarkToNode)
            best = std::max(best, static_cast<Weight>(landmarkToTarget - landmarkToNode));

        const Weight nodeToLandmark = toLandmark[node];
        const Weight targetToLandmark = toLandmark[target];
        if (nodeToLandmark != INFINITE_WEIGHT && targetToLandmark != INFINITE_WEIGHT && nodeToLandmark > targetToLandmark)
            best = std::max(best, static_cast<Weight>(nodeToLandmark - targetToLandmark));
    }
    return best;
}

SearchResult SearchEngine::search(const NodeId source, const NodeId target, const Algorithm algorithm) {
    SearchResult result;
    result.algorithm = algorithm;
    result.source = source;
    result.target = target;

    if (source >= graph_.nodeCount() || target >= graph_.nodeCount()) return result;

    resetScratch();

    const bool useEstimates = algorithm == Algorithm::Alt && landmarks_ != nullptr && landmarks_->count() > 0;

    const auto start = std::chrono::steady_clock::now();

    distance_[source] = 0;
    estimate_[source] = useEstimates ? estimate(source, target) : 0;
    state_[source] = STATE_QUEUED;
    touched_.push_back(source);
    queue_.insertOrDecrease(source, estimate_[source]);

    while (!queue_.empty()) {
        const NodeId current = queue_.popMin();
        state_[current] = STATE_SETTLED;
        ++result.processedNodes;

        // Stop when the target leaves the queue: only then is its distance final.
        if (current == target) break;

        const std::span<const NodeId> successors = graph_.successors(current);
        const std::span<const Weight> weights = graph_.successorWeights(current);
        const Weight currentDistance = distance_[current];

        for (std::size_t i = 0; i < successors.size(); ++i) {
            const NodeId next = successors[i];
            if (state_[next] == STATE_SETTLED) continue;

            const Weight candidate = addWeight(currentDistance, weights[i]);
            if (candidate >= distance_[next]) continue;

            distance_[next] = candidate;
            parent_[next] = current;
            if (state_[next] == STATE_UNSEEN) {
                state_[next] = STATE_QUEUED;
                touched_.push_back(next);
                ++result.queuedNodes;
                // The estimate depends only on the node and the target, so it is
                // computed once, when the node first enters the queue.
                estimate_[next] = useEstimates ? estimate(next, target) : 0;
            }
            queue_.insertOrDecrease(next, addWeight(candidate, estimate_[next]));
        }
    }

    const auto finish = std::chrono::steady_clock::now();
    result.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start);

    if (state_[target] == STATE_SETTLED) {
        result.travelTime = distance_[target];
        for (NodeId node = target; node != NO_NODE; node = parent_[node]) {
            result.route.push_back(node);
            if (node == source) break;
        }
        std::ranges::reverse(result.route);
    }

    return result;
}

void SearchEngine::distancesFromNode(const NodeId source, std::span<Weight> distances) {
    resetScratch();
    std::ranges::fill(distances, INFINITE_WEIGHT);

    distance_[source] = 0;
    state_[source] = STATE_QUEUED;
    touched_.push_back(source);
    queue_.insertOrDecrease(source, 0);

    while (!queue_.empty()) {
        const NodeId current = queue_.popMin();
        state_[current] = STATE_SETTLED;
        distances[current] = distance_[current];

        const std::span<const NodeId> successors = graph_.successors(current);
        const std::span<const Weight> weights = graph_.successorWeights(current);
        for (std::size_t i = 0; i < successors.size(); ++i) {
            const NodeId next = successors[i];
            if (state_[next] == STATE_SETTLED) continue;
            const Weight candidate = addWeight(distance_[current], weights[i]);
            if (candidate >= distance_[next]) continue;
            distance_[next] = candidate;
            if (state_[next] == STATE_UNSEEN) {
                state_[next] = STATE_QUEUED;
                touched_.push_back(next);
            }
            queue_.insertOrDecrease(next, candidate);
        }
    }
}

void SearchEngine::distancesToNode(const NodeId source, std::span<Weight> distances) {
    resetScratch();
    std::ranges::fill(distances, INFINITE_WEIGHT);

    distance_[source] = 0;
    state_[source] = STATE_QUEUED;
    touched_.push_back(source);
    queue_.insertOrDecrease(source, 0);

    while (!queue_.empty()) {
        const NodeId current = queue_.popMin();
        state_[current] = STATE_SETTLED;
        distances[current] = distance_[current];

        const std::span<const NodeId> predecessors = graph_.predecessors(current);
        const std::span<const Weight> weights = graph_.predecessorWeights(current);
        for (std::size_t i = 0; i < predecessors.size(); ++i) {
            const NodeId next = predecessors[i];
            if (state_[next] == STATE_SETTLED) continue;
            const Weight candidate = addWeight(distance_[current], weights[i]);
            if (candidate >= distance_[next]) continue;
            distance_[next] = candidate;
            if (state_[next] == STATE_UNSEEN) {
                state_[next] = STATE_QUEUED;
                touched_.push_back(next);
            }
            queue_.insertOrDecrease(next, candidate);
        }
    }
}

std::vector<SearchEngine::PointOfInterest> SearchEngine::nearestPointsOfInterest(
    const NodeId source, std::span<const NodeId> candidates, const std::size_t wanted) {
    std::vector<PointOfInterest> found;
    if (candidates.empty() || wanted == 0) return found;

    resetScratch();

    std::vector<std::uint8_t> isCandidate(graph_.nodeCount(), 0);
    for (const NodeId candidate : candidates) isCandidate[candidate] = 1;

    distance_[source] = 0;
    state_[source] = STATE_QUEUED;
    touched_.push_back(source);
    queue_.insertOrDecrease(source, 0);

    while (!queue_.empty() && found.size() < wanted) {
        const NodeId current = queue_.popMin();
        state_[current] = STATE_SETTLED;

        if (isCandidate[current]) found.push_back(PointOfInterest{current, distance_[current]});

        const std::span<const NodeId> successors = graph_.successors(current);
        const std::span<const Weight> weights = graph_.successorWeights(current);
        for (std::size_t i = 0; i < successors.size(); ++i) {
            const NodeId next = successors[i];
            if (state_[next] == STATE_SETTLED) continue;
            const Weight candidate = addWeight(distance_[current], weights[i]);
            if (candidate >= distance_[next]) continue;
            distance_[next] = candidate;
            if (state_[next] == STATE_UNSEEN) {
                state_[next] = STATE_QUEUED;
                touched_.push_back(next);
            }
            queue_.insertOrDecrease(next, candidate);
        }
    }

    return found;
}

} // namespace routing
