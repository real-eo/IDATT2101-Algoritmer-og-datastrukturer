//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++26
//    Build: UCRT GCC (Windows) / GCC (Linux), GCC 15+
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <string_view>

// ? Design: everything below is evaluated at compile time.
// ? The graph files are embedded with #embed, parsed with constexpr from_chars,
// ? and solved with a constexpr Edmonds-Karp, so solve() is consteval: an
// ? incorrect file, an out-of-range index, or an exceeded path budget is a
// ? compile-time error, never a runtime failure. The cost is that all storage
// ? must be statically sized (std::array), which is why the path budget is
// ? derived from the data as edgeCount * nodeCount + 1 - the proven upper
// ? bound on augmenting paths in Edmonds-Karp.
namespace FlowNetworks {
    // * Embedded flow-network data
    namespace _data {
        inline constexpr char NETWORK_1[]{ 
            #embed "flytgraf1" suffix(,0) 
        };
        inline constexpr char NETWORK_2[]{ 
            #embed "flytgraf2" suffix(,0) 
        };
        inline constexpr char NETWORK_3[]{ 
            #embed "flytgraf3" suffix(,0) 
        };
        inline constexpr char NETWORK_4[]{ 
            #embed "flytgraf4" suffix(,0) 
        };
        inline constexpr char NETWORK_5[]{ 
            #embed "flytgraf5" suffix(,0) 
        };
    } // namespace _data

    inline constexpr std::string_view NETWORK_1{_data::NETWORK_1, sizeof(_data::NETWORK_1) - 1};
    inline constexpr std::string_view NETWORK_2{_data::NETWORK_2, sizeof(_data::NETWORK_2) - 1};
    inline constexpr std::string_view NETWORK_3{_data::NETWORK_3, sizeof(_data::NETWORK_3) - 1};
    inline constexpr std::string_view NETWORK_4{_data::NETWORK_4, sizeof(_data::NETWORK_4) - 1};
    inline constexpr std::string_view NETWORK_5{_data::NETWORK_5, sizeof(_data::NETWORK_5) - 1};

    namespace detail {
        [[nodiscard]] constexpr bool isSpace(const char C) noexcept {
            return C == ' ' || C == '\n' || C == '\r' || C == '\t' || C == '\f' || C == '\v';
        }

        [[nodiscard]] constexpr std::size_t skipSpace(std::string_view s, std::size_t pos) {
            while (pos < s.size() && isSpace(s[pos])) ++pos;
            return pos;
        }

        [[nodiscard]] constexpr int readInt(std::string_view s, std::size_t& pos) {
            pos = skipSpace(s, pos);

            const char* const FIRST = s.data() + pos;
            const char* const LAST  = s.data() + s.size();

            int value = 0;

            const auto [PTR, EC] = std::from_chars(FIRST, LAST, value);

            // NOTE: PTR == FIRST in readInt is redundant. 
            // ? `from_chars` guarantees `EC == std::errc::invalid_argument` when no characters
            // ? are consumed, so `EC != std::errc{}` already implies `PTR == FIRST`.
            if (EC != std::errc{}) [[unlikely]]
                throw "flownetworks: malformed graph file";                             // aborts constant evaluation

            pos = static_cast<std::size_t>(PTR - s.data());
            return value;
        }

        [[nodiscard]] constexpr std::size_t readNodeCount(std::string_view s) {
            std::size_t pos = 0;
            return static_cast<std::size_t>(readInt(s, pos));
        }

    } // namespace detail

    // * Flow network with residual capacities in an adjacency matrix
    template <std::size_t N>
    struct Network {
        std::size_t nodeCount{};
        std::size_t edgeCount{};
        std::array<std::array<int, N>, N> capacity{};
    };

