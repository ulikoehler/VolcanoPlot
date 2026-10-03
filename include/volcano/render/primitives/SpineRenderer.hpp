// volcano/render/primitives/SpineRenderer.hpp — axis spine/border renderer
//
// Backend-neutral interface. The Vulkan implementation lives in
// src/render/primitives/SpineRenderer.cpp (class SpineRendererVk); the
// WebGPU op-recording implementation is src/web/OpSpineRenderer.*.
// Instances come from GpuServices — never constructed directly by plots.
#pragma once

#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

namespace volcano::render::primitives {

/// Draws axis spines (border lines) around the axes rect in pixel space.
/// Implementations use a simple line/triangle pipeline with a
/// pixel→NDC vertex transform.
class SpineRenderer {
public:
    virtual ~SpineRenderer() = default;

    /// Draw a rectangle border around the given pixel rect.
    /// `clip` is the scissor rect; `resolution` is the framebuffer extent
    /// used for the pixel→NDC transform (canvas pixels, not clip-relative).
    virtual void drawRect(Cmd& cmd, plot::Rect2D clip,
                          plot::Extent2D resolution,
                          plot::Rect2D rect, plot::Color color,
                          float lineWidth) = 0;

    /// Draw a filled rectangle (two triangles).
    virtual void drawFilledRect(Cmd& cmd, plot::Rect2D clip,
                                plot::Extent2D resolution,
                                plot::Rect2D rect, plot::Color color) = 0;

    /// Reset the scratch vertex buffer offset. Call at the start of each
    /// frame. Implementations may defer releasing scratch buffers retired
    /// by mid-frame growth until the next reset (see GpuServices::
    /// beginFrameScratch).
    virtual void resetScratch() = 0;

    /// Draw tick marks along an axis.
    /// yAxis=false: x-axis ticks at the bottom edge (down); true: y-axis
    /// ticks at the left edge (left).
    /// inFrac: fraction of tickLength pointing inside the axes
    /// (0 = "out", 1 = "in", 0.5 = "inout").
    /// farSide: x ticks at the top edge / y ticks at the right edge
    /// (matplotlib xaxis.top / yaxis.right).
    /// tickWidth: line width in pixels.
    virtual void drawTicks(Cmd& cmd, plot::Rect2D clip,
                           plot::Extent2D resolution,
                           plot::Rect2D rect,
                           std::span<const float> positions,
                           plot::Color color, float tickLength,
                           bool yAxis, float dataMin, float dataMax,
                           float inFrac = 0.0f, bool farSide = false,
                           float tickWidth = 2.0f) = 0;

    /// Draw a line strip from the given pixel-space points.
    /// Used by reference line plots (AxhLine, AxvLine) that need to draw
    /// lines spanning the axes in pixel coordinates.
    virtual void drawLineStrip(Cmd& cmd, plot::Rect2D clip,
                               plot::Extent2D resolution,
                               std::span<const plot::Point2D> points,
                               plot::Color color, float width) = 0;

    /// Draw a pixel-space triangle soup (non-indexed: 3 vertices per tri).
    /// `clip` is the scissor rect; `resolution` is the framebuffer extent
    /// used for the pixel→NDC transform.
    /// Used by the polyline stroker for wide/dashed lines with joins & caps.
    virtual void drawTriangles(Cmd& cmd, plot::Rect2D clip,
                               plot::Extent2D resolution,
                               std::span<const plot::Point2D> triVerts,
                               plot::Color color) = 0;

    /// Like drawTriangles, but each vertex carries its own color
    /// (per-vertex interpolation — Gouraud-style fills, color meshes).
    virtual void drawTrianglesVC(Cmd& cmd, plot::Rect2D clip,
                                 plot::Extent2D resolution,
                                 std::span<const plot::Point2D> triVerts,
                                 std::span<const plot::Color> colors) = 0;

    /// Draw a device-resident triangle soup whose vertex count lives in
    /// `countBuf` (an indirect draw) — the shape of a compute-produced
    /// mesh. Backends without indirect support do nothing; callers only
    /// take this path after the matching compute capability reported
    /// success (GpuServices::contourTessellate).
    virtual void drawTrianglesGpuIndirect(Cmd& cmd, plot::Rect2D clip,
                                          plot::Extent2D res,
                                          GpuBuf buffer,
                                          uint64_t byteOffset,
                                          GpuBuf countBuf) {
        (void)cmd; (void)clip; (void)res; (void)buffer;
        (void)byteOffset; (void)countBuf;
    }

    /// Draw a GPU-generated LineVertex triangle soup produced by
    /// GpuLineRenderer (compute-stroked polylines). `byteOffset` selects
    /// the first vertex inside `buffer`.
    virtual void drawTrianglesGpu(Cmd& cmd, plot::Rect2D clip,
                                  plot::Extent2D resolution,
                                  GpuBuf buffer, uint64_t byteOffset,
                                  uint32_t vertexCount) = 0;
};

} // namespace volcano::render::primitives
