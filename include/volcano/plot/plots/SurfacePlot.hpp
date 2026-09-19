// volcano/plot/plots/SurfacePlot.hpp — 3D surface plot
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/SurfaceRenderer.hpp"
namespace volcano::plot {
class SurfacePlot : public IPlot {
public:
    [[nodiscard]] bool is3D() const override { return true; }
    SurfacePlot(Grid2D grid, Camera3D camera = {})
        : grid_(std::move(grid)), camera_(camera) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    Camera3D& camera() noexcept { return camera_; }
    /// mpl plot_surface `shade` (default True): lambert light shading.
    bool shade = true;
    /// mpl LightSource defaults (degrees).
    float lightAzdeg = 315.0f, lightAltdeg = 45.0f;
private:
    Grid2D grid_;
    Camera3D camera_;
    render::primitives::SurfaceRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
