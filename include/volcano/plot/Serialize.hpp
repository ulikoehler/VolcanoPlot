// volcano/plot/Serialize.hpp — Figure ↔ JSON round-trip
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace volcano::plot {

class Figure;

/// Serialize a Figure to JSON. Covers the grid layout, axes viewports /
/// labels / title / scales / legend visibility / grids, and the common
/// series layers (line, scatter, bar, errorbar) with their style fields.
/// Layers of other types are skipped.
std::string figureToJson(const Figure& fig);

/// Parse a JSON document produced by figureToJson back into a Figure.
/// Returns nullptr on malformed input; unknown plot "type" entries are
/// skipped so forward-compatible documents still load.
std::unique_ptr<Figure> figureFromJson(std::string_view json);

/// Convenience file wrappers.
bool saveFigureJson(const Figure& fig, const std::filesystem::path& path);
std::unique_ptr<Figure> loadFigureJson(const std::filesystem::path& path);

} // namespace volcano::plot
