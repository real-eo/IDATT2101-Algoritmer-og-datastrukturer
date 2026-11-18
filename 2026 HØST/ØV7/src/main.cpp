//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++26
//    Build: UCRT GCC (Windows) / GCC (Linux), GCC 15+
//
// Øving 7: korteste vei på kartdata med Dijkstra og ALT.
//
// Programmet leser noder.txt, kanter.txt og interessepkt.txt, preprosesserer
// landemerker (eller leser dem fra cache), og kjører deretter Dijkstra og ALT
// på samme graf. Begge søkene bruker nøyaktig samme kodevei og skiller seg bare
// i estimatet: ALT bruker landemerkene, Dijkstra bruker 0. Derfor kan de ikke
// ende opp med ulik kjøretid.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arguments.h"
#include "format_util.h"
#include "graph.h"
#include "landmarks.h"
#include "places.h"
#include "search.h"
#include "text_scan.h"

namespace {

using namespace routing;

constexpr std::size_t DEFAULT_LANDMARKS = 8;

struct Options {
    std::filesystem::path dataDirectory = "data/norden";
    std::filesystem::path landmarkCache;
    std::size_t landmarkCount = DEFAULT_LANDMARKS;
    bool rebuildLandmarks = false;
    bool quiet = false;
    std::optional<NodeId> source;
    std::optional<NodeId> target;
    std::optional<std::string> sourceName;
    std::optional<std::string> targetName;
    std::optional<NodeId> nearbySource;
    std::optional<std::string> nearbySourceName;
    std::uint8_t nearbyCategories = mask(PlaceCategory::ChargingStation);
    std::size_t nearbyCount = 4;
    std::optional<std::filesystem::path> routeFile;
    std::optional<std::filesystem::path> nearbyRouteFile;
};

[[nodiscard]] std::string formatDuration(const Weight hundredths) {
    const auto total = std::chrono::seconds{hundredths / HUNDREDTHS_PER_SECOND};
    return std::format("{:%H:%M:%S}", total);
}

[[nodiscard]] std::string formatMilliseconds(const std::chrono::nanoseconds elapsed) {
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    return std::format("{}.{:03} ms", microseconds / 1000, microseconds % 1000);
}

[[nodiscard]] std::string categoryLabel(const std::uint8_t categories) {
    std::vector<std::string_view> labels;
    if ((categories & mask(PlaceCategory::PlaceName)) != 0) labels.emplace_back("stedsnavn");
    if ((categories & mask(PlaceCategory::FuelStation)) != 0) labels.emplace_back("bensinstasjon");
    if ((categories & mask(PlaceCategory::ChargingStation)) != 0) labels.emplace_back("ladestasjon");
    if ((categories & mask(PlaceCategory::Eatery)) != 0) labels.emplace_back("spisested");
    if ((categories & mask(PlaceCategory::DrinkingPlace)) != 0) labels.emplace_back("drikkested");
    if ((categories & mask(PlaceCategory::Accommodation)) != 0) labels.emplace_back("overnatting");

    std::string joined;
    for (const std::string_view label : labels) {
        if (!joined.empty()) joined += ", ";
        joined += label;
    }
    return joined.empty() ? "ukjent" : joined;
}

void printUsage() {
    constexpr std::string_view USAGE = R"(Bruk: ruting [valg]

  --data <mappe>          Mappe med noder.txt, kanter.txt, interessepkt.txt
                          (standard: data/norden)
  --landmarks <antall>    Antall landemerker for ALT (standard: 8)
  --rebuild-landmarks     Regn ut landemerkene på nytt selv om cachen finnes
  --cache <fil>           Fil for landemerkecachen
                          (standard: <mappe>/landemerker.bin)
  --from <node>           Startnode
  --to <node>             Målnode
  --from-name <tekst>     Slå opp startnode på stedsnavn i interessepkt.txt
  --to-name <tekst>       Slå opp målnode på stedsnavn i interessepkt.txt
  --near <node>           Finn nærmeste interessepunkter fra denne noden
  --near-name <tekst>     Slå opp noden for --near på stedsnavn
  --category <navn>       Kategori for --near: ladestasjon, bensinstasjon,
                          spisested, drikkested, overnatting, stedsnavn
                          (standard: ladestasjon)
  --count <antall>        Antall interessepunkter (standard: 4)
  --route <fil>           Skriv reiseruta som "breddegrad,lengdegrad" per linje
  --near-route <fil>      Skriv interessepunktene som "breddegrad,lengdegrad,navn"
  --quiet                 Bare resultattabellen
  --help                  Vis denne hjelpen

Eksempler:
  ruting --from 2001238 --to 1987066
  ruting --from 2001238 --to 2763740 --route gloshaugen-odense.csv
  ruting --near 2763740 --category ladestasjon --count 4
  ruting --near 2001238 --category spisested --count 4
)";
    std::fwrite(USAGE.data(), 1, USAGE.size(), stdout);
}

