// volcano/plot/Specialized.hpp — §15 specialized plot types
#pragma once

#include "volcano/plot/Collections.hpp"
#include "volcano/plot/Plot.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace volcano::plot {

struct TextAnnotation;

/// Table — `ax.table(...)`: a grid of text cells drawn along one edge of
/// the axes (matplotlib `table`). Rows × cols cell text, optional row/column
/// header labels and per-cell colors.
class TablePlot : public IPlot {
public:
    /// mpl `loc`: "bottom" (default) extends below the axes, "top" above,
    /// "center" overlays the table centered inside the axes.
    std::string loc = "bottom";
    std::vector<std::vector<std::string>> cellText;   // rows × cols
    std::vector<std::vector<Color>> cellColors;       // optional, rows × cols
    std::vector<std::string> rowLabels, colLabels;
    Color labelColor{0.85f, 0.85f, 0.85f, 1.0f};      // header cell bg
    Color edgeColor{0, 0, 0, 1};
    Color textColor{0, 0, 0, 1};
    float cellFontScale = 0.85f;
    /// Table height as a fraction of the axes height (all rows + header).
    float heightFrac = 0.0f;                          // 0 = auto-fit rows
    /// mpl Table.scale(xscale, yscale) — per-cell scale factors.
    float scaleX = 1.0f, scaleY = 1.0f;

    void prepare(render::Renderer&) override {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport&) const override {} // axes-frac space
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
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
    /// Emit ribbons + labels into the axes (mpl `finish()`). Returns one
    /// info struct per add()ed subdiagram (mpl's diagrams list).
    struct Diagram {
        std::vector<float> flows;
        /// mpl `angles`: arrow direction in deg/90 (0=RIGHT, 1=UP,
        /// 2=LEFT, 3=DOWN); nullopt for |flow| < tolerance (skipped).
        std::vector<std::optional<int>> angles;
        /// mpl `tips`: outer tip/dip position of each flow path.
        std::vector<Point2D> tips;
        /// Trunk outline patch + per-flow ribbon patches.
        Patch* patch = nullptr;
        std::vector<Patch*> ribbons;
        /// mpl `text`: the patchlabel Text (nullptr when unset).
        TextAnnotation* text = nullptr;
        /// mpl `texts`: one TextAnnotation* per rendered flow label
        /// (nullptr for flows without labels).
        std::vector<TextAnnotation*> texts;
    };
    std::vector<Diagram> finish();

    /// mpl `scale`: multiplies the auto-normalized ribbon width.
    float scale = 1.0f;
    /// mpl `gap`: vertical gap between ribbons (data units).
    float gap = 0.02f;
    /// mpl `tolerance`: flows with |f| < tolerance are skipped.
    float tolerance = 1e-6f;
    /// mpl `patchlabel` per subdiagram (index into sets_).
    std::vector<std::string> patchLabels;

private:
    struct FlowSet { std::vector<float> flows; std::vector<std::string> labels;
                     std::vector<int> orientations; Color color; };
    Axes* axes_;
    std::vector<FlowSet> sets_;
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

/// WordCloud — `wordcloud`-package equivalent: words sized by weight
/// and packed on an Archimedean spiral inside the axes rect. Layout is
/// computed lazily in draw() (needs pixel-space text metrics) and is
/// deterministic for a given `seed`.
class WordCloudPlot : public IPlot {
public:
    struct Entry { std::string text; double weight; };

    std::vector<Entry> words;
    /// Font scale range: largest word gets `maxFontScale`, smallest
    /// `minFontScale` (linear in weight, or log when `logScale`).
    float minFontScale = 0.6f;
    float maxFontScale = 4.0f;
    /// Fraction of words rotated 90° (mpl `prefer_horizontal` inverse).
    float rotationRatio = 0.0f;
    /// Pixel padding around each word's bounding box.
    float margin = 2.0f;
    /// Cap on rendered words (largest by weight win).
    uint32_t maxWords = 200;
    uint32_t seed = 42;
    bool logScale = false;
    /// Per-word colors: rank → colormap. nullptr → viridis.
    const Colormap* cmap = nullptr;

    WordCloudPlot() = default;
    explicit WordCloudPlot(std::vector<Entry> w) : words(std::move(w)) {}

    void prepare(render::Renderer&) override {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport&) const override {}  // axes space

private:
    struct Placed { size_t word; float x, y, w, h; float scale;
                    bool vertical; Color color; };
    std::vector<Placed> placed_;
    bool laidOut_ = false;
    void layout(render::Renderer& r, Rect2D rect);
};

/// NetworkPlot — `networkx.draw` equivalent: edges as line segments,
/// nodes as round markers, optional labels. Layout is computed once in
/// prepare() into [0,1]² data space (autoscale picks up node positions).
class NetworkPlot : public IPlot {
public:
    enum class Layout { Spring, Circular, Random, Given };
    struct Options {
        Layout layout = Layout::Spring;
        int iterations = 60;             ///< spring-layout step count
        uint32_t seed = 42;
        float nodeSize = 14.0f;          ///< marker diameter px
        Color nodeColor{0.121f, 0.466f, 0.705f, 1.0f};
        Color edgeColor{0.5f, 0.5f, 0.5f, 1.0f};
        float edgeWidth = 1.0f;
        std::vector<std::string> labels; ///< per-node, optional
        std::vector<Point2D> positions;  ///< used when layout == Given
        float fontScale = 0.6f;
    };

    NetworkPlot(uint32_t nodeCount,
                std::vector<std::pair<uint32_t, uint32_t>> edges)
        : NetworkPlot(nodeCount, std::move(edges), Options()) {}
    NetworkPlot(uint32_t nodeCount,
                std::vector<std::pair<uint32_t, uint32_t>> edges,
                Options opts);

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return {}; }
    [[nodiscard]] Color legendColor() const override { return opts_.nodeColor; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Circle; }

    /// Node positions in [0,1]² after layout (for tests/inspection).
    [[nodiscard]] const std::vector<Point2D>& positions() const { return pos_; }
    /// Mutable options (layout, colors, …). Mutating re-runs the layout.
    [[nodiscard]] Options& options() noexcept { return opts_; }

private:
    uint32_t n_;
    std::vector<std::pair<uint32_t, uint32_t>> edges_;
    Options opts_;
    std::vector<Point2D> pos_;
    render::primitives::LineSegmentRenderer edgesR_;
    render::primitives::PointRenderer nodesR_;
    bool laidOut_ = false, prepared_ = false;
    void computeLayout();
};

} // namespace volcano::plot
