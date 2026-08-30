// volcano/plot/plots/Collections3D.hpp — 3D line and polygon collections
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

// ═══════════════════════════════════════════════════════════════════════════
// Line3DCollection
// ═══════════════════════════════════════════════════════════════════════════

/// Configuration for a 3D line collection.
struct Line3DCollectionConfig {
    /// Default color for all segments (used when per-segment colors are empty).
    Color color = Color::black();
    /// Per-segment colors (optional, overrides `color`).
    std::vector<Color> colors;
    /// Line width.
    float lineWidth = 1.0f;
    /// Label for legend.
    std::string label;
};

/// 3D line segment collection. Renders a set of independent 3D line segments,
/// each defined by a start and end point. Equivalent to matplotlib's
/// `Line3DCollection`.
///
/// All 3D points are projected to 2D NDC on the CPU using the Camera3D
/// view-projection matrix, then rendered as independent line segments via
/// LineSegmentRenderer. Per-segment colors are supported.
class Line3DCollection : public IPlot {
public:
    /// Construct from segment endpoints.
    /// `segs` is a vector of (start, end) pairs: [x0,y0,z0, x1,y1,z1, ...].
    /// Each segment is 6 floats (2 points × 3 coords).
    Line3DCollection(std::vector<float> segments, Line3DCollectionConfig config = {});

    /// Construct from a vector of (start, end) point pairs.
    Line3DCollection(std::vector<std::pair<Point3D, Point3D>> segments,
                     Line3DCollectionConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<std::pair<Point3D, Point3D>> segments_;
    Line3DCollectionConfig config_;
    Camera3D camera_;

    render::primitives::LineSegmentRenderer renderer_;
    std::vector<Point2D> projected_;       // 2 points per segment
    std::vector<Color> segmentColors_;     // 1 color per segment (for per-vertex)
    bool prepared_ = false;

    void projectSegments();
};

// ═══════════════════════════════════════════════════════════════════════════
// Poly3DCollection
// ═══════════════════════════════════════════════════════════════════════════

/// Configuration for a 3D polygon collection.
struct Poly3DCollectionConfig {
    /// Default face color (used when per-polygon colors are empty).
    Color faceColor = Color::fromRgba8(31, 119, 180, 200);
    /// Per-polygon face colors (optional, overrides `faceColor`).
    std::vector<Color> faceColors;
    /// Edge color for polygon outlines.
    Color edgeColor = Color::black();
    /// Edge line width.
    float edgeWidth = 0.5f;
    /// Whether to draw edge outlines.
    bool drawEdges = true;
    /// Whether to draw filled faces.
    bool drawFaces = true;
    /// Label for legend.
    std::string label;
};

/// 3D polygon collection. Renders a set of 3D polygons (each with arbitrary
/// number of vertices), projected through Camera3D and sorted by average
/// depth (painter's algorithm). Equivalent to matplotlib's `Poly3DCollection`.
///
/// Each polygon is fan-triangulated and rendered as filled triangles via
/// FillRenderer. Optional edge outlines are drawn via LineSegmentRenderer.
/// Per-polygon colors are supported.
class Poly3DCollection : public IPlot {
public:
    /// Construct from a vector of polygons, each a vector of 3D vertices.
    Poly3DCollection(std::vector<std::vector<Point3D>> polygons,
                     Poly3DCollectionConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.faceColor; }

private:
    std::vector<std::vector<Point3D>> polygons_;
    Poly3DCollectionConfig config_;
    Camera3D camera_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;

    render::primitives::LineSegmentRenderer edgeRenderer_;
    std::vector<Point2D> edgeSegments_;

    bool prepared_ = false;

    void projectPolygons();
};

} // namespace volcano::plot
