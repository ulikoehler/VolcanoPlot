// volcano/plot/plots/Hist2DPlot.hpp — 2D histogram (matplotlib `hist2d`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <memory>
#include <vector>
#include <string>
#include <optional>

namespace volcano::plot {

/// Bin-count selection method for 2D histogram axes.
enum class Hist2DBinMethod {
    Auto,       ///< max(Sturges, FD) per axis
    Fixed,      ///< user-specified nx × ny bins
    Edges,      ///< user-specified bin edges
};

/// 2D histogram normalization mode.
enum class Hist2DNorm {
    Count,      ///< raw counts (default)
    Density,    ///< count / (total * bin_area) — integrates to 1
    Probability,///< count / total — sums to 1
};

/// 2D histogram configuration.
struct Hist2DConfig {
    Hist2DBinMethod bins = Hist2DBinMethod::Auto;
    int nBinsX = 10;               ///< used when bins = Fixed
    int nBinsY = 10;               ///< used when bins = Fixed
    std::vector<float> xEdges;     ///< used when bins = Edges
    std::vector<float> yEdges;     ///< used when bins = Edges
    std::optional<Range> xRange;   ///< data range; auto if unset
    std::optional<Range> yRange;
    Hist2DNorm normMode = Hist2DNorm::Count;
    const Colormap* cmap = nullptr;  ///< colormap (nullptr = viridis)
    /// Explicit count/value range; auto if invalid.
    /// Ignored if norm is set (use norm->setVmin/setVmax instead).
    Range valueRange{0, 0};
    /// Optional normalization. If set, replaces the linear (vmin,vmax) mapping.
    std::shared_ptr<Normalize> norm;
    std::string label;
};

/// 2D histogram plot. Bins (x, y) sample pairs into a 2D grid and renders
/// as colored cells. Equivalent to matplotlib's `hist2d(x, y)`.
class Hist2DPlot : public IPlot {
public:
    /// Construct from raw (x, y) sample pairs.
    Hist2DPlot(std::vector<float> x, std::vector<float> y,
               Hist2DConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

    /// Access computed bin edges (valid after prepare()).
    [[nodiscard]] const std::vector<float>& xEdges() const { return xEdges_; }
    [[nodiscard]] const std::vector<float>& yEdges() const { return yEdges_; }
    /// Access computed counts grid (valid after prepare()).
    /// counts_[j * nBinsX_ + i] = count in cell (i, j).
    [[nodiscard]] const std::vector<float>& counts() const { return counts_; }

private:
    std::vector<float> x_, y_;
    Hist2DConfig config_;

    // Computed in prepare().
    std::vector<float> xEdges_, yEdges_;
    std::vector<float> counts_;  // nBinsX * nBinsY, row-major
    uint32_t nBinsX_ = 0, nBinsY_ = 0;
    Range valueRange_;

    render::primitives::FillRenderer renderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeBins();
    void buildGeometry();
};

} // namespace volcano::plot
