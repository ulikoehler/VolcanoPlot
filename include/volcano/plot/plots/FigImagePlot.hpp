// volcano/plot/plots/FigImagePlot.hpp — figure-level image overlay
// (matplotlib `figimage`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// FigImage configuration.
struct FigImageConfig {
    /// X position in figure pixels (left edge of image).
    uint32_t x = 0;
    /// Y position in figure pixels (top edge of image, Y-down).
    uint32_t y = 0;
    /// Scale factor for the image (1.0 = original size).
    float scale = 1.0f;
    /// Label for legend (typically empty).
    std::string label;
};

/// Figure-level image overlay — places an RGBA image directly on the figure
/// at pixel coordinates, bypassing axes data coordinates.
/// Equivalent to matplotlib's `figimage(Z)` or `figimage(Z, x, y)`.
///
/// The image is rendered in pixel space (not data space), so it stays at
/// a fixed position regardless of axis limits or zoom. This is useful for
/// watermarks, logos, or raw image overlays.
///
/// Input format: RGBA8 packed as uint32_t (0xAABBGGRR in little-endian),
/// width × height pixels.
class FigImagePlot : public IPlot {
public:
    /// Construct from RGBA8 pixel data.
    /// `pixels`: width*height RGBA8 values packed as uint32_t.
    FigImagePlot(std::vector<uint32_t> pixels, uint32_t width, uint32_t height,
                 FigImageConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return Color::white(); }

private:
    std::vector<uint32_t> pixels_;
    uint32_t width_, height_;
    FigImageConfig config_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void buildGeometry();
};

} // namespace volcano::plot
