// volcano/plot/plots/BoxPlot.hpp — box-and-whisker plot (matplotlib `boxplot`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Whisker range specification for BoxPlot.
enum class BoxWhiskerType {
    IQR_15,     ///< 1.5×IQR (matplotlib default)
    MinMax,     ///< whiskers extend to min/max of data
    Percentile, ///< whiskers at given percentiles (cfg.whiskerLo/Hi)
};

/// Configuration for BoxPlot.
struct BoxPlotConfig {
    BoxWhiskerType whisker = BoxWhiskerType::IQR_15;
    float whiskerLo = 5.0f;    ///< lower percentile (0-100) if Percentile
    float whiskerHi = 95.0f;   ///< upper percentile (0-100) if Percentile
    float boxWidth = 0.5f;     ///< box width in data units (x-axis)
    Color boxColor = Color::fromRgba8(31, 119, 180, 128);   ///< box fill
    Color boxEdgeColor = Color::fromRgba8(31, 119, 180, 255); ///< box edge
    Color whiskerColor = Color::black();   ///< whisker + cap color
    Color medianColor = Color::fromRgba8(255, 127, 14, 255); ///< median line
    Color outlierColor = Color::fromRgba8(31, 119, 180, 255); ///< outlier markers
    float lineWidth = 1.0f;    ///< box edge and whisker line width
    float medianWidth = 2.0f;  ///< median line width
    float outlierSize = 4.0f;  ///< outlier marker size in pixels
    float capSize = 0.2f;      ///< cap width in data units
    bool showOutliers = true;  ///< draw outlier points
    bool fillBox = true;       ///< fill the box with boxColor
    std::vector<std::string> labels; ///< per-group labels for legend
    std::string label;         ///< overall legend label
};

/// Box-and-whisker plot. Computes quartiles, whiskers, and outliers for
/// one or more groups of data, then renders boxes, whiskers, median lines,
/// caps, and outlier points. Equivalent to matplotlib's `boxplot(data)`.
class BoxPlot : public IPlot {
public:
    /// Construct from a single group of data.
    explicit BoxPlot(std::vector<float> data, BoxPlotConfig cfg = {})
        : cfg_(std::move(cfg)) {
        groups_.push_back(std::move(data));
    }

    /// Construct from multiple groups of data.
    BoxPlot(std::vector<std::vector<float>> groups, BoxPlotConfig cfg = {})
        : groups_(std::move(groups)), cfg_(std::move(cfg)) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return cfg_.label; }
    [[nodiscard]] Color legendColor() const override { return cfg_.boxEdgeColor; }

    /// Computed statistics for one group (valid after prepare()).
    struct Stats {
        float q1, median, q3;
        float whiskerLo, whiskerHi;
        float min, max;
        std::vector<float> outliers;
    };
    [[nodiscard]] const std::vector<Stats>& stats() const { return stats_; }

private:
    std::vector<std::vector<float>> groups_;
    BoxPlotConfig cfg_;
    std::vector<Stats> stats_;  // computed in prepare()

    // Renderers.
    render::primitives::FillRenderer boxFillRenderer_;       // box fills
    render::primitives::LineSegmentRenderer boxEdgeRenderer_; // box edges + whiskers + caps
    render::primitives::LineSegmentRenderer medianRenderer_;  // median lines
    render::primitives::PointRenderer outlierRenderer_;       // outlier points

    // Computed geometry.
    std::vector<Point2D> boxFillVerts_;
    std::vector<Color> boxFillColors_;
    std::vector<Point2D> boxEdgeSegs_;
    std::vector<Point2D> medianSegs_;
    std::vector<Point2D> outlierPoints_;
    std::vector<Color> outlierColors_;
    std::vector<float> outlierSizes_;
    uint32_t boxFillCount_ = 0;
    uint32_t boxEdgeCount_ = 0;
    uint32_t medianCount_ = 0;
    uint32_t outlierCount_ = 0;

    bool prepared_ = false;

    /// Compute statistics for a single group.
    Stats computeStats(const std::vector<float>& data) const;

    /// Build all geometry from computed stats.
    void buildGeometry();

    /// Linear interpolation percentile (0-100).
    static float percentile(const std::vector<float>& sorted, float p);
};

} // namespace volcano::plot
