// volcano/plot/plots/HistPlot.hpp — histogram plot (matplotlib `hist`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Bin-count selection method (mirrors matplotlib's `bins` parameter).
enum class HistBinMethod {
    Auto,       ///< max(Sturges, FD) — matplotlib's default
    Sturges,    ///< ceil(log2(n) + 1)
    FD,         ///< Freedman-Diaconis (2*IQR/n^(1/3))
    Rice,       ///< 2*n^(1/3)
    Square,     ///< sqrt(n)
    Fixed,      ///< user-specified number of bins
    Edges,      ///< user-specified bin edges
};

/// Histogram normalization mode.
enum class HistNorm {
    Count,      ///< raw counts (default)
    Density,    ///< count / (total * bin_width) — area sums to 1
    Probability,///< count / total — heights sum to 1
    Cumulative, ///< cumulative count
};

/// Histogram configuration.
struct HistConfig {
    HistBinMethod bins = HistBinMethod::Auto;
    int binCount = 10;           ///< used when bins = Fixed
    std::vector<float> binEdges; ///< used when bins = Edges
    std::optional<Range> range;  ///< data range; auto if unset
    HistNorm norm = HistNorm::Count;
    Color color = Color::fromRgba8(31, 119, 180, 128);
    std::string label;
    bool horizontal = false;     ///< horizontal histogram (bars along X)
};

/// Histogram plot. Computes bins from raw sample data and renders as
/// filled bars. Equivalent to matplotlib's `hist(x, bins=...)`.
///
/// Features:
///   - Automatic bin count (Sturges, Freedman-Diaconis, or fixed N)
///   - Fixed bin edges or automatic range
///   - Count or density normalization
///   - Cumulative mode
///   - Per-bin or uniform color
///   - Alpha blending for overlapping histograms
class HistPlot : public IPlot {
public:
    /// Construct from raw samples.
    explicit HistPlot(std::vector<float> samples, HistConfig cfg = {})
        : samples_(std::move(samples)), cfg_(std::move(cfg)) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return cfg_.label; }
    [[nodiscard]] Color legendColor() const override { return cfg_.color; }

    /// Access computed bin edges (valid after prepare()).
    [[nodiscard]] const std::vector<float>& binEdges() const { return binEdges_; }
    /// Access computed bin heights (valid after prepare()).
    [[nodiscard]] const std::vector<float>& binHeights() const { return heights_; }

private:
    std::vector<float> samples_;
    HistConfig cfg_;
    std::vector<float> binEdges_;  // computed in prepare()
    std::vector<float> heights_;   // computed in prepare()
    render::primitives::FillRenderer renderer_;
    std::vector<Point2D> uploadedPoints_;  // for GPU autoscale
    bool prepared_ = false;

    /// Compute bin edges and heights from samples_.
    void computeBins();

    /// Build triangle vertices for the histogram bars.
    void buildBarVertices(std::vector<Point2D>& positions,
                          std::vector<Color>& colors) const;
};

} // namespace volcano::plot
