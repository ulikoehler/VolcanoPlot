// volcano/plot/Stroke.hpp — CPU polyline stroker (dashes, joins, caps)
#pragma once

#include <volcano/plot/Types.hpp>

#include <span>
#include <vector>

namespace volcano::plot {

/// Parameters for stroking a polyline in pixel space.
struct StrokeParams {
    float width = 1.5f;                 ///< stroke width in pixels
    std::vector<float> dashes;          ///< on/off lengths in px; empty = solid
    float dashOffset = 0.0f;            ///< offset into the dash pattern, px
    JoinStyle join = JoinStyle::Round;  ///< vertex join style
    CapStyle cap = CapStyle::Butt;      ///< end cap style
    float miterLimit = 10.0f;           ///< miter limit (× half-width)
};

/// A flat triangle soup in pixel space (non-indexed: 3 vertices per tri).
struct TriMesh {
    std::vector<Point2D> verts;         ///< 3 per triangle
    [[nodiscard]] uint32_t triCount() const {
        return static_cast<uint32_t>(verts.size() / 3);
    }
    [[nodiscard]] bool empty() const noexcept { return verts.empty(); }
};

/// Stroke a pixel-space polyline into a triangle mesh, honoring dashes,
/// joins, and caps. Points with non-finite coordinates split the polyline.
[[nodiscard]] TriMesh strokePolyline(std::span<const Point2D> points,
                                     const StrokeParams& params);

/// Split a polyline into dash sub-polylines following the on/off pattern.
/// Each returned polyline should be stroked separately (caps apply per dash).
[[nodiscard]] std::vector<std::vector<Point2D>>
dashSplit(std::span<const Point2D> points, std::span<const float> dashes,
          float dashOffset);

/// xkcd-style sketch wobble (mpl `Path.sketch`): resample the polyline and
/// perturb it with smooth correlated perpendicular noise.
/// `scale` = amplitude in px, `length` = wobble wavelength in px.
[[nodiscard]] std::vector<Point2D>
sketchPolyline(std::span<const Point2D> points, float scale,
               float length = 128.0f, uint64_t seed = 0);

} // namespace volcano::plot
