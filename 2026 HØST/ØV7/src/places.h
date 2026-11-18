#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "graph.h"

namespace routing {

/// Bit flags from interessepkt.txt.
enum class PlaceCategory : std::uint8_t {
    PlaceName = 1,
    FuelStation = 2,
    ChargingStation = 4,
    Eatery = 8,
    DrinkingPlace = 16,
    Accommodation = 32,
};

[[nodiscard]] constexpr std::uint8_t mask(const PlaceCategory category) noexcept {
    return static_cast<std::uint8_t>(category);
}

struct Place {
    NodeId node;
    std::uint8_t categories;
    std::string name;
};

class PlaceIndex {
public:
    static PlaceIndex load(const std::filesystem::path& path);

    [[nodiscard]] std::span<const Place> places() const noexcept { return places_; }

    /// Nodes whose category flags contain every bit in `wanted`.
    [[nodiscard]] std::vector<NodeId> nodesWith(std::uint8_t wanted) const;

    /// First place whose name contains `needle`, case-insensitive.
    [[nodiscard]] const Place* findByName(std::string_view needle) const;

private:
    std::vector<Place> places_;
};

} // namespace routing
