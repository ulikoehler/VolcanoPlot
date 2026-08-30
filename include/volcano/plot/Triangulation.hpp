// volcano/plot/Triangulation.hpp — Delaunay triangulation for unstructured points
#pragma once
#include <volcano/plot/Types.hpp>
#include <vector>
#include <cstdint>

namespace volcano::plot {

/// A triangle defined by 3 point indices.
struct Triangle {
    uint32_t a, b, c;
};

/// Compute a Delaunay triangulation of a set of 2D points.
/// Uses the Bowyer-Watson incremental algorithm.
/// Returns triangles as indices into the points vector.
///
/// @param points Input 2D points.
/// @return Vector of triangles (index triples).
std::vector<Triangle> delaunay(const std::vector<Point2D>& points);

} // namespace volcano::plot
