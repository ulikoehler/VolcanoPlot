// volcano/plot/Plot.hpp — IPlot interface + Figure
#pragma once

#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Cycler.hpp"
#include "volcano/plot/Events.hpp"
#include "volcano/plot/GridSpec.hpp"
#include "volcano/plot/Style.hpp"

#include <vulkan/vulkan.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace volcano::render { class Renderer; class VectorCanvas; }

namespace volcano::render::primitives { class ReduceRenderer; }

namespace volcano::plot {

class Navigation;
class Widget;

/// Interface implemented by all plot types (scatter, line, bar, pie, ...).
class IPlot {
public:
    virtual ~IPlot() = default;
    /// Called once to upload GPU resources (buffers, pipelines).
    virtual void prepare(render::Renderer& renderer) = 0;
    /// Called every frame BEFORE the render pass begins, on a dedicated
    /// pre-pass command buffer submitted ahead of the frame on the same
    /// queue. Use it for compute work that feeds vertex input (e.g. GPU
    /// line tessellation); the results must be consumed by draw() in the
    /// same frame. Default: no pre-pass work.
    virtual void preDraw(vk::CommandBuffer cmd,
                         render::Renderer& renderer,
                         const Axes& axes, Rect2D rect) {
        (void)cmd; (void)renderer; (void)axes; (void)rect;
    }
    /// Called every frame to record draw commands.
    virtual void draw(vk::CommandBuffer cmd, render::Renderer& renderer,
                      const Axes& axes, Rect2D rect) = 0;
    /// Contribute to autoscale (extend the viewport).
    virtual void contributeToAutoscale(Viewport& v) const = 0;
    /// Scale-aware autoscale contribution: called with the axes' scales
    /// so point-series plots can drop out-of-domain data (e.g.
    /// non-positive values on log axes). The default forwards to
    /// `contributeToAutoscale`.
    virtual void contributeToAutoscaleScaled(Viewport& v,
                                             const AxisScale& xscale,
                                             const AxisScale& yscale) const {
        (void)xscale; (void)yscale;
        contributeToAutoscale(v);
    }
    /// GPU-side autoscale contribution. Runs a parallel min/max reduce over
    /// the plot's uploaded GPU buffers when available. The default
    /// implementation falls back to the CPU `contributeToAutoscale`.
    /// Called only after `prepare()` has uploaded GPU resources.
    virtual void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                          Viewport& v) const;
    /// Legend label.
    [[nodiscard]] virtual std::string label() const { return {}; }
    /// Legend marker color.
    [[nodiscard]] virtual Color legendColor() const { return Color::black(); }
    /// Legend marker shape (default: filled square).
    [[nodiscard]] virtual LegendMarker legendMarker() const { return LegendMarker::Square; }
    /// Legend entries. Default: a single handle from label()/legendColor()/
    /// legendMarker() when label() is non-empty; plots with several legend
    /// items (per-series) override to return multiple handles.
    [[nodiscard]] virtual std::vector<LegendHandle> legendEntries() const {
        if (label().empty()) return {};
        return {{label(), legendColor(), legendMarker()}};
    }
    /// mpl "sticky edges": image/quadmesh-style artists disable the 5%
    /// autoscale margin (imshow/pcolormesh/hist2d/hexbin request tight
    /// autoscaling). If any plot returns true, axes margins are skipped.
    [[nodiscard]] virtual bool tightAutoscale() const { return false; }
    /// True for 3D plots (matplotlib projection="3d") — the renderer
    /// suppresses the 2D spine rectangle and axis tick labels since
    /// mplot3d draws its own box/panes instead.
    [[nodiscard]] virtual bool is3D() const { return false; }
    /// Mutable camera for 3D plots — used by interactive 3D rotation
    /// (Navigation drags update every 3D plot's camera on an axes).
    [[nodiscard]] virtual Camera3D* camera3D() noexcept { return nullptr; }
    /// Scalar value range for colormap-mapped plots (the "mappable"'s norm
    /// range in matplotlib — drives the colorbar's tick range). Empty for
    /// plots without a scalar mapping.
    [[nodiscard]] virtual std::optional<Range> valueRange() const { return {}; }
    /// Apply one entry of the axes' property cycler (matplotlib
    /// axes.prop_cycle). Return true when the entry was consumed — the
    /// cycle position only advances for consuming plots. Called by
    /// Axes::addPlot before the plot is stored.
    virtual bool applyCycleProps(const CycleProps&) { return false; }
    /// How much of this artist's data lies inside the data-space box
    /// `xr`×`yr`, as a fraction 0..1. Used to score candidate corners
    /// for legend loc="best" (matplotlib picks the least-overlapping
    /// placement). Default 0 = no occupancy information.
    [[nodiscard]] virtual float occupancy(Range, Range) const {
        return 0.0f;
    }

