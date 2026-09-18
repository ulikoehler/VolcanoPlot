// volcano/plot/plots/BarPlot.hpp
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/BarRenderer.hpp"
namespace volcano::plot {
class BarPlot : public IPlot {
public:
    explicit BarPlot(BarData data) : data_(std::move(data)) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;

    /// Picking: hit when the data point is inside a bar.
    bool contains(const Axes&, Point2D pt) const override {
        float n = static_cast<float>(data_.heights.size());
        if (n <= 0) return false;
        float bw = data_.width;
        for (size_t i = 0; i < data_.heights.size(); ++i) {
            float x0 = float(i) - bw * 0.5f;
            float h = data_.heights[i];
            bool inY = h >= 0 ? (pt.y >= 0 && pt.y <= h)
                              : (pt.y >= h && pt.y <= 0);
            if (pt.x >= x0 && pt.x <= x0 + bw && inY) return true;
        }
        return false;
    }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
private:
    BarData data_;
    render::primitives::BarRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
