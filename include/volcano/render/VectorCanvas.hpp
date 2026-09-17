// volcano/render/VectorCanvas.hpp — abstract vector drawing surface
//
// A VectorCanvas records drawing operations in *figure pixel space*
// (Y-down, origin top-left — the same coordinate space the raster
// renderer uses). Concrete implementations emit SVG, PDF, EPS, or PGF.
// Plots emit native vector geometry via `IPlot::emitVector`; plots that
// cannot are rasterized and embedded through `image()`.
#pragma once

#include "volcano/plot/Types.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace volcano::render {

class VectorCanvas {
public:
    /// Stroke style (maps to PDF `w`/`d`, SVG stroke-*, PS setlinewidth).
    struct Pen {
        plot::Color color{0, 0, 0, 1};
        float width = 1.0f;                    ///< px
        std::vector<float> dashes;             ///< on/off lengths, px
        float dashOffset = 0.0f;               ///< px into the pattern
        plot::CapStyle cap = plot::CapStyle::Butt;
        plot::JoinStyle join = plot::JoinStyle::Miter;
    };

    virtual ~VectorCanvas() = default;

    /// Stroke an open polyline.
    virtual void polyline(std::span<const plot::Point2D> pts,
                          const Pen& pen) = 0;
    /// Fill (and optionally stroke) a closed polygon.
    virtual void polygon(std::span<const plot::Point2D> pts,
                         plot::Color fill, const Pen* stroke = nullptr) = 0;
    /// Draw text. `baseline` is the left end of the baseline in pixels;
    /// `sizePx` the font size; `rot` the rotation about the baseline
    /// origin in radians, clockwise-positive in Y-down screen space
    /// (the same convention TextRenderer uses; a y-axis label is -π/2).
    virtual void text(plot::Point2D baseline, std::string_view utf8,
                      float sizePx, plot::Color color,
                      float rot = 0.0f) = 0;
    /// Embed an RGBA8 bitmap stretched over `rect` (figure pixels).
    virtual void image(plot::Rect2D rect, uint32_t w, uint32_t h,
                       std::span<const uint8_t> rgba) = 0;
    /// Clip all subsequent drawing to `rect` until `popClip`.
    virtual void pushClip(plot::Rect2D r) = 0;
    virtual void popClip() = 0;
    /// Write the document to `path`.
    [[nodiscard]] virtual bool finish(const std::filesystem::path& path) = 0;
};

} // namespace volcano::render
