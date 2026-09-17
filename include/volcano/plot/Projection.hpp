// volcano/plot/Projection.hpp — matplotlib-style axes projections
//
// A projection maps 2D data (x, y) to display plane coordinates before the
// linear viewport mapping. Implemented in the vertex shaders via a
// projection code so it applies uniformly to all plot types.
#pragma once

#include "volcano/plot/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

/// Projection kinds; the integer values are the shader projection codes.
enum class ProjectionKind : int {
    Rectilinear = 0,
    Polar = 1,      ///< data = (theta[rad], r) → (r·cosθ, r·sinθ)
    Aitoff = 2,     ///< data = (lon[rad], lat[rad])
    Hammer = 3,
    Lambert = 4,    ///< Lambert azimuthal equal-area
    Mollweide = 5,
};

struct Projection {
    ProjectionKind kind = ProjectionKind::Rectilinear;
    /// Polar parameters: rotation of theta=0 (radians) and direction
    /// (+1 counterclockwise, -1 clockwise).
    float thetaOffset = 0.0f;
    float thetaDir = 1.0f;

    /// data -> projected (x, y) plane.
    [[nodiscard]] Point2D forward(Point2D p) const noexcept;
    /// projected (x, y) -> data (inverse; approximate for iterative maps).
    [[nodiscard]] Point2D inverse(Point2D p) const noexcept;

    /// Viewport covering the projected plane for full-range data.
    /// For geo projections these are the fixed natural bounds; for polar
    /// pass rmax to get [-rmax, rmax]².
    [[nodiscard]] Viewport bounds(float rmax = 1.0f) const noexcept;

    static Projection parse(std::string_view name);
    [[nodiscard]] std::string_view name() const noexcept;
};

} // namespace volcano::plot
