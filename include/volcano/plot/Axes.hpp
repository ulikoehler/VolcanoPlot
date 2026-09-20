// volcano/plot/Axes.hpp — an Axes (subplot) holding plot layers
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Cycler.hpp"
#include "volcano/plot/Style.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Annotation.hpp"
#include "volcano/plot/Scale.hpp"
#include "volcano/plot/Projection.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Units.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace volcano::render::primitives { class ReduceRenderer; }

namespace volcano::plot {

class IPlot;
struct Patch;
class Figure;

/// Which side of the axes a spine/tick/label set is drawn on.
enum class AxisSide { Bottom, Top, Left, Right };

/// Aspect-ratio mode (matplotlib set_aspect).
enum class AspectMode {
    Auto,   ///< Rect fills its cell.
    Equal,  ///< 1 data unit has equal pixel size on x and y.
};

/// How 'equal' aspect is achieved (matplotlib `adjustable`).
enum class Adjustable {
    Box,     ///< Shrink the axes box to match data aspect (default).
    DataLim, ///< Expand data limits to match the box aspect.
};

/// A secondary axis: tick labels derived from the primary axis through
/// user functions (matplotlib secondary_xaxis/secondary_yaxis).
struct SecondaryAxis {
    std::function<float(float)> forward;  ///< primary -> secondary units
    std::function<float(float)> inverse;  ///< secondary -> primary units
    std::string label;
    bool enabled = false;
};

/// An Axes is one subplot with its own coordinate system, axes, and layers.
class Axes {
public:
    /// Constructs an Axes with the current global rc params (rc::params()).
    Axes();

    /// Set the data viewport (axis limits).
    void setViewport(Viewport v) {
        viewport_ = v; manualX_ = manualY_ = true;
    }
    [[nodiscard]] const Viewport& viewport() const noexcept { return viewport_; }
    [[nodiscard]] Viewport& viewport() noexcept { return viewport_; }
    [[nodiscard]] bool manualViewport() const noexcept {
        return manualX_ && manualY_;
    }
    [[nodiscard]] bool manualX() const noexcept { return manualX_; }
    [[nodiscard]] bool manualY() const noexcept { return manualY_; }

    /// matplotlib set_xlim / set_ylim / xlim / ylim.
    void setXlim(float lo, float hi) { viewport_.x = {lo, hi}; manualX_ = true; }
    void setYlim(float lo, float hi) { viewport_.y = {lo, hi}; manualY_ = true; }
    [[nodiscard]] Range xlim() const { return viewport_.x; }
    [[nodiscard]] Range ylim() const { return viewport_.y; }
    [[nodiscard]] bool xAxisInverted() const { return viewport_.x.min > viewport_.x.max; }
    [[nodiscard]] bool yAxisInverted() const { return viewport_.y.min > viewport_.y.max; }
    void invertXAxis() { std::swap(viewport_.x.min, viewport_.x.max); manualX_ = true; }
    void invertYAxis() { std::swap(viewport_.y.min, viewport_.y.max); manualY_ = true; }

    // --- Scales (matplotlib set_xscale/set_yscale) ---
    void setXscale(AxisScale s) { xScale_ = std::move(s); }
    void setYscale(AxisScale s) { yScale_ = std::move(s); }
    void setXscale(std::string_view name);   ///< "linear","log","symlog","logit","asinh","mercator"
    void setYscale(std::string_view name);
    [[nodiscard]] const AxisScale& xscale() const noexcept { return xScale_; }
    [[nodiscard]] const AxisScale& yscale() const noexcept { return yScale_; }
    [[nodiscard]] AxisScale& xscale() noexcept { return xScale_; }
    [[nodiscard]] AxisScale& yscale() noexcept { return yScale_; }

    /// Back-compat log toggles (equivalent to setXscale("log")).
    void setLogX(bool v) { if (v) xScale_ = AxisScale::log(); else xScale_ = {}; }
    void setLogY(bool v) { if (v) yScale_ = AxisScale::log(); else yScale_ = {}; }
    [[nodiscard]] bool logX() const noexcept { return xScale_.kind == ScaleKind::Log; }
    [[nodiscard]] bool logY() const noexcept { return yScale_.kind == ScaleKind::Log; }

