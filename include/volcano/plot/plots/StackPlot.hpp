// volcano/plot/plots/StackPlot.hpp — stacked area plot (matplotlib `stackplot`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// mpl stackplot `baseline` modes.
enum class StackBaseline {
    Zero,            ///< Constant zero baseline (default).
    Sym,             ///< Symmetric around zero ("ThemeRiver").
    Wiggle,          ///< Minimizes the sum of squared slopes.
    WeightedWiggle,  ///< Wiggle weighted by layer size ("Streamgraph").
};

/// Stacked area plot. Multiple series are stacked on top of each other,
/// showing the cumulative contribution over time.
/// Equivalent to matplotlib's `stackplot(x, y1, y2, ...)`.
class StackPlot : public IPlot {
public:
    /// Construct from x and a 2D array of y values (one vector per series).
    StackPlot(std::vector<float> x,
              std::vector<std::vector<float>> ys,
              std::vector<Color> colors = {},
              std::vector<std::string> labels = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return overallLabel_; }
    [[nodiscard]] Color legendColor() const override;
    void setOverallLabel(std::string l) { overallLabel_ = std::move(l); }

    /// Labels for each series (for legend).
    [[nodiscard]] const std::vector<std::string>& seriesLabels() const { return labels_; }
    [[nodiscard]] const std::vector<Color>& seriesColors() const { return colors_; }

    /// mpl stackplot `baseline`: "zero" (default), "sym", "wiggle", or
    /// "weighted_wiggle". Unknown names throw std::invalid_argument
    /// (mpl raises ValueError).
    void setBaseline(std::string_view name);
    void setBaseline(StackBaseline b) { baseline_ = b; touch(); }
    [[nodiscard]] StackBaseline baseline() const { return baseline_; }

private:
    std::vector<float> x_;
    std::vector<std::vector<float>> ys_;
    std::vector<Color> colors_;
    std::vector<std::string> labels_;
    std::string overallLabel_;
    StackBaseline baseline_ = StackBaseline::Zero;

    // Cumulative stacked values: stack[i][j] = baseline[j] + sum of
    // ys[0..i][j]; stack[0] is the baseline ("first_line" in mpl).
    mutable std::vector<std::vector<float>> stack_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeStack() const;
    void buildFillTriangles();
    void initDefaultColors();
};

} // namespace volcano::plot
