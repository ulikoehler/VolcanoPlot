// volcano/plot/plots/SpyPlot.hpp — sparsity pattern plot (matplotlib `spy`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Spy plot configuration.
struct SpyConfig {
    /// Color for non-zero elements.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Color for zero elements. If alpha=0, zero cells are not rendered.
    Color zeroColor = Color::transparent();
    /// Threshold for considering an element non-zero (|value| > precision).
    float precision = 0.0f;
    /// Marker size as fraction of cell size (1.0 = fill cell, 0.5 = half).
    float markerSize = 1.0f;
    std::string label;
};

/// Spy plot — visualizes the sparsity pattern of a 2D array/matrix.
/// Equivalent to matplotlib's `spy(Z)`.
///
/// Non-zero elements are rendered as filled cells (or markers within cells),
/// zero elements are either skipped or rendered with `zeroColor`.
/// Cells are centered on integer data coordinates (mpl imshow extent,
/// -0.5 … n-0.5); `Axes::spy()` applies the full mpl wiring (inverted y
/// → row 0 on top, aspect='equal', integer index locators).
class SpyPlot : public IPlot {
public:
    /// Construct from a 2D matrix (row-major, nrows × ncols).
    SpyPlot(std::vector<float> data, uint32_t nrows, uint32_t ncols,
            SpyConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(render::Cmd& cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// mpl sticky edges: tight autoscale, no 5% margin.
    [[nodiscard]] bool tightAutoscale() const override { return true; }
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<float> data_;
    uint32_t nrows_, ncols_;
    SpyConfig config_;

    std::unique_ptr<render::primitives::FillRenderer> fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void buildGeometry();
};

} // namespace volcano::plot