    /// Convenience: log scale on both axes (matplotlib `loglog`).
    void loglog() { setLogX(true); setLogY(true); }
    /// Convenience: log scale on x only (matplotlib `semilogx`).
    void semilogx() { setLogX(true); setLogY(false); }
    /// Convenience: log scale on y only (matplotlib `semilogy`).
    void semilogy() { setLogX(false); setLogY(true); }

    // --- Projection (matplotlib projection="polar" etc.) ---
    void setProjection(Projection p) {
        projection_ = p;
        // matplotlib polar axes are always aspect-equal (adjustable box)
        // and show the theta/r grid by default (rcParam polaraxes.grid).
        if (p.kind == ProjectionKind::Polar) {
            aspect_ = AspectMode::Equal;
            style_.xAxis.grid = true;
            style_.yAxis.grid = true;
        }
    }
    void setProjection(std::string_view name) { setProjection(Projection::parse(name)); }
    [[nodiscard]] const Projection& projection() const noexcept { return projection_; }

    /// Polar helpers (matplotlib set_rgrids/set_thetagrids/...).
    void setRgrids(std::vector<float> radii) { rgrids_ = std::move(radii); }
    void setThetagrids(std::vector<float> degrees) { thetagrids_ = std::move(degrees); }
    void setThetaOffset(float radians) { projection_.thetaOffset = radians; }
    void setThetaDirection(int dir) { projection_.thetaDir = dir < 0 ? -1.0f : 1.0f; }
    void setThetaZeroLocation(std::string_view loc); ///< "N","E","S","W",...
    [[nodiscard]] const std::vector<float>& rgrids() const { return rgrids_; }
    [[nodiscard]] const std::vector<float>& thetagrids() const { return thetagrids_; }

    // --- Aspect ---
    void setAspect(AspectMode m) { aspect_ = m; }
    void setAspectEqual() { aspect_ = AspectMode::Equal; }
    void setAdjustable(Adjustable a) { adjustable_ = a; }
    [[nodiscard]] AspectMode aspect() const noexcept { return aspect_; }
    [[nodiscard]] Adjustable adjustable() const noexcept { return adjustable_; }

    // --- Axis sharing (matplotlib sharex/sharey) ---
    void shareX(Axes& other);
    void shareY(Axes& other);
    [[nodiscard]] const std::vector<Axes*>& sharedX() const { return shareXWith_; }
    [[nodiscard]] const std::vector<Axes*>& sharedY() const { return shareYWith_; }

    // --- Twin / secondary axes ---
    /// matplotlib ax.twinx(): overlay axes sharing x, y ticks on the right.
    Axes* twinx();
    /// matplotlib ax.twiny(): overlay axes sharing y, x ticks on top.
    Axes* twiny();
    /// matplotlib secondary_xaxis/secondary_yaxis.
    void secondaryXaxis(std::function<float(float)> forward,
                        std::function<float(float)> inverse,
                        std::string label = {});
    void secondaryYaxis(std::function<float(float)> forward,
                        std::function<float(float)> inverse,
                        std::string label = {});
    [[nodiscard]] const std::optional<SecondaryAxis>& secondaryX() const { return secondaryX_; }
    [[nodiscard]] const std::optional<SecondaryAxis>& secondaryY() const { return secondaryY_; }

    /// matplotlib inset_axes: fractional rect inside this axes.
    Axes* insetAxes(float x, float y, float w, float h);

    // --- Tick / spine placement ---
    void setXTicksTop(bool top) { xTicksTop_ = top; }
    void setYTicksRight(bool right) { yTicksRight_ = right; }
    /// matplotlib tick_params(top=True, right=True): tick marks on the
    /// far side in addition to the near side; labels stay on the near
    /// side (unlike setXTicksTop/setYTicksRight which move both).
    void setXTickMarksTop(bool on) { xTickMarksTop_ = on; }
    void setYTickMarksRight(bool on) { yTickMarksRight_ = on; }

    /// Per-side spine visibility (matplotlib ax.spines[...].set_visible).
    struct SpineSet { bool left = true, right = true, bottom = true, top = true; };
    /// `side`: "left", "right", "bottom", "top", or "all".
    void setSpineVisible(std::string_view side, bool visible);
    [[nodiscard]] const SpineSet& spines() const noexcept { return spines_; }
    [[nodiscard]] bool xTicksTop() const noexcept { return xTicksTop_; }
    [[nodiscard]] bool yTicksRight() const noexcept { return yTicksRight_; }
    [[nodiscard]] bool xTickMarksTop() const noexcept { return xTickMarksTop_; }
    [[nodiscard]] bool yTickMarksRight() const noexcept { return yTickMarksRight_; }

