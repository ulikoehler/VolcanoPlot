// volcano/plot/plots/VoxelsPlot.hpp — 3D voxel plot (matplotlib `Axes3D.voxels`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for VoxelsPlot.
struct VoxelsConfig {
    /// Uniform color for all voxels (used when per-voxel colors are empty).
    Color color = Color::fromRgba8(31, 119, 180, 200);
    /// Per-voxel colors (optional, overrides `color`).
    std::vector<Color> colors;
    /// Edge color for voxel outlines.
    Color edgeColor = Color::black();
    /// Edge line width.
    float edgeWidth = 0.5f;
    /// Whether to draw voxel edge outlines.
    bool drawEdges = true;
    /// Label for legend.
    std::string label;
};

/// 3D voxel plot — renders a 3D grid of filled cubes.
/// Equivalent to matplotlib's `Axes3D.voxels`.
///
/// The voxel data is a 3D boolean array (filled = true). Each filled voxel
/// is rendered as a unit cube with 6 faces. All 3D points are projected to
/// 2D NDC on the CPU using the Camera3D view-projection matrix. Faces are
/// sorted by average depth (painter's algorithm) for correct occlusion.
/// Per-face shading provides simple lighting. Optional edge outlines are
/// drawn via LineSegmentRenderer.
class VoxelsPlot : public IPlot {
public:
    /// Construct from a 3D boolean array.
    /// `filled` is indexed as [x * ny * nz + y * nz + z].
    VoxelsPlot(std::vector<uint8_t> filled, uint32_t nx, uint32_t ny, uint32_t nz,
               VoxelsConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<uint8_t> filled_;
    uint32_t nx_, ny_, nz_;
    VoxelsConfig config_;
    Camera3D camera_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;

    render::primitives::LineSegmentRenderer edgeRenderer_;
    std::vector<Point2D> edgeSegments_;

    bool prepared_ = false;

    void projectVoxels();
};

} // namespace volcano::plot