[[nodiscard]] std::uint8_t parseCategory(const std::string_view name) {
    if (name == "ladestasjon") return mask(PlaceCategory::ChargingStation);
    if (name == "bensinstasjon") return mask(PlaceCategory::FuelStation);
    if (name == "spisested") return mask(PlaceCategory::Eatery);
    if (name == "drikkested") return mask(PlaceCategory::DrinkingPlace);
    if (name == "overnatting") return mask(PlaceCategory::Accommodation);
    if (name == "stedsnavn") return mask(PlaceCategory::PlaceName);
    throw std::runtime_error{"ukjent kategori: " + std::string{name}};
}

[[nodiscard]] Options parseOptions(const std::span<const std::string> arguments) {
    Options options;
    const auto requireValue = [&](std::size_t& index, const std::string_view flag) -> std::string_view {
        if (index + 1 >= arguments.size()) throw std::runtime_error{"mangler verdi for " + std::string{flag}};
        return arguments[++index];
    };

    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            std::exit(EXIT_SUCCESS);
        } else if (argument == "--data") {
            options.dataDirectory = requireValue(index, argument);
        } else if (argument == "--landmarks") {
            options.landmarkCount = parseInteger<std::size_t>(requireValue(index, argument));
        } else if (argument == "--rebuild-landmarks") {
            options.rebuildLandmarks = true;
        } else if (argument == "--cache") {
            options.landmarkCache = requireValue(index, argument);
        } else if (argument == "--from") {
            options.source = parseInteger<NodeId>(requireValue(index, argument));
        } else if (argument == "--to") {
            options.target = parseInteger<NodeId>(requireValue(index, argument));
        } else if (argument == "--from-name") {
            options.sourceName = std::string{requireValue(index, argument)};
        } else if (argument == "--to-name") {
            options.targetName = std::string{requireValue(index, argument)};
        } else if (argument == "--near") {
            options.nearbySource = parseInteger<NodeId>(requireValue(index, argument));
        } else if (argument == "--near-name") {
            options.nearbySourceName = std::string{requireValue(index, argument)};
        } else if (argument == "--category") {
            options.nearbyCategories = parseCategory(requireValue(index, argument));
        } else if (argument == "--count") {
            options.nearbyCount = parseInteger<std::size_t>(requireValue(index, argument));
        } else if (argument == "--route") {
            options.routeFile = std::filesystem::path{requireValue(index, argument)};
        } else if (argument == "--near-route") {
            options.nearbyRouteFile = std::filesystem::path{requireValue(index, argument)};
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else {
            throw std::runtime_error{"ukjent valg: " + std::string{argument}};
        }
    }

    if (options.landmarkCache.empty()) options.landmarkCache = options.dataDirectory / "landemerker.bin";
    return options;
}

void writeRouteFile(const std::filesystem::path& path, const Graph& graph, const std::span<const NodeId> route) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) throw std::runtime_error{"kan ikke skrive " + path.string()};

    for (const NodeId node : route) {
        const Coordinate coordinate = graph.coordinate(node);
        const std::string line = std::format("{:.7f},{:.7f}\n", coordinate.latitude, coordinate.longitude);
        std::fwrite(line.data(), 1, line.size(), file);
    }
    std::fclose(file);
}

