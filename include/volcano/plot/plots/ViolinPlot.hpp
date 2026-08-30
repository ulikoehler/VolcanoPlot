// volcano/plot/plots/ViolinPlot.hpp — violin plot (matplotlib `violinplot`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for ViolinPlot.
struct ViolinConfig {
    float width = 0.8f;       ///< violin width in data units (x-axis)
    float bandwidth = 0.0f;  ///< KDE bandwidth (0 = auto, Silverman's rule)
    uint32_t numPoints = 128; ///< number of KDE evaluation points per side
    bool showBox = true;      ///< draw inner box (IQR + median)
    bool showPoints = false;  ///< draw individual data points
    Color bodyColor = Color::fromRgba8(31, 119, 180, 128);  ///< violin fill
    Color edgeColor = Color::fromRgba8(31, 119, 180, 255);  ///< violin edge
    Color medianColor = Color::fromRgba8(255, 127, 14, 255); ///< median line
    Color whiskerColor = Color::black();   ///< whisker line color
    float lineWidth = 1.0f;   ///< edge/whisker line width
    float medianWidth = 2.0f; ///< median line width
    std::vector<std::string> labels; ///< per-group labels
    std::string label;       ///< overall legend label
};

/// Violin plot — kernel density estimation mirrored around a center line
/// for each group of data. Equivalent to matplotlib's `violinplot(data)`.
class ViolinPlot : public IPlot {
public:
    /// Construct from a single group of data.
    explicit ViolinPlot(std::vector<float> data, ViolinConfig cfg = {})
        : cfg_(std::move(cfg)) {
        groups_.push_back(std::move(data));
    }

    /// Construct from multiple groups of data.
    ViolinPlot(std::vector<std::vector<float>> groups, ViolinConfig cfg = {})
        : groups_(std::move(groups)), cfg_(std::move(cfg)) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return cfg_.label; }
    [[nodiscard]] Color legendColor() const override { return cfg_.edgeColor; }

private:
    std::vector<std::vector<float>> groups_;
    ViolinConfig cfg_;

    // Computed geometry.
    std::vector<Point2D> bodyFillPos_;
    std::vector<Color> bodyFillColors_;
    std::vector<Point2D> bodyEdgeSegs_;
    std::vector<Point2D> innerSegs_;  // box + whisker + median lines

    render::primitives::FillRenderer fillRenderer_;
    render::primitives::LineSegmentRenderer edgeRenderer_;
    render::primitives::LineSegmentRenderer innerRenderer_;
    bool prepared_ = false;

    /// Compute KDE for one group: returns (y_eval, density) pairs.
    std::pair<std::vector<float>, std::vector<float>>
    computeKde(const std::vector<float>& data) const;

    /// Build geometry for all groups.
    void buildGeometry();

    /// Compute statistics (median, quartiles) for one group.
    struct Stats { float q1, median, q3, min, max; };
    Stats computeStats(const std::vector<float>& data) const;
};

} // namespace volcano::plot
