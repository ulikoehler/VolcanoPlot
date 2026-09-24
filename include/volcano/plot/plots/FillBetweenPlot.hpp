// volcano/plot/plots/FillBetweenPlot.hpp — fill between two curves
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
namespace volcano::plot {

/// Fill between two curves (or a curve and a baseline).
/// Equivalent to matplotlib's `fill_between(x, y1, y2)`.
///
/// The area between y1 (upper) and y2 (lower) is filled with the given color.
/// If y2 is not provided, it defaults to 0 (the x-axis baseline).
/// The x values must be the same for both curves.
class FillBetweenPlot : public IPlot {
public:
    /// Fill between y1 and y2=0 (baseline).
    explicit FillBetweenPlot(std::vector<float> x, std::vector<float> y1,
                             Color color = Color::fromRgba8(31, 119, 180, 128))
        : x_(std::move(x)), y1_(std::move(y1)), y2_(x_.size(), 0.0f),
          color_(color) {}

    /// Fill between y1 and y2.
    FillBetweenPlot(std::vector<float> x, std::vector<float> y1,
                    std::vector<float> y2,
                    Color color = Color::fromRgba8(31, 119, 180, 128))
        : x_(std::move(x)), y1_(std::move(y1)), y2_(std::move(y2)),
          color_(color) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    void setLabel(std::string l) { label_ = std::move(l); }
    /// mpl fill_between `where`: fill between x[i] and x[i+1] only when
    /// where[i] && where[i+1]. `interpolate` (mpl kwarg) extends each
    /// contiguous fill run to the interpolated y1==y2 crossing point.
    /// where must have the same size as x (mpl raises otherwise — here
    /// a mismatched mask is ignored).
    void setWhere(std::vector<bool> where, bool interpolate = false) {
        where_ = std::move(where);
        interpolate_ = interpolate;
        prepared_ = false;   // rebuild the triangle list
        touch();
    }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

private:
    std::vector<float> x_, y1_, y2_;
    /// mpl `where` mask (empty → fill everywhere).
    std::vector<bool> where_;
    bool interpolate_ = false;
    Color color_;
    std::string label_;
    render::primitives::FillRenderer renderer_;
    std::vector<Point2D> uploadedPoints_;  // for GPU autoscale
    bool prepared_ = false;
};

} // namespace volcano::plot