void printRoute(const Graph& graph, const SearchResult& result, const bool quiet) {
    if (!result.found()) {
        write("  Ingen rute funnet.\n");
        return;
    }

    write("  Reisetid: {} ({},{} s)\n", formatDuration(result.travelTime), result.travelTime,
          result.travelTime % HUNDREDTHS_PER_SECOND);
    write("  Noder i ruta: {}\n", result.route.size());

    if (quiet) return;

    write("  Første noder: ");
    const std::size_t shown = std::min<std::size_t>(result.route.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) write("{}{}", i == 0 ? "" : " -> ", result.route[i]);
    if (result.route.size() > shown) write(" -> ...");
    write("\n");

    write("  Siste noder: ");
    const std::size_t tail = std::min<std::size_t>(result.route.size(), 8);
    for (std::size_t i = result.route.size() - tail; i < result.route.size(); ++i)
        write("{}{}", i == result.route.size() - tail ? "" : " -> ", result.route[i]);
    write("\n");

    const Coordinate start = graph.coordinate(result.route.front());
    const Coordinate finish = graph.coordinate(result.route.back());
    write("  Start: {:.7f},{:.7f}   Mål: {:.7f},{:.7f}\n", start.latitude, start.longitude, finish.latitude,
          finish.longitude);
}

void runComparison(const Graph& graph, const LandmarkTable& landmarks, const NodeId source, const NodeId target,
                   const bool quiet, const std::optional<std::filesystem::path>& routeFile) {
    SearchEngine engine{graph, &landmarks};

    write("\n=== {} -> {} ===\n", source, target);

    const SearchResult dijkstra = engine.search(source, target, Algorithm::Dijkstra);
    const SearchResult alt = engine.search(source, target, Algorithm::Alt);

    write("\n{:<10} {:>12} {:>12} {:>14} {:>10}\n", "Søk", "Kjøretid", "Prosessert", "Kølagt", "Tid");
    write("{:-<62}\n", "");
    for (const SearchResult& result : {dijkstra, alt}) {
        write("{:<10} {:>12} {:>12} {:>14} {:>10}\n", algorithmName(result.algorithm),
              result.found() ? formatDuration(result.travelTime) : "-", result.processedNodes, result.queuedNodes,
              formatMilliseconds(result.elapsed));
    }

    if (dijkstra.found() && alt.found()) {
        if (dijkstra.travelTime == alt.travelTime) {
            write("\nLik kjøretid: {} ({} hundredeler) - som forventet.\n", formatDuration(dijkstra.travelTime),
                  dijkstra.travelTime);
        } else {
            write("\nAVVIK: Dijkstra {} mot ALT {} - dette skal ikke skje.\n", dijkstra.travelTime, alt.travelTime);
        }
        if (dijkstra.route == alt.route) {
            write("Lik reiserute ({} noder).\n", dijkstra.route.size());
        } else {
            write("Ulik reiserute, men samme kjøretid: {} mot {} noder.\n", dijkstra.route.size(), alt.route.size());
        }
        if (dijkstra.processedNodes > 0) {
            const double ratio = 100.0 * static_cast<double>(alt.processedNodes) / static_cast<double>(dijkstra.processedNodes);
            write("ALT prosesserte {:.1f} % av nodene Dijkstra prosesserte.\n", ratio);
        }
    }

    write("\nDijkstra:\n");
    printRoute(graph, dijkstra, quiet);
    write("\nALT:\n");
    printRoute(graph, alt, quiet);

    if (routeFile) {
        const SearchResult& chosen = alt.found() ? alt : dijkstra;
        if (chosen.found()) {
            writeRouteFile(*routeFile, graph, chosen.route);
            write("\nReiseruta skrevet til {} ({} punkter).\n", routeFile->string(), chosen.route.size());
        }
    }
}

[[nodiscard]] std::string_view placeName(const PlaceIndex& places, const NodeId node) {
    for (const Place& place : places.places())
        if (place.node == node) return place.name;
    return "(ukjent)";
}