    // --- Tick locators / formatters (matplotlib axis.set_*_locator etc.) ---
    void setXLocator(std::shared_ptr<Locator> l) {
        style_.xAxis.ticks.locator = std::move(l);
    }
    void setYLocator(std::shared_ptr<Locator> l) {
        style_.yAxis.ticks.locator = std::move(l);
    }
    void setXMinorLocator(std::shared_ptr<Locator> l) {
        style_.xAxis.ticks.minorLocator = std::move(l);
    }
    void setYMinorLocator(std::shared_ptr<Locator> l) {
        style_.yAxis.ticks.minorLocator = std::move(l);
    }
    void setXFormatter(std::shared_ptr<Formatter> f) {
        style_.xAxis.ticks.formatter = std::move(f);
    }
    void setYFormatter(std::shared_ptr<Formatter> f) {
        style_.yAxis.ticks.formatter = std::move(f);
    }
    void setXMinorFormatter(std::shared_ptr<Formatter> f) {
        style_.xAxis.ticks.minorFormatter = std::move(f);
    }
    void setYMinorFormatter(std::shared_ptr<Formatter> f) {
        style_.yAxis.ticks.minorFormatter = std::move(f);
    }

    /// matplotlib ax.minorticks_on/off (both axes).
    void minorticksOn() {
        style_.xAxis.ticks.minor = style_.yAxis.ticks.minor = true;
    }
    void minorticksOff() {
        style_.xAxis.ticks.minor = style_.yAxis.ticks.minor = false;
    }

    /// matplotlib ax.tick_params: direction ("in"/"out"/"inout") and
    /// major/minor sizes & widths in pixels. axis: "x", "y", or "both".
    void tickParams(std::string_view axis = "both",
                    std::string_view direction = "",
                    float majorSize = -1.0f, float minorSize = -1.0f,
                    float majorWidth = -1.0f, float minorWidth = -1.0f);

    /// matplotlib ax.ticklabel_format: axis = "x"/"y"/"both",
    /// style = "plain"/"sci"/"scientific".
    void ticklabelFormat(std::string_view axis = "both",
                         std::string_view style = "",
                         std::pair<int, int> scilimits = {-5, 6},
                         bool useOffset = true, bool useMathText = false);

    /// matplotlib ax.grid: on/off, which = "major"/"minor"/"both",
    /// axis = "x"/"y"/"both".
    void grid(bool on, std::string_view which = "major",
              std::string_view axis = "both");

    void setTitle(std::string t) { style_.title.text = std::move(t); }
    /// matplotlib ax.legend(): enable the legend and return its style for
    /// configuration (`axes.legend().location = "upper left";`).
    LegendStyle& legend() { style_.legend.visible = true; return style_.legend; }
    /// Replaces the style AND reseeds the prop cycler (matplotlib:
    /// style()/rcParams control the cycle for subsequently added plots).
    void setStyle(FigureStyle s);
    void reseedCycler();
    [[nodiscard]] const FigureStyle& style() const noexcept { return style_; }
    [[nodiscard]] FigureStyle& style() noexcept { return style_; }

    /// Add a plot layer (scatter, line, bar, ...). Returns a raw pointer for further configuration.
    /// If the axes' property cycler is non-empty and the plot consumes it
    /// (IPlot::applyCycleProps), the cycle position advances.
    IPlot* addPlot(std::unique_ptr<IPlot> plot);
    /// mpl `ax.add_patch`: append a patch drawn as a PatchCollection layer.
    /// Returns a reference to the stored patch for styling.
    Patch& addPatch(Patch p);
    /// mpl `ax.table`: a grid of text cells along an axes edge.
    /// Returns the created TablePlot for styling.
    class TablePlot& table(std::vector<std::vector<std::string>> cellText,
                           std::string loc = "bottom");