    /// matplotlib `zorder`: plots are drawn in ascending zorder
    /// (stable — equal zorder keeps insertion order).
    float zorder = 0.0f;
    /// matplotlib `rasterized`: stored for vector backends (PDF/SVG) which
    /// may embed the layer as a bitmap. No effect on raster rendering.
    bool rasterized = false;
    /// matplotlib `animated`: when blitting is active, animated artists
    /// are excluded from the captured background and re-drawn each frame
    /// over the restored snapshot.
    bool animated = false;
    /// Hit-test (matplotlib `contains` / pick): true when the data-space
    /// point hits this layer. Default: never hit.
    virtual bool contains(const Axes&, Point2D) const { return false; }
    /// Owning axes — set by Axes::addPlot. Lets data mutators propagate
    /// the stale flag (matplotlib `artist.axes.stale`).
    void setOwner(Axes* ax) noexcept { owner_ = ax; }
    [[nodiscard]] Axes* owner() const noexcept { return owner_; }
    /// Mark the owning axes (and figure) stale — call from data mutators.
    void touch() noexcept;
    /// Whether this layer can emit native vector primitives
    /// (see emitVector). Used by the exporter to group raster fallback
    /// runs; `rasterized=true` forces fallback even when true.
    [[nodiscard]] virtual bool canEmitVector() const { return false; }
    /// Emit native vector primitives (PDF/SVG/EPS/PGF export) in figure
    /// pixel space, clipped to `rect` (the axes rect). Only called when
    /// canEmitVector() is true and `rasterized` is false. Non-const like
    /// draw(): geometry may be built lazily.
    virtual void emitVector(render::VectorCanvas&, const Axes&, Rect2D) {}

private:
    Axes* owner_ = nullptr;
};

/// A Figure holds one or more Axes arranged in a grid.
enum class PlacementMode {
    Grid,            ///< Positioned via SubplotSpec in a GridSpec
    FigureFraction,  ///< Fraction of the figure rect (matplotlib add_axes([l,b,w,h]))
    Inset,           ///< Fraction of a parent axes' rect (inset_axes)
    Overlay,         ///< Same rect as a parent axes (twinx/twiny)
    Located,         ///< Adjacent to a parent axes (make_axes_locatable)
};

/// Side for located axes / twin ticks.
enum class Side { Left, Right, Bottom, Top };

struct AxesPlacement {
    std::unique_ptr<Axes> axes;
    SubplotSpec spec{};               ///< used when mode == Grid
    PlacementMode mode = PlacementMode::Grid;
    Axes* relTo = nullptr;            ///< parent axes for Inset/Overlay/Located
    // FigureFraction / Inset: fractional rect (of figure / parent rect)
    float fx = 0, fy = 0, fw = 0, fh = 0;
    // Located: which side of the parent, size fraction of parent extent,
    // pad fraction of figure.
    Side side = Side::Right;
    float locatedSize = 0.05f, locatedPad = 0.02f;
};

struct SubfigPlacement {
    std::unique_ptr<Figure> figure;
    SubplotSpec spec;
};

class Figure {
public:
    Figure();
    explicit Figure(uint32_t rows, uint32_t cols);
    explicit Figure(std::shared_ptr<GridSpec> grid);
    ~Figure(); // out-of-line: unique_ptrs to incomplete types

    /// Add an Axes at grid position (row, col), spanning rowSpan×colSpan.
    Axes* addAxes(uint32_t row = 0, uint32_t col = 0,
                  uint32_t rowSpan = 1, uint32_t colSpan = 1);
    /// Add an Axes for a SubplotSpec (from any GridSpec, incl. nested).
    Axes* addAxes(const SubplotSpec& spec);
    /// Add an Axes at a figure-fraction rect [left, bottom, width, height]
    /// (matplotlib fig.add_axes([l, b, w, h])).
    Axes* addAxesFraction(float l, float b, float w, float h);

