#include "graph.h"

#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "text_scan.h"

namespace routing {
namespace {

    struct RawEdge {
        NodeId from;
        NodeId to;
        Weight weight;
    };

} // namespace

Graph Graph::load(const std::filesystem::path& directory) {
    Graph graph;
    {
        const std::vector<char> buffer = readWholeFile(directory / "noder.txt");
        graph.loadNodes(std::string_view{buffer.data(), buffer.size()});
    }
    {
        const std::vector<char> buffer = readWholeFile(directory / "kanter.txt");
        graph.loadEdges(std::string_view{buffer.data(), buffer.size()});
    }
    return graph;
}

void Graph::loadNodes(std::string_view text) {
    LineScanner scanner{text};
    std::string_view line;
    if (!scanner.next(line)) throw std::runtime_error{"noder.txt er tom"};

    const std::size_t declared = parseInteger<std::size_t>(nextField(line));
    latitude_.assign(declared, 0);
    longitude_.assign(declared, 0);

    std::size_t seen = 0;
    while (scanner.next(line)) {
        std::string_view rest = line;
        const NodeId id = parseInteger<NodeId>(nextField(rest));
        const std::int32_t latitude = parseFixed7(nextField(rest));
        const std::int32_t longitude = parseFixed7(nextField(rest));
        if (id >= declared) throw std::runtime_error{"nodenummer utenfor området i noder.txt"};
        latitude_[id] = latitude;
        longitude_[id] = longitude;
        ++seen;
    }

    if (seen != declared)
        throw std::runtime_error{"noder.txt oppgir " + std::to_string(declared) + " noder, men inneholder " +
                                 std::to_string(seen)};
}

void Graph::loadEdges(std::string_view text) {
    LineScanner header{text};
    std::string_view line;
    if (!header.next(line)) throw std::runtime_error{"kanter.txt er tom"};

    const std::size_t declared = parseInteger<std::size_t>(nextField(line));
    const std::size_t nodes = nodeCount();

    // One text pass: parse every edge into a flat array, then lay out the CSR
    // arrays from it. Re-parsing the 450 MB text file a second time is slower
    // than keeping the 12-byte triples around for a moment.
    std::vector<RawEdge> edges;
    edges.reserve(declared);
    {
        LineScanner scanner{text};
        if (!scanner.next(line)) throw std::runtime_error{"kanter.txt er tom"};
        while (scanner.next(line)) {
            std::string_view rest = line;
            const NodeId from = parseInteger<NodeId>(nextField(rest));
            const NodeId to = parseInteger<NodeId>(nextField(rest));
            const Weight weight = parseInteger<Weight>(nextField(rest));
            if (from >= nodes || to >= nodes) throw std::runtime_error{"kantenode utenfor området i kanter.txt"};
            edges.push_back(RawEdge{from, to, weight});
        }
    }

    if (edges.size() != declared)
        throw std::runtime_error{"kanter.txt oppgir " + std::to_string(declared) + " kanter, men inneholder " +
                                 std::to_string(edges.size())};

    forwardOffsets_.assign(nodes + 1, 0);
    reverseOffsets_.assign(nodes + 1, 0);
    for (const RawEdge& edge : edges) {
        ++forwardOffsets_[edge.from + 1];
        ++reverseOffsets_[edge.to + 1];
    }
    std::partial_sum(forwardOffsets_.begin(), forwardOffsets_.end(), forwardOffsets_.begin());
    std::partial_sum(reverseOffsets_.begin(), reverseOffsets_.end(), reverseOffsets_.begin());

    forwardTargets_.resize(edges.size());
    forwardWeights_.resize(edges.size());
    reverseTargets_.resize(edges.size());
    reverseWeights_.resize(edges.size());

    std::vector<std::uint32_t> forwardCursor{forwardOffsets_.begin(), forwardOffsets_.end() - 1};
    std::vector<std::uint32_t> reverseCursor{reverseOffsets_.begin(), reverseOffsets_.end() - 1};

    for (const RawEdge& edge : edges) {
        forwardTargets_[forwardCursor[edge.from]] = edge.to;
        forwardWeights_[forwardCursor[edge.from]] = edge.weight;
        ++forwardCursor[edge.from];

        reverseTargets_[reverseCursor[edge.to]] = edge.from;
        reverseWeights_[reverseCursor[edge.to]] = edge.weight;
        ++reverseCursor[edge.to];
    }
}

} // namespace routing
