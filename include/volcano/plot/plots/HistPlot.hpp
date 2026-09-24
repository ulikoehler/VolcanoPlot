// volcano/plot/plots/HistPlot.hpp — histogram plot (matplotlib `hist`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <memory>
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

/// Histogram rendering style (matplotlib `histtype`).
enum class HistType {
    Bar,        ///< filled bars; multiple datasets side-by-side (default)
    BarStacked, ///< filled bars stacked on top of each other
    Step,       ///< unfilled step outline
    StepFilled, ///< filled step outline down to the baseline
};

/// Histogram configuration.
struct HistConfig {
    HistBinMethod bins = HistBinMethod::Auto;
    int binCount = 10;           ///< used when bins = Fixed
    std::vector<float> binEdges; ///< used when bins = Edges
    std::optional<Range> range;  ///< data range; auto if unset
    HistNorm norm = HistNorm::Count;
    HistType histtype = HistType::Bar;
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Per-dataset colors (multi-dataset hist); empty → `color` for all.
    std::vector<Color> colors;
    std::string label;
    /// Step outline line width (histtype=step).
    float stepLineWidth = 1.5f;
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
        : datasets_{std::move(samples)}, cfg_(std::move(cfg)) {}

    /// Construct from multiple datasets (matplotlib hist([a, b, ...])).
    /// Shared bin edges; rendering depends on cfg.histtype.
    HistPlot(std::vector<std::vector<float>> datasets, HistConfig cfg = {})
        : datasets_(std::move(datasets)), cfg_(std::move(cfg)) {}

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
    /// Access computed bin heights of the first dataset (valid after prepare()).
    [[nodiscard]] const std::vector<float>& binHeights() const { return heights_.front(); }
    /// Per-dataset bin heights (valid after prepare()).
    [[nodiscard]] const std::vector<std::vector<float>>& binHeightsAll() const { return heights_; }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

    /// Eagerly (re)compute the derived arrays so bindings can return
    /// mpl's `(values, ...) ` tuples before the first draw. Idempotent.
    void ensureComputed() { computeBins(); }

private:
    std::vector<std::vector<float>> datasets_;
    HistConfig cfg_;
    std::vector<float> binEdges_;                  // shared, computed in prepare()
    std::vector<std::vector<float>> heights_;      // per-dataset heights
    render::primitives::FillRenderer renderer_;
    /// One segment renderer per dataset (histtype=step, uniform color each).
    std::vector<std::unique_ptr<render::primitives::LineSegmentRenderer>>
        stepRenderers_;
    std::vector<uint32_t> stepCounts_;
    std::vector<std::vector<Point2D>> stepSegs_;   // per-dataset segments
    std::vector<Point2D> uploadedPoints_;  // for GPU autoscale
    bool prepared_ = false;

    /// Compute shared bin edges and per-dataset heights.
    void computeBins();

    /// Build triangle vertices for the histogram bars / step fills.
    void buildBarVertices(std::vector<Point2D>& positions,
                          std::vector<Color>& colors) const;
    /// Build step outline segments (histtype=step).
    void buildStepSegments();
};

} // namespace volcano::plot