    // ── Reference lines / spans (mpl ax.axhline etc.) ──
    /// mpl `ax.axhline(y)`: horizontal line across the axes.
    class AxhLine& axhline(float y, Color color = Color::black(),
                           float width = 1.0f);
    /// mpl `ax.axvline(x)`: vertical line across the axes.
    class AxvLine& axvline(float x, Color color = Color::black(),
                           float width = 1.0f);
    /// mpl `ax.axhspan(ymin, ymax)`: shaded horizontal band.
    class AxhSpan& axhspan(float y1, float y2,
        Color color = Color::fromRgba8(200, 200, 200, 128));
    /// mpl `ax.axvspan(xmin, xmax)`: shaded vertical band.
    class AxvSpan& axvspan(float x1, float x2,
        Color color = Color::fromRgba8(200, 200, 200, 128));
    /// mpl `ax.hlines(y, xmin, xmax)`: horizontal segments per y.
    class Hlines& hlines(std::vector<float> y, float xMin, float xMax,
                         Color color = Color::black(), float width = 1.0f);
    /// mpl `ax.vlines(x, ymin, ymax)`: vertical segments per x.
    class Vlines& vlines(std::vector<float> x, float yMin, float yMax,
                         Color color = Color::black(), float width = 1.0f);
    /// mpl `ax.eventplot(positions)`: rows of identical event markers.
    class EventPlot& eventplot(std::vector<std::vector<float>> positions);
    class EventPlot& eventplot(std::vector<float> positions);
    /// mpl `ax.imshow(grid)`: 2D image/heatmap display.
    /// `interpolation` mirrors mpl: "nearest" (default), "bilinear",
    /// "bicubic", "antialiased" (GPU-approximated by bilinear).
    /// `aspect` mirrors mpl imshow: "equal" (default — square pixels,
    /// mpl rcParams image.aspect) or "auto" (fill the axes box).
    class HeatmapPlot& imshow(Grid2D grid,
        const Colormap& cmap = colormaps::viridis(),
        std::string_view interpolation = "nearest",
        std::string_view aspect = "equal");

    // ── Specialized (§15) ──
    /// Word cloud: `ax.wordcloud({{"word", weight}, …})` — words packed
    /// on an Archimedean spiral, sized by weight.
    class WordCloudPlot& wordcloud(
        std::vector<std::pair<std::string, double>> words);
    /// Network/graph drawing (`networkx.draw` equivalent):
    /// `ax.network(nodeCount, {{a,b},…}, opts)`.
    class NetworkPlot& network(
        uint32_t nodeCount,
        std::vector<std::pair<uint32_t, uint32_t>> edges);

    // ── Units / categorical & date axes (mpl matplotlib.units) ──

    /// mpl `ax.plot(x, y)` — unit-aware: plain floats pass through,
    /// std::chrono dates convert to day numbers, std::string becomes
    /// categorical positions. The converter's axisInfo is applied to
    /// the axis (locator/formatter defaults).
    class LinePlot& plot(const std::vector<float>& x,
                         const std::vector<float>& y);
    template <class TX, class TY>
    class LinePlot& plot(const std::vector<TX>& xs,
                         const std::vector<TY>& ys) {
        registerBuiltinConverters();
        auto fx = convertSeq(xs, this, 'x');
        auto fy = convertSeq(ys, this, 'y');
        if constexpr (!std::is_convertible_v<TX, float>) {
            if (auto* c = UnitsRegistry::instance().find<TX>())
                applyAxisInfo(*c, 'x');
        }
        if constexpr (!std::is_convertible_v<TY, float>) {
            if (auto* c = UnitsRegistry::instance().find<TY>())
                applyAxisInfo(*c, 'y');
        }
        return plot(fx, fy);
    }
    /// Y-only mpl `ax.plot(y)` form.
    class LinePlot& plot(const std::vector<float>& y) {
        std::vector<float> x(y.size());
        for (size_t i = 0; i < y.size(); ++i) x[i] = float(i);
        return plot(x, y);
    }

    /// mpl `ax.xaxis_date()`: treat x values as days since 1970-01-01
    /// UTC — installs an AutoDateLocator + AutoDateFormatter.
    void xaxis_date();
    /// mpl `ax.yaxis_date()`.
    void yaxis_date();

    /// mpl categorical axis: set the category labels; ticks sit at
    /// positions 0..n-1 labelled with the strings.
    void setXCategories(std::vector<std::string> labels);
    void setYCategories(std::vector<std::string> labels);
    [[nodiscard]] const std::vector<std::string>& xCategories() const {
        return xCategories_;
    }
    [[nodiscard]] const std::vector<std::string>& yCategories() const {
        return yCategories_;
    }
    /// Lookup-or-append a category (mpl unit_data semantics): returns
    /// its index, extending the axis category list on first sight and
    /// refreshing the FixedLocator/FixedFormatter.
    int xCategoryIndex(std::string_view label);
    int yCategoryIndex(std::string_view label);

