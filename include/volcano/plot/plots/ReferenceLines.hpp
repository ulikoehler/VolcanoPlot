// volcano/plot/plots/ReferenceLines.hpp — axhline, axvline, axhspan, axvspan,
// vlines, hlines (matplotlib reference lines and spans)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Large value used for axis-spanning lines/spans. The viewport transform
/// maps data coords to NDC, and the scissor clips anything outside the axes
/// rect. Triangles (spans) clip correctly at any distance, but lines have a
/// GPU guard band limit (~4096+ in NDC on most GPUs). A value of 1e4 keeps
/// NDC within ~2000 for typical viewport spans (1–100), safely within the
/// guard band while spanning well beyond any reasonable axes.
inline constexpr float kAxisSpan = 1e4f;

// ═══════════════════════════════════════════════════════════════════════════
// AxhLine — horizontal line spanning entire axes at a given y
// ═══════════════════════════════════════════════════════════════════════════

/// AxhLine — horizontal line spanning entire axes at a given y.
/// Drawn via SpineRenderer's pixel-space line strip for correct clipping.
class AxhLine : public IPlot {
public:
    AxhLine(float y, Color color = Color::black(), float width = 1.0f)
        : y_(y), color_(color), width_(width) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    float y_;
    Color color_;
    float width_;
    std::string label_;
    bool prepared_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// AxvLine — vertical line spanning entire axes at a given x
// ═══════════════════════════════════════════════════════════════════════════

/// AxvLine — vertical line spanning entire axes at a given x.
/// Drawn via SpineRenderer's pixel-space line strip for correct clipping.
class AxvLine : public IPlot {
public:
    AxvLine(float x, Color color = Color::black(), float width = 1.0f)
        : x_(x), color_(color), width_(width) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    float x_;
    Color color_;
    float width_;
    std::string label_;
    bool prepared_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// AxhSpan — horizontal filled region spanning entire axes between y1 and y2
// ═══════════════════════════════════════════════════════════════════════════

class AxhSpan : public IPlot {
public:
    AxhSpan(float y1, float y2,
            Color color = Color::fromRgba8(200, 200, 200, 128))
        : y1_(y1), y2_(y2), color_(color) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    float y1_, y2_;
    Color color_;
    std::string label_;
    render::primitives::FillRenderer renderer_;
    bool prepared_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// AxvSpan — vertical filled region spanning entire axes between x1 and x2
// ═══════════════════════════════════════════════════════════════════════════

class AxvSpan : public IPlot {
public:
    AxvSpan(float x1, float x2,
            Color color = Color::fromRgba8(200, 200, 200, 128))
        : x1_(x1), x2_(x2), color_(color) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    float x1_, x2_;
    Color color_;
    std::string label_;
    render::primitives::FillRenderer renderer_;
    bool prepared_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// Vlines — collection of vertical line segments
// ═══════════════════════════════════════════════════════════════════════════

class Vlines : public IPlot {
public:
    /// Vertical lines at given x positions, from yMin to yMax for each.
    Vlines(std::vector<float> xPositions, float yMin, float yMax,
           Color color = Color::black(), float width = 1.0f)
        : xPositions_(std::move(xPositions)), yMin_(yMin), yMax_(yMax),
          color_(color), width_(width) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    std::vector<float> xPositions_;
    float yMin_, yMax_;
    Color color_;
    float width_;
    std::string label_;
    render::primitives::LineSegmentRenderer renderer_;
    uint32_t vertexCount_ = 0;
    bool prepared_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// Hlines — collection of horizontal line segments
// ═══════════════════════════════════════════════════════════════════════════

class Hlines : public IPlot {
public:
    /// Horizontal lines at given y positions, from xMin to xMax for each.
    Hlines(std::vector<float> yPositions, float xMin, float xMax,
           Color color = Color::black(), float width = 1.0f)
        : yPositions_(std::move(yPositions)), xMin_(xMin), xMax_(xMax),
          color_(color), width_(width) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    std::vector<float> yPositions_;
    float xMin_, xMax_;
    Color color_;
    float width_;
    std::string label_;
    render::primitives::LineSegmentRenderer renderer_;
    uint32_t vertexCount_ = 0;
    bool prepared_ = false;
};

} // namespace volcano::plot