    /// matplotlib subplot2grid: create/reset the top grid to `shape` and
    /// add an axes at `loc` spanning (rowSpan, colSpan).
    Axes* subplot2grid(std::pair<uint32_t, uint32_t> shape,
                       std::pair<uint32_t, uint32_t> loc,
                       uint32_t rowSpan = 1, uint32_t colSpan = 1);

    /// matplotlib subplot_mosaic: grid of string labels. Each unique label
    /// becomes an axes spanning its cells; "." leaves a cell empty.
    /// Returns label → Axes* in insertion order.
    std::map<std::string, Axes*>
    subplotMosaic(const std::vector<std::vector<std::string>>& layout);

    /// Add a nested figure covering `spec` (matplotlib subfigures).
    Figure* addSubfigure(const SubplotSpec& spec,
                         uint32_t rows = 1, uint32_t cols = 1);
    /// Split the whole figure into rows×cols subfigures.
    std::vector<Figure*> subfigures(uint32_t rows, uint32_t cols);

    /// Overlay axes sharing `parent`'s rect; sharesX links the x viewport
    /// (twinx: shared x, y ticks on right) or the y viewport (twiny).
    Axes* twinx(Axes& parent);
    Axes* twiny(Axes& parent);
    /// Inset axes: fraction rect (x, y, w, h) inside `parent` (inset_axes).
    Axes* insetAxes(Axes& parent, float x, float y, float w, float h);
    /// Axes adjacent to `parent` (make_axes_locatable append_axes).
    /// `size` is a fraction of the parent's extent, `pad` a fraction of the
    /// figure extent.
    Axes* appendAxes(Axes& parent, Side side, float size, float pad);

    /// Layout all axes within the given pixel extent.
    void layout(Extent2D extent);
    /// Layout within an explicit rect (used for subfigures).
    void layoutInRect(Rect2D rect);

    /// matplotlib subplots_adjust: set the top-level grid margins and gaps.
    void subplotsAdjust(float left, float bottom, float right, float top,
                        float wspace, float hspace);
    /// matplotlib tight_layout / constrained_layout toggles.
    void setTightLayout(bool on) { tightLayout_ = on; markStale(); }
    void setConstrainedLayout(bool on) { constrainedLayout_ = on; markStale(); }

    [[nodiscard]] const std::vector<AxesPlacement>& placements() const noexcept { return placements_; }
    [[nodiscard]] const std::vector<SubfigPlacement>& subfigs() const noexcept { return subfigs_; }
    [[nodiscard]] GridSpec& grid() noexcept { return *grid_; }
    [[nodiscard]] const GridSpec& grid() const noexcept { return *grid_; }
    [[nodiscard]] FigureStyle& style() noexcept { return style_; }
    [[nodiscard]] const FigureStyle& style() const noexcept { return style_; }

    void setTitle(std::string t) { style_.title.text = std::move(t); markStale(); }

    /// matplotlib `fig.suptitle` — figure-level centered title.
    void suptitle(std::string t) { style_.title.text = std::move(t); markStale(); }
    /// matplotlib `fig.supxlabel` / `fig.supylabel` — figure-level axis
    /// labels (bottom center / left center, rotated).
    void supxlabel(std::string t) { supXlabel_ = std::move(t); markStale(); }
    void supylabel(std::string t) { supYlabel_ = std::move(t); markStale(); }
    [[nodiscard]] const std::string& supxlabel() const { return supXlabel_; }
    [[nodiscard]] const std::string& supylabel() const { return supYlabel_; }

    /// Merge viewports of axes linked via shareX/shareY. Called by layout().
    void syncSharedAxes();

    // --- Interaction (§11) ---
    /// Flat list of every axes (including subfigures), layout order.
    [[nodiscard]] std::vector<Axes*> allAxes();
    [[nodiscard]] std::vector<const Axes*> allAxes() const;
    /// Event canvas — mpl's `fig.canvas.mpl_connect(...)`.
    [[nodiscard]] EventCanvas& canvas() noexcept { return canvas_; }
    /// Navigation toolbar controller (lazily created).
    [[nodiscard]] Navigation& nav();
    /// True once nav() has created the controller (renderers draw the
    /// zoom rubber-band only when interaction is in use).
    [[nodiscard]] bool navCreated() const noexcept { return nav_ != nullptr; }
    /// Add an interactive widget; the figure owns it.
    template <class T, class... Args>
    T* addWidget(Args&&... args) {
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        auto* p = w.get();
        widgets_.push_back(std::move(w));
        return p;
    }
    [[nodiscard]] std::vector<std::unique_ptr<Widget>>& widgets() noexcept {
        return widgets_;
    }
    /// Pixel rect the figure was last laid out into (transFigure space).
    [[nodiscard]] const Rect2D& figRect() const noexcept { return figRect_; }
    /// Figure DPI (style.dpi) — used by offset_copy / point conversions.
    [[nodiscard]] float dpi() const noexcept { return style_.dpi; }
    /// mpl `fig.transFigure`: figure fraction → display pixels.
    [[nodiscard]] TransformPtr transFigure() const;