void runNearby(const Graph& graph, const PlaceIndex& places, const NodeId source, const std::uint8_t categories,
               const std::size_t count, const std::optional<std::filesystem::path>& markerFile) {
    const std::vector<NodeId> candidates = places.nodesWith(categories);
    write("\n=== Nærmeste {} fra node {} ===\n", categoryLabel(categories), source);
    write("Kandidater i kategorien: {}\n\n", candidates.size());

    SearchEngine engine{graph};
    const std::vector<SearchEngine::PointOfInterest> found = engine.nearestPointsOfInterest(source, candidates, count);

    if (found.empty()) {
        write("Fant ingen interessepunkter.\n");
        return;
    }

    write("{:>4} {:>10} {:>12}  {}\n", "#", "Node", "Reisetid", "Sted");
    write("{:-<60}\n", "");
    for (std::size_t i = 0; i < found.size(); ++i)
        write("{:>4} {:>10} {:>12}  {}\n", i + 1, found[i].node, formatDuration(found[i].travelTime),
              placeName(places, found[i].node));

    if (!markerFile) return;

    std::FILE* file = std::fopen(markerFile->string().c_str(), "wb");
    if (file == nullptr) throw std::runtime_error{"kan ikke skrive " + markerFile->string()};
    for (const SearchEngine::PointOfInterest& point : found) {
        const Coordinate coordinate = graph.coordinate(point.node);
        const std::string line = std::format("{:.7f},{:.7f},{}\n", coordinate.latitude, coordinate.longitude,
                                             placeName(places, point.node));
        std::fwrite(line.data(), 1, line.size(), file);
    }
    std::fclose(file);
    write("\nInteressepunktene skrevet til {}.\n", markerFile->string());
}

} // namespace

int main(int argc, char** argv) {
    try {
        useUtf8Console();
        const Options options = parseOptions(commandLineArguments(argc, argv));

        const auto loadStart = std::chrono::steady_clock::now();
        write("Leser kart fra {} ...\n", options.dataDirectory.string());
        const Graph graph = Graph::load(options.dataDirectory);
        const PlaceIndex places = PlaceIndex::load(options.dataDirectory / "interessepkt.txt");
        const auto loadEnd = std::chrono::steady_clock::now();
        write("  {} noder, {} kanter, {} interessepunkter ({}).\n", graph.nodeCount(), graph.edgeCount(),
              places.places().size(), formatMilliseconds(loadEnd - loadStart));

        LandmarkTable landmarks;
        if (std::filesystem::exists(options.landmarkCache) && !options.rebuildLandmarks) {
            write("Leser landemerker fra {} ...\n", options.landmarkCache.string());
            landmarks = LandmarkTable::load(options.landmarkCache);
            if (landmarks.nodeCount() != graph.nodeCount())
                throw std::runtime_error{"landemerkefila passer ikke til kartet - kjør med --rebuild-landmarks"};
        } else {
            write("Preprosesserer {} landemerker (dette tar noen minutter) ...\n", options.landmarkCount);
            landmarks = LandmarkTable::build(graph, options.landmarkCount, [](const std::size_t landmark, const std::size_t total) {
                write("  landemerke {}/{} ferdig\n", landmark + 1, total);
            });
            landmarks.save(options.landmarkCache);
            write("  lagret i {}\n", options.landmarkCache.string());
        }

        write("\nLandemerker ({}):\n", landmarks.count());
        for (std::size_t i = 0; i < landmarks.count(); ++i) {
            const NodeId node = landmarks.node(i);
            const Coordinate coordinate = graph.coordinate(node);
            write("  {}. node {:>8}  {:.5f},{:.5f}  rekker {} noder, nås fra {} noder\n", i + 1, node,
                  coordinate.latitude, coordinate.longitude, landmarks.reachableFrom(i), landmarks.reachableTo(i));
        }

        const auto resolve = [&](const std::optional<NodeId>& node, const std::optional<std::string>& name,
                                 const std::string_view role) -> std::optional<NodeId> {
            if (node) return node;
            if (!name) return std::nullopt;
            const Place* place = places.findByName(*name);
            if (place == nullptr) throw std::runtime_error{"fant ikke " + std::string{role} + ": " + *name};
            write("  {} '{}' er node {}\n", role, place->name, place->node);
            return place->node;
        };

        const std::optional<NodeId> source = resolve(options.source, options.sourceName, "start");
        const std::optional<NodeId> target = resolve(options.target, options.targetName, "mål");

        if (source && target) {
            runComparison(graph, landmarks, *source, *target, options.quiet, options.routeFile);
        }

        const std::optional<NodeId> nearbySource = resolve(options.nearbySource, options.nearbySourceName, "utgangspunkt");
        if (nearbySource)
            runNearby(graph, places, *nearbySource, options.nearbyCategories, options.nearbyCount, options.nearbyRouteFile);

        if (!source && !target && !nearbySource) printUsage();

        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        writeError("Feil: {}\n", error.what());
        return EXIT_FAILURE;
    }
}
