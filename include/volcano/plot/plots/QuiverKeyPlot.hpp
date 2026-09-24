// volcano/plot/plots/QuiverKeyPlot.hpp — quiver key (matplotlib `quiverkey`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include <string>

namespace volcano::plot {

class QuiverPlot;

/// A quiver reference arrow + label drawn in axes-fraction coordinates
/// (matplotlib `ax.quiverkey(Q, X, Y, U, label)`). The arrow length is
/// `U` rendered with the reference quiver's scale, so it matches the
/// field arrows' visual magnitude.
class QuiverKeyPlot : public IPlot {
public:
    /// mpl labelpos: label above/below/right/left of the arrow.
    enum class LabelPos { N, S, E, W };

    struct Config {
        /// mpl `label`: the key text (e.g. "2 m/s").
        std::string label;
        /// mpl `angle`: arrow direction, degrees counterclockwise from
        /// the horizontal axis.
        float angleDeg = 0.0f;
        /// mpl `labelsep` (inches → ~px at 96 dpi).
        float labelSepPx = 9.6f;
        LabelPos labelPos = LabelPos::N;
        /// mpl `labelcolor` / `color` (transparent → quiver color for the
        /// arrow, text color for the label).
        Color color = Color::transparent();
        Color labelColor = Color::transparent();
        /// mpl `zorder` default: Q.zorder + 0.1.
        float zorderOffset = 0.1f;
    };

    /// (x, y): anchor position in axes fraction (mpl coordinates='axes').
    /// u: the magnitude the key arrow represents.
    /// ref: the QuiverPlot this key refers to (provides scale/width/color).
    /// May be nullptr — then `u` is interpreted as a data-x displacement.
    QuiverKeyPlot(float x, float y, float u, const QuiverPlot* ref,
                  Config cfg);

    [[nodiscard]] Config& config() noexcept { return cfg_; }
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

    void prepare(render::Renderer&) override {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport&) const override {}  // axes coords
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

    /// Arrow geometry in pixel space: anchor, tail→tip, head verts.
    struct Geom {
        Point2D tail, tip;
        float shaftW;
        float headLen, headW;
    };
    [[nodiscard]] Geom geometry(const Axes& axes, Rect2D rect) const;
    /// Pixel-space label anchor + alignment for the config's labelpos.
    struct LabelGeom { Point2D pos; int halign; float baselineAdjust; };
    [[nodiscard]] Point2D anchorPx(Rect2D rect) const;

private:
    float x_, y_, u_;
    const QuiverPlot* ref_;
    Config cfg_;
};

} // namespace volcano::plot