    /// mpl `fig.align_xlabels/align_ylabels/align_labels` — persistent:
    /// on every draw the renderer equalizes label depth across each
    /// subplot row/column group (bottom xlabels share their rowspan.stop
    /// row, top labels rowspan.start; left ylabels colspan.start, right
    /// labels colspan.stop). Axes without a SubplotSpec are skipped.
    void alignXlabels() noexcept { alignXLabels_ = true; markStale(); }
    void alignYlabels() noexcept { alignYLabels_ = true; markStale(); }
    void alignLabels() noexcept { alignXlabels(); alignYlabels(); }
    [[nodiscard]] bool alignXLabels() const noexcept { return alignXLabels_; }
    [[nodiscard]] bool alignYLabels() const noexcept { return alignYLabels_; }

    /// matplotlib `fig.legend` — a figure-level legend collecting
    /// handles from all axes, anchored in figure coordinates. `loc`
    /// resolves against the canvas edge; configure via figureLegend().
    void legend(std::string_view loc = "upper right") {
        figLegend_.visible = true;
        figLegend_.location = std::string(loc);
        markStale();
    }
    /// Figure-level legend config (enables the legend on write access,
    /// mirroring Axes::style().legend).
    [[nodiscard]] LegendStyle& figureLegend() noexcept {
        figLegend_.visible = true;
        markStale();
        return figLegend_;
    }
    [[nodiscard]] const LegendStyle& figureLegend() const noexcept {
        return figLegend_;
    }

    /// Hit-test: topmost axes containing canvas pixel (x, y), or nullptr.
    [[nodiscard]] Axes* axesAt(float x, float y);
    /// Dispatch a raw event: fills `inaxes`/`dataPos`, emits on the canvas,
    /// then feeds widgets and the navigation controller.
    void dispatch(Event e);

    // --- Stale tracking (matplotlib `figure.stale`) ---
    /// Mark the figure dirty — the next draw must re-render.
    void markStale() noexcept { stale_ = true; }
    /// True when the figure or any axes was mutated since the last draw.
    [[nodiscard]] bool stale() const;
    /// Clear the stale flag on the figure and every axes (after a draw).
    void setStale(bool v);

private:
    std::shared_ptr<GridSpec> grid_;
    /// Grids replaced by subplotMosaic/subplot2grid/subfigures — kept alive
    /// because existing SubplotSpecs hold raw GridSpec pointers.
    std::vector<std::shared_ptr<GridSpec>> retiredGrids_;
    FigureStyle style_;
    std::string supXlabel_, supYlabel_;
    std::vector<AxesPlacement> placements_;
    std::vector<SubfigPlacement> subfigs_;
    bool stale_ = true;
    bool tightLayout_ = false;
    bool constrainedLayout_ = false;
    bool alignXLabels_ = false, alignYLabels_ = false;
    LegendStyle figLegend_;
    Rect2D figRect_{};

    EventCanvas canvas_;
    std::unique_ptr<Navigation> nav_;
    std::vector<std::unique_ptr<Widget>> widgets_;

    /// Legend drag state (mpl legend.draggable()): the axes whose legend
    /// is being dragged and the pointer position at the last event.
    Axes* legendDragAxes_ = nullptr;
    Point2D legendDragLast_{};
    /// Draggable TextAnnotation/Annotation: pointer to its dragOffset.
    Point2D* artistDrag_ = nullptr;
    Point2D artistDragLast_{};
    /// Legend drag handling; returns true when the event was consumed.
    bool legendDragEvent(const Event& e);
    /// Draggable text/annotation handling; true when event consumed.
    bool artistDragEvent(const Event& e);

    /// Compute effective grid margins for tight/constrained layout.
    void computeTightMargins(Extent2D extent);
    /// Apply aspect-ratio adjustment to axes rects after grid layout.
    void applyAspect();
};

} // namespace volcano::plot