    // Parses "N M" followed by M lines of "from to capacity" at compile time.
    template <std::size_t N>
    constexpr Network<N> parse(const std::string_view S) {
        std::size_t pos = 0;
        Network<N> net;

        net.nodeCount = static_cast<std::size_t>(detail::readInt(S, pos));
        
        if (net.nodeCount > N)  [[unlikely]] 
            throw "flownetworks: node count exceeds N";
        
        net.edgeCount = static_cast<std::size_t>(detail::readInt(S, pos));
        
        for (std::size_t e = 0; e < net.edgeCount; ++e) {
            const auto U = static_cast<std::size_t>(detail::readInt(S, pos));
            const auto V = static_cast<std::size_t>(detail::readInt(S, pos));
            const auto C = detail::readInt(S, pos);
            
            if (C < 0) [[unlikely]]
                throw "flownetworks: negative capacity";

            if (U >= net.nodeCount || V >= net.nodeCount) [[unlikely]]
                throw "flownetworks: edge endpoint out of range";
                        
            // ! Parallel edges overwrite, not sum: net.capacity.at(U).at(V) = C 
            // ? If a data file ever lists two edges between the same pair, the 
            // ? second replaces the first. Fine since the files don't do that.
            net.capacity.at(U).at(V) = C;
        }
        
        return net;
    }

    // * Edmonds-Karp result 
    // ? MaxPaths must be at least edgeCount * nodeCount + 1: each edge is 
    // ? critical at most O(V) times, so there can be O(V*E) augmenting paths.
    template <std::size_t N, std::size_t MaxPaths>
    struct MaxFlowResult {
        int maxFlow{};
        std::size_t pathCount{};
        std::array<int, MaxPaths> increase{};
        std::array<std::size_t, MaxPaths> pathLength{};
        std::array<std::array<int, N>, MaxPaths> path{};
    };

    template <std::size_t N, std::size_t MaxPaths>
    constexpr MaxFlowResult<N, MaxPaths> edmondsKarp(const Network<N>& net, const int source, const int sink) {
        if (source == sink) [[unlikely]]
            throw "flownetworks: source equals sink";

        auto residual = net.capacity;
        MaxFlowResult<N, MaxPaths> result{};

        std::array<int, N> parent{};
        std::array<int, N> queue{};

        while (true) {
            parent.fill(-1);
            parent.at(source) = source;
            queue.at(0) = source;
            std::size_t head = 0;
            std::size_t tail = 1;

            // BFS over edges with positive residual capacity
            while (head < tail && parent.at(sink) == -1) {
                const std::size_t U = static_cast<std::size_t>(queue.at(head));
                
                ++head;
                
                // ? Enqueue branch is taken at most once per vertex per BFS, so it is
                // ? predominantly false for non-degenerate graphs (data-dependent).
                for (std::size_t v = 0; v < net.nodeCount; ++v) {
                    if (parent.at(v) == -1 && residual.at(U).at(v) > 0) [[unlikely]] {
                        parent.at(v) = static_cast<int>(U);
                        queue.at(tail) = static_cast<int>(v);
                        ++tail;
                    }
                }
            }

            if (parent.at(sink) == -1) [[unlikely]] break;                              // no augmenting path left

            if (result.pathCount >= MaxPaths) [[unlikely]]
                throw "flownetworks: augmenting path budget exceeded";

            int bottleneck = std::numeric_limits<int>::max();
            std::size_t depth = 0;

            for (int v = sink; v != source; v = parent.at(v)) {
                bottleneck = std::min(bottleneck, residual.at(parent.at(v)).at(v));
                ++depth;
            }

            auto& increase    = result.increase.at(result.pathCount);
            auto& pathLength  = result.pathLength.at(result.pathCount);
            auto& path        = result.path.at(result.pathCount);

            increase   = bottleneck;
            pathLength = depth + 1;

            std::size_t i = depth;
            for (int v = sink; v != source; v = parent.at(v)) {
                path.at(i) = v;
                --i;
                residual.at(parent.at(v)).at(v) -= bottleneck;
                residual.at(v).at(parent.at(v)) += bottleneck;
            }
            path.at(i) = source;

            result.maxFlow += bottleneck;
            ++result.pathCount;
        }
        return result;
    }

    // * One-stop solver: node count and path budget are deduced from the data.
    template <std::string_view const& Data, int Source, int Sink>
    consteval auto solve() {
        constexpr std::size_t n   = detail::readNodeCount(Data);
        constexpr auto net        = parse<n>(Data);
        // Edmonds-Karp: each edge is critical at most n times -> O(n*E) augmentations.
        return edmondsKarp<n, net.edgeCount * n + 1>(net, Source, Sink);
    }

} // namespace FlowNetworks