// volcano/plot/plots/MexicanHatPlot.hpp — 3D Mexican hat (Ricker) wavelet plot
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

/// Configuration for MexicanHatPlot.
struct MexicanHatConfig {
    /// Colormap for surface coloring (based on z-height).
    std::string colormap = "viridis";
    /// Wireframe line color.
    Color wireframeColor = Color::black();
    /// Wireframe line width.
    float wireframeWidth = 0.5f;
    /// Whether to draw the wireframe overlay.
    bool drawWireframe = true;
    /// Whether to draw the filled surface.
    bool drawSurface = true;
    /// Stride for wireframe rows (1 = every row).
    uint32_t rowStride = 1;
    /// Stride for wireframe columns (1 = every column).
    uint32_t colStride = 1;
    /// Label for legend.
    std::string label;
};

/// 3D Mexican hat (Ricker) wavelet plot.
///
/// The 2D Mexican hat wavelet (also known as the Ricker wavelet or Marr
/// wavelet) is the second derivative of a Gaussian:
///   psi(x,y) = (2 - (x^2 + y^2) / sigma^2) * exp(-(x^2 + y^2) / (2*sigma^2))
///
/// This plot evaluates the wavelet on a regular grid and renders it as a
/// 3D surface, with z = psi(x,y). The surface is projected through Camera3D
/// to 2D NDC on the CPU, with painter's algorithm depth sorting for correct
/// occlusion. Surface faces are colored by z-height via a colormap.
/// An optional wireframe overlay shows the grid structure.
class MexicanHatPlot : public IPlot {
public:
    /// Construct a Mexican hat wavelet plot.
    /// `sigma` controls the width of the wavelet.
    /// `xRange`, `yRange` define the evaluation domain.
    /// `gridW`, `gridH` are the grid resolution.
    MexicanHatPlot(float sigma, Range xRange, Range yRange,
                   uint32_t gridW = 50, uint32_t gridH = 50,
                   MexicanHatConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return Color::blue(); }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

private:
    float sigma_;
    Range xRange_, yRange_;
    uint32_t gridW_, gridH_;
    MexicanHatConfig config_;
    Camera3D camera_;

    // Grid data: z-values at each (x, y) grid point.
    std::vector<float> zValues_;
    float zMin_ = 0.0f, zMax_ = 0.0f;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;

    render::primitives::LineSegmentRenderer wireRenderer_;
    std::vector<Point2D> wireSegments_;

    bool prepared_ = false;

    void evaluateWavelet();
    void projectSurface();
};

} // namespace volcano::plot
