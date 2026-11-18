//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++26
//    Build: UCRT GCC (Windows) / GCC (Linux), GCC 15+
//
// Tegner en reiserute (eller interessepunkter) som SVG, med en enkel
// equirectangular projeksjon. Ruta leses fra fila som ruting.exe skriver med
// --route: én "breddegrad,lengdegrad" per linje.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "arguments.h"
#include "format_util.h"
#include "text_scan.h"

namespace {

using namespace routing;

struct Point {
    double latitude;
    double longitude;
    std::string label;
};

[[nodiscard]] std::vector<Point> readPoints(const std::filesystem::path& path) {
    std::ifstream stream{path};
    if (!stream) throw std::runtime_error{"kan ikke åpne " + path.string()};

    std::vector<Point> points;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const std::size_t comma = line.find(',');
        if (comma == std::string::npos) continue;

        Point point{};
        const std::string_view latitude = std::string_view{line}.substr(0, comma);
        const std::string_view rest = std::string_view{line}.substr(comma + 1);
        const std::size_t secondComma = rest.find(',');
        const std::string_view longitude = rest.substr(0, secondComma);
        if (secondComma != std::string_view::npos) point.label = std::string{rest.substr(secondComma + 1)};

        const auto [latPointer, latError] =
            std::from_chars(latitude.data(), latitude.data() + latitude.size(), point.latitude);
        const auto [lonPointer, lonError] =
            std::from_chars(longitude.data(), longitude.data() + longitude.size(), point.longitude);
        if (latError != std::errc{} || lonError != std::errc{}) continue;
        points.push_back(std::move(point));
    }
    return points;
}

[[nodiscard]] std::string escapeXml(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

void writeSvg(const std::filesystem::path& path, const std::vector<Point>& points, const std::string_view title,
              const bool markersOnly) {
    if (points.empty()) throw std::runtime_error{"ingen punkter å tegne"};

    constexpr double WIDTH = 1400.0;
    constexpr double HEIGHT = 1400.0;
    constexpr double MARGIN = 40.0;
    constexpr double TOP_MARGIN = 70.0;

    double minLat = points.front().latitude;
    double maxLat = minLat;
    double minLon = points.front().longitude;
    double maxLon = minLon;
    for (const Point& point : points) {
        minLat = std::min(minLat, point.latitude);
        maxLat = std::max(maxLat, point.latitude);
        minLon = std::min(minLon, point.longitude);
        maxLon = std::max(maxLon, point.longitude);
    }

    // Equirectangular projection: longitude is scaled by cos(latitude) so the
    // route keeps roughly the right shape instead of being stretched east-west.
    // 8 % luft rundt ytterpunktene gjør at punkter på kanten ikke klistrer seg
    // til bilderamma.
    constexpr double PADDING = 0.08;
    const double rawSpanLat = maxLat - minLat;
    const double rawSpanLon = maxLon - minLon;
    minLat -= rawSpanLat * PADDING;
    maxLat += rawSpanLat * PADDING;
    minLon -= rawSpanLon * PADDING;
    maxLon += rawSpanLon * PADDING;

    const double meanLatitude = (minLat + maxLat) / 2.0;
    const double lonScale = std::cos(meanLatitude * std::numbers::pi / 180.0);
    const double spanLat = std::max(maxLat - minLat, 1e-6);
    const double spanLon = std::max((maxLon - minLon) * lonScale, 1e-6);
    const double scale = std::min((WIDTH - 2 * MARGIN) / spanLon, (HEIGHT - TOP_MARGIN - MARGIN) / spanLat);

    const double offsetX = (WIDTH - spanLon * scale) / 2.0;
    const double offsetY = TOP_MARGIN + (HEIGHT - TOP_MARGIN - MARGIN - spanLat * scale) / 2.0;

    const auto project = [&](const Point& point) {
        const double x = offsetX + (point.longitude - minLon) * lonScale * scale;
        const double y = offsetY + (maxLat - point.latitude) * scale;
        return std::pair{x, y};
    };

    std::ofstream stream{path, std::ios::trunc};
    if (!stream) throw std::runtime_error{"kan ikke skrive " + path.string()};

    stream << std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="0 0 {} {}">
  <rect width="100%" height="100%" fill="#101418"/>
  <text x="{}" y="28" fill="#e8eef5" font-family="sans-serif" font-size="20">{}</text>
)",
        WIDTH, HEIGHT, WIDTH, HEIGHT, MARGIN, escapeXml(title));

    if (!markersOnly) {
        stream << R"(  <polyline fill="none" stroke="#3fa9f5" stroke-width="2.5" stroke-linejoin="round" points=")";
        for (const Point& point : points) {
            const auto [x, y] = project(point);
            stream << std::format("{:.2f},{:.2f} ", x, y);
        }
        stream << "\"/>\n";
    }

    const auto marker = [&](const Point& point, const std::string_view colour, const double radius) {
        const auto [x, y] = project(point);
        stream << std::format(R"(  <circle cx="{:.2f}" cy="{:.2f}" r="{}" fill="{}"/>)", x, y, radius, colour);
        if (!point.label.empty()) {
            // Hold etiketten innenfor bildet: 7.6 px per tegn er et godt anslag
            // for sans-serif i 14 px, og etiketten legges til venstre for punktet
            // hvis den ikke får plass til høyre.
            const double estimatedWidth = 7.6 * static_cast<double>(point.label.size());
            const bool fitsRight = x + radius + 4 + estimatedWidth <= WIDTH - MARGIN / 2;
            const double labelX = fitsRight ? x + radius + 4 : x - radius - 4 - estimatedWidth;
            stream << std::format(
                R"(  <text x="{:.2f}" y="{:.2f}" fill="#e8eef5" font-family="sans-serif" font-size="14">{}</text>)",
                labelX, y + 5, escapeXml(point.label));
        }
        stream << '\n';
    };

    if (markersOnly) {
        for (const Point& point : points) marker(point, "#f5c542", 7.0);
    } else {
        marker(points.front(), "#4ade80", 7.0);
        marker(points.back(), "#f87171", 7.0);
    }

    stream << "</svg>\n";
    if (!stream) throw std::runtime_error{"feil under skriving av " + path.string()};
}

} // namespace

int main(int argc, char** argv) {
    try {
        useUtf8Console();
        const std::vector<std::string> arguments = commandLineArguments(argc, argv);
        if (arguments.size() < 3) {
            write("Bruk: plot <innfil> <utfil.svg> [--markers] [--title <tekst>]\n");
            return EXIT_FAILURE;
        }

        const std::filesystem::path input = arguments[1];
        const std::filesystem::path output = arguments[2];
        bool markersOnly = false;
        std::string title = "Reiserute";
        for (std::size_t index = 3; index < arguments.size(); ++index) {
            const std::string_view argument = arguments[index];
            if (argument == "--markers") {
                markersOnly = true;
            } else if (argument == "--title" && index + 1 < arguments.size()) {
                title = arguments[++index];
            }
        }

        const std::vector<Point> points = readPoints(input);
        writeSvg(output, points, title, markersOnly);
        write("Skrev {} ({} punkter) til {}\n", markersOnly ? "punkter" : "rute", points.size(), output.string());
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        writeError("Feil: {}\n", error.what());
        return EXIT_FAILURE;
    }
}