    /// Set the property cycler (matplotlib axes.prop_cycle). Initialized
    /// from style_.colorCycle (or tab10 when unset).
    void setPropCycle(Cycler c) { cycler_ = std::move(c); }

    /// Legend box rect in figure pixels, tracked by the renderer each
    /// frame for hit-testing (draggable legends). Empty width/height →
    /// no legend currently drawn.
    void setLegendBox(Rect2D r) const { legendBox_ = r; }
    [[nodiscard]] Rect2D legendBox() const { return legendBox_; }
    /// True if (x,y) in figure pixels lies inside the legend box.
    [[nodiscard]] bool legendContains(float x, float y) const {
        return legendBox_.width > 0 && x >= legendBox_.x &&
               x <= legendBox_.x + float(legendBox_.width) &&
               y >= legendBox_.y &&
               y <= legendBox_.y + float(legendBox_.height);
    }
    /// Colorbar region rect in figure pixels (the right `fraction`
    /// slice of the pre-shrink axes rect), computed by Figure layout.
    /// Empty when no colorbar is reserved for this axes.
    void setColorbarRegion(Rect2D r) const { colorbarRegion_ = r; }
    [[nodiscard]] Rect2D colorbarRegion() const { return colorbarRegion_; }

    [[nodiscard]] Cycler& propCycle() noexcept { return cycler_; }
    [[nodiscard]] const Cycler& propCycle() const noexcept { return cycler_; }
    /// Reset the cycler position to the first entry.
    void resetPropCycle() { cycler_.reset(); }

    [[nodiscard]] const std::vector<std::unique_ptr<IPlot>>& plots() const noexcept { return plots_; }
    /// Plots in ascending zorder (stable — matplotlib draw order).
    [[nodiscard]] std::vector<const IPlot*> drawOrder() const;
    /// Picking: layers hit by the data-space point, topmost first
    /// (descending zorder). Uses each plot's `contains` virtual.
    [[nodiscard]] std::vector<const IPlot*> pick(Point2D dataPt) const;

    /// Add a text annotation at (x, y) in the given coordinate system.
    /// Returns a pointer to the annotation for further customization.
    TextAnnotation* text(float x, float y, std::string txt,
                         CoordSystem coords = CoordSystem::Data) {
        TextAnnotation t;
        t.x = x; t.y = y; t.coords = coords;
        t.text = std::move(txt);
        texts_.push_back(std::move(t));
        return &texts_.back();
    }

    /// Add an annotation with an arrow from xyText to xy.
    /// Returns a pointer to the annotation for further customization.
    Annotation* annotate(float xyX, float xyY, float xyTextX, float xyTextY,
                         std::string txt,
                         CoordSystem coords = CoordSystem::Data) {
        Annotation a;
        a.xy[0] = xyX; a.xy[1] = xyY;
        a.xyCoords = coords;
        a.xyText[0] = xyTextX; a.xyText[1] = xyTextY;
        a.xyTextCoords = coords;
        a.text = std::move(txt);
        annotations_.push_back(std::move(a));
        return &annotations_.back();
    }

    [[nodiscard]] const std::vector<TextAnnotation>& texts() const noexcept { return texts_; }
    [[nodiscard]] const std::vector<Annotation>& annotations() const noexcept { return annotations_; }

    /// Add an anchored scale bar (mpl_toolkits.axes_grid1
    /// AnchoredSizeBar). `size` is the bar length in data-x units.
    SizeBar* addSizeBar(SizeBar bar) {
        sizeBars_.push_back(std::move(bar));
        return &sizeBars_.back();
    }
    [[nodiscard]] const std::vector<SizeBar>& sizeBars() const noexcept { return sizeBars_; }

    /// mpl `ax.indicate_inset_zoom(inset_ax)`: mark the data region the
    /// inset zooms into, with connectors to the inset axes box.
    InsetIndicator& indicateInsetZoom(const Axes& inset) {
        insetIndicators_.push_back(InsetIndicator{.inset = &inset});
        return insetIndicators_.back();
    }
    /// mpl `ax.indicate_inset(bounds, inset_ax)`: explicit data-space
    /// rectangle, optional connectors to `inset` (may be nullptr).
    InsetIndicator& indicateInset(float x0, float y0, float x1, float y1,
                                  const Axes* inset = nullptr) {
        InsetIndicator ind;
        ind.inset = inset;
        ind.hasBounds = true;
        ind.x0 = x0; ind.y0 = y0; ind.x1 = x1; ind.y1 = y1;
        insetIndicators_.push_back(ind);
        return insetIndicators_.back();
    }
    [[nodiscard]] const std::vector<InsetIndicator>&
    insetIndicators() const noexcept { return insetIndicators_; }

