#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "graph.h"

namespace routing {

/// Binary min-heap with O(1) lookup of a node's position, so lowering a priority
/// is a sift-up rather than a linear scan. A linear scan is what makes naive
/// implementations take minutes on the Nordic map.
class IndexedMinHeap {
public:
    static constexpr std::uint32_t NOT_IN_HEAP = std::numeric_limits<std::uint32_t>::max();

    explicit IndexedMinHeap(std::size_t nodeCount)
        : position_(nodeCount, NOT_IN_HEAP), priority_(nodeCount, 0) {
        // Dijkstra on the longest trips queues almost every node, so reserving the
        // full node count avoids reallocating (and copying) mid-search. Untouched
        // pages are never faulted in, so the cost is only address space.
        heap_.reserve(nodeCount);
    }

    void clear() noexcept {
        for (const NodeId node : heap_) position_[node] = NOT_IN_HEAP;
        heap_.clear();
    }

    /// Inserts `node`, or lowers its priority if it is already queued.
    void insertOrDecrease(const NodeId node, const Weight priority) noexcept {
        const std::uint32_t existing = position_[node];
        if (existing != NOT_IN_HEAP) {
            if (priority >= priority_[node]) return;
            priority_[node] = priority;
            siftUp(existing);
            return;
        }
        priority_[node] = priority;
        position_[node] = static_cast<std::uint32_t>(heap_.size());
        heap_.push_back(node);
        siftUp(heap_.size() - 1);
    }

    [[nodiscard]] NodeId popMin() noexcept {
        const NodeId top = heap_.front();
        position_[top] = NOT_IN_HEAP;
        heap_.front() = heap_.back();
        heap_.pop_back();
        if (!heap_.empty()) {
            position_[heap_.front()] = 0;
            siftDown(0);
        }
        return top;
    }

    [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }
    [[nodiscard]] bool contains(const NodeId node) const noexcept { return position_[node] != NOT_IN_HEAP; }

private:
    void siftUp(std::size_t index) noexcept {
        const NodeId node = heap_[index];
        const Weight priority = priority_[node];
        while (index > 0) {
            const std::size_t parent = (index - 1) / 2;
            if (priority_[heap_[parent]] <= priority) break;
            heap_[index] = heap_[parent];
            position_[heap_[index]] = static_cast<std::uint32_t>(index);
            index = parent;
        }
        heap_[index] = node;
        position_[node] = static_cast<std::uint32_t>(index);
    }

    void siftDown(std::size_t index) noexcept {
        const NodeId node = heap_[index];
        const Weight priority = priority_[node];
        const std::size_t count = heap_.size();
        while (true) {
            const std::size_t left = 2 * index + 1;
            if (left >= count) break;
            const std::size_t right = left + 1;
            const std::size_t child = (right < count && priority_[heap_[right]] < priority_[heap_[left]]) ? right : left;
            if (priority_[heap_[child]] >= priority) break;
            heap_[index] = heap_[child];
            position_[heap_[index]] = static_cast<std::uint32_t>(index);
            index = child;
        }
        heap_[index] = node;
        position_[node] = static_cast<std::uint32_t>(index);
    }

    std::vector<NodeId> heap_;
    std::vector<std::uint32_t> position_;
    std::vector<Weight> priority_;
};

} // namespace routing
