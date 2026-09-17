// volcano/plot/Specialized.hpp — §15 specialized plot types
#pragma once

#include "volcano/plot/Collections.hpp"
#include "volcano/plot/Plot.hpp"

#include <string>
#include <vector>

namespace volcano::plot {

/// Table — `ax.table(...)`: a grid of text cells drawn along one edge of
/// the axes (matplotlib `table`). Rows × cols cell text, optional row/column
/// header labels and per-cell colors.
class TablePlot : public IPlot {
public:
    /// mpl `loc`: "bottom" (default) or "top" — the table sits inside the
    /// axes along that edge.
    std::string loc = "bottom";
    std::vector<std::vector<std::string>> cellText;   // rows × cols
    std::vector<std::vector<Color>> cellColors;       // optional, rows × cols
    std::vector<std::string> rowLabels, colLabels;
    Color labelColor{0.85f, 0.85f, 0.85f, 1.0f};      // header cell bg
    Color edgeColor{0, 0, 0, 1};
    Color textColor{0, 0, 0, 1};
    float cellFontScale = 0.6f;
    /// Table height as a fraction of the axes height (all rows + header).
    float heightFrac = 0.0f;                          // 0 = auto-fit rows

    void prepare(render::Renderer&) override {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport&) const override {} // axes-frac space
};

/// Sankey — simplified `matplotlib.sankey.Sankey`: a horizontal trunk with
/// ribbons entering on the left (positive flows) and leaving on the right
/// (negative flows). Usage:
///   Sankey sk(*ax);
///   sk.add({1.0, 0.5, -1.5}, {"in1", "in2", "out"});
///   sk.finish();
class Sankey {
public:
    explicit Sankey(Axes& axes) : axes_(&axes) {}
    /// Queue one set of flows. Positive = input (left), negative = output
    /// (right). orientations: +1 = top, -1 = bottom (vertical fan position).
    Sankey& add(std::vector<float> flows,
                std::vector<std::string> labels = {},
                std::vector<int> orientations = {},
                Color color = Color{0.121f, 0.466f, 0.705f, 0.7f});
    /// Emit ribbons + labels into the axes (mpl `finish()`).
    void finish();

private:
    struct FlowSet { std::vector<float> flows; std::vector<std::string> labels;
                     std::vector<int> orientations; Color color; };
    Axes* axes_;
    std::vector<FlowSet> sets_;
    float gap_ = 0.02f;   ///< vertical gap between ribbons (data units)
};

/// Squarified treemap layout (mpl third-party `squarify` equivalent):
/// `sizes` (positive) → rectangles in [0,w]×[0,h] data space, same order.
[[nodiscard]] std::vector<Rect2Df>
squarify(std::span<const float> sizes, float x, float y, float w, float h);

/// `ax.treemap(sizes, labels, colors)` — squarified treemap: colored
/// rectangles + centered labels in axes-fraction space.
void treemap(Axes& axes, std::span<const float> sizes,
             const std::vector<std::string>& labels = {},
             const std::vector<Color>& colors = {});

} // namespace volcano::plot