    /// mpl_toolkits AnchoredText: anchored text box at `loc`.
    AnchoredText& addAnchoredText(std::string text,
                                  std::string loc = "upper left") {
        anchoredTexts_.push_back(
            AnchoredText{std::move(text), std::move(loc)});
        return anchoredTexts_.back();
    }
    [[nodiscard]] const std::vector<AnchoredText>&
    anchoredTexts() const noexcept { return anchoredTexts_; }

    /// Compute the data viewport from all layers if not manually set (CPU).
    void autoscale();

    /// Compute the data viewport using a GPU parallel min/max reduce over
    /// each layer's uploaded buffers. Falls back to CPU per-layer when a
    /// layer has no GPU buffer. No-op if the viewport was set manually.
    /// Must be called after each layer's `prepare()` has uploaded GPU data.
    void autoscaleGpu(render::primitives::ReduceRenderer& reducer);

    /// Transform2D configured for this axes: view in *display* space
    /// (scale + projection applied to the bounds) plus scale/projection
    /// codes for the shaders.
    [[nodiscard]] Transform2D transform() const;

    /// data-space point -> axes-fraction position (0..1), honoring scales
    /// and projection. Used for tick/annotation placement.
    [[nodiscard]] Point2D dataToFraction(Point2D p) const;
    /// Inverse of dataToFraction (scale + viewport; projection inverse is
    /// not applied — returns pre-projection coords).
    [[nodiscard]] Point2D fractionToData(Point2D f) const;
    /// Canvas pixel → axes fraction (Y-down canvas → Y-up fraction).
    [[nodiscard]] Point2D canvasToFraction(Point2D px) const;

    /// mpl `ax.transData`: data coords → display pixels (live-bound).
    [[nodiscard]] TransformPtr transData() const { return plot::transData(*this); }
    /// mpl `ax.transAxes`: axes fraction → display pixels (live-bound).
    [[nodiscard]] TransformPtr transAxes() const { return plot::transAxes(*this); }

    /// The figure this axes belongs to (set by Figure::addAxes).
    void setFigure(Figure* f) { figure_ = f; }
    [[nodiscard]] Figure* figure() const noexcept { return figure_; }

    /// Pixel rect within the figure (set by Figure layout).
    Rect2D rect{};

private:
    /// Apply 5% padding and degenerate-range fixup to a raw min/max viewport.
    void finalizeAutoscale(Viewport& v, bool tight = false) const;
    /// Install a converter's axisInfo defaults on 'x' or 'y'.
    void applyAxisInfo(const UnitConverter& conv, char axis);
    /// Reinstall FixedLocator/FixedFormatter for the category list.
    void installCategoryTicks(char axis);

    Viewport viewport_{0,1,0,1};
    bool manualX_ = false, manualY_ = false;
    AxisScale xScale_, yScale_;
    Projection projection_;
    std::vector<float> rgrids_, thetagrids_;
    AspectMode aspect_ = AspectMode::Auto;
    Adjustable adjustable_ = Adjustable::Box;
    std::vector<Axes*> shareXWith_, shareYWith_;
    std::optional<SecondaryAxis> secondaryX_, secondaryY_;
    bool xTicksTop_ = false, yTicksRight_ = false;
    bool xTickMarksTop_ = false, yTickMarksRight_ = false;
    SpineSet spines_{};
    Figure* figure_ = nullptr;
    FigureStyle style_;
    Cycler cycler_;
    std::vector<std::unique_ptr<IPlot>> plots_;
    std::vector<TextAnnotation> texts_;
    std::vector<Annotation> annotations_;
    std::vector<SizeBar> sizeBars_;
    std::vector<InsetIndicator> insetIndicators_;
    std::vector<AnchoredText> anchoredTexts_;
    /// Category labels per axis (mpl Axis.units unit_data).
    std::vector<std::string> xCategories_, yCategories_;
    /// Legend box rect tracked by the renderer for hit-testing.
    mutable Rect2D legendBox_{};
    mutable Rect2D colorbarRegion_{};
};

} // namespace volcano::plot
