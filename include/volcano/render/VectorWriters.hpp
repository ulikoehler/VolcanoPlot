// volcano/render/VectorWriters.hpp — concrete VectorCanvas writers
//
// Each writer records vector primitives in figure pixel space (Y-down)
// and serializes on `finish()`:
//   - SvgCanvas — inline SVG (paths, text, clipPaths, base64 PNG images)
//   - PdfCanvas — minimal PDF (base-14 Helvetica, FlateDecode when zlib
//     is available, image XObjects with /SMask for alpha)
//   - EpsCanvas — Encapsulated PostScript (Helvetica, hex RGB images
//     alpha-composited onto the figure facecolor)
//   - PgfCanvas — pgf command stream; raster images are written as
//     `<name>-img<N>.png` files next to the output (matplotlib-style)
#pragma once

#include "volcano/render/VectorCanvas.hpp"

#include <map>
#include <memory>
#include <string>

namespace volcano::render {

struct VectorOptions {
    /// Figure facecolor (background); alpha 0 → transparent.
    plot::Color facecolor{1, 1, 1, 1};
    /// Document metadata (PDF /Info, SVG dc:*, EPS comments).
    std::map<std::string, std::string> metadata;
    /// Image DPI hint for embedded rasters.
    float dpi = 100.0f;
};

/// Vector output formats (subset of encode::ImageFormat).
enum class VectorFormat { Svg, Pdf, Eps, Pgf };

/// Create a canvas for the given format. `width`/`height` are the figure
/// size in pixels.
[[nodiscard]] std::unique_ptr<VectorCanvas>
vectorCanvas(VectorFormat fmt, float width, float height,
             const VectorOptions& opts = {});

} // namespace volcano::render
