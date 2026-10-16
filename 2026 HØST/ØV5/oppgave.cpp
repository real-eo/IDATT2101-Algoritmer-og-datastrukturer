//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++26
//    Build: UCRT GCC (Windows) / GCC (Linux), GCC 15+
#include <iostream>

#include "flownetworks.h"

namespace {
    constexpr auto result1 = FlowNetworks::solve<FlowNetworks::NETWORK_1, 0, 7>();
    constexpr auto result2 = FlowNetworks::solve<FlowNetworks::NETWORK_2, 0, 1>();
    constexpr auto result3 = FlowNetworks::solve<FlowNetworks::NETWORK_3, 0, 1>();
    constexpr auto result4 = FlowNetworks::solve<FlowNetworks::NETWORK_4, 0, 7>();
    constexpr auto result5 = FlowNetworks::solve<FlowNetworks::NETWORK_5, 0, 7>();

    // Known answers - the compiler verifies the implementation on every build.
    static_assert(result1.maxFlow == 10);
    static_assert(result2.maxFlow == 27);
    // Unknown answer for flytgraf3 
    static_assert(result4.maxFlow == 11);
    // Unknown answer for flytgraf5

    void print(const std::string_view NAME, const int SOURCE, const int SINK, const auto& result) {
        std::cout << "Maksimum flyt fra " << SOURCE << " til " << SINK
                  << " med Edmonds-Karp (" << NAME << ")\n";

        for (std::size_t p = 0; p < result.pathCount; ++p) {
            std::cout << result.increase.at(p) << " : ";
            
            for (std::size_t i = 0; i < result.pathLength.at(p); ++i) {
                std::cout << result.path.at(p).at(i);
                if (i + 1 < result.pathLength.at(p)) std::cout << ' ';
            }
            
            std::cout << '\n';
        }

        std::cout << "Maksimal flyt ble " << result.maxFlow << "\n\n";
    }

} // namespace

int main() {
    print("flytgraf1", 0, 7, result1);
    print("flytgraf2", 0, 1, result2);
    print("flytgraf3", 0, 1, result3);
    print("flytgraf4", 0, 7, result4);
    print("flytgraf5", 0, 7, result5);
}