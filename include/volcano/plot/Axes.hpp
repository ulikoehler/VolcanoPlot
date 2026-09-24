// volcano/plot/Axes.hpp — an Axes (subplot) holding plot layers
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/GridSpec.hpp"
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

/// Parameters for Axes::locatorParams — mirrors matplotlib
/// `ax.locator_params(axis=..., tight=..., nbins=..., integer=...,
/// symmetric=..., prune=..., min_n_ticks=..., steps=[...])`.
struct LocatorParams {
    std::optional<int> nbins;
    /// mpl `steps`: acceptable step mantissas in [1, 10].
    std::optional<std::vector<float>> steps;
    std::optional<bool> integer;
    std::optional<bool> symmetric;
    /// mpl `prune`: "lower"/"upper"/"both"/"none".
    std::optional<std::string> prune;
    std::optional<int> minNTicks;
    /// mpl `tight=True`: autoscale drops the margin.
    bool tight = false;
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
        viewport_ = v; manualX_ = manualY_ = true; touch();
    }
    /// matplotlib `axes.stale`: mark this axes (and its figure) dirty so
    /// the next draw re-renders. Called automatically by mutators.
    void touch() noexcept;
    /// True when a mutator ran since the last draw cleared the flag.
    [[nodiscard]] bool stale() const noexcept { return stale_; }
    void setStale(bool v) noexcept { stale_ = v; }
    [[nodiscard]] const Viewport& viewport() const noexcept { return viewport_; }
    [[nodiscard]] Viewport& viewport() noexcept { return viewport_; }
    [[nodiscard]] bool manualViewport() const noexcept {
        return manualX_ && manualY_;
    }
    [[nodiscard]] bool manualX() const noexcept { return manualX_; }
    [[nodiscard]] bool manualY() const noexcept { return manualY_; }

    /// matplotlib set_xlim / set_ylim / xlim / ylim.
    void setXlim(float lo, float hi) { viewport_.x = {lo, hi}; manualX_ = true; touch(); }
    void setYlim(float lo, float hi) { viewport_.y = {lo, hi}; manualY_ = true; touch(); }
    [[nodiscard]] Range xlim() const { return viewport_.x; }
    [[nodiscard]] Range ylim() const { return viewport_.y; }
    [[nodiscard]] bool xAxisInverted() const { return viewport_.x.min > viewport_.x.max; }
    [[nodiscard]] bool yAxisInverted() const { return viewport_.y.min > viewport_.y.max; }
    void invertXAxis() { std::swap(viewport_.x.min, viewport_.x.max); manualX_ = true; touch(); }
    void invertYAxis() { std::swap(viewport_.y.min, viewport_.y.max); manualY_ = true; touch(); }
    /// mpl Axis.set_inverted / Axes.invert_xaxis — swaps limits only
    /// when the requested state differs from the current one.
    void setXInverted(bool inverted) {
        if (inverted != xAxisInverted()) invertXAxis();
    }
    void setYInverted(bool inverted) {
        if (inverted != yAxisInverted()) invertYAxis();
    }

    /// matplotlib set_xbound/set_ybound: set axis *bounds* — the lower
    /// bound goes to the displayed lower edge regardless of direction,
    /// so an inverted axis keeps its inversion (unlike set_xlim, where
    /// the argument order controls direction).
    void setXbound(float lower, float upper);
    void setYbound(float lower, float upper);
    /// mpl single-sided forms: set_xbound(lower=v)/set_xbound(upper=v).
    void setXbound(std::optional<float> lower, std::optional<float> upper);
    void setYbound(std::optional<float> lower, std::optional<float> upper);

    /// matplotlib ax.margins(x, y=None): autoscale padding fraction
    /// (default 0.05). margins(x) sets both axes; y >= 0 overrides y only.
    void margins(float x, float y = -1.0f) {
        marginX_ = x;
        marginY_ = (y < 0.0f ? x : y);
        touch();
    }
    [[nodiscard]] float marginX() const noexcept { return marginX_; }
    [[nodiscard]] float marginY() const noexcept { return marginY_; }
    /// mpl Axes.set_xmargin/set_ymargin — set one margin only.
    void setXMargin(float m) { marginX_ = m; }
    void setYMargin(float m) { marginY_ = m; }

    // --- Scales (matplotlib set_xscale/set_yscale) ---
    void setXscale(AxisScale s) { xScale_ = std::move(s); touch(); }
    void setYscale(AxisScale s) { yScale_ = std::move(s); touch(); }
    void setXscale(std::string_view name);   ///< "linear","log","symlog","logit","asinh","mercator"
    void setYscale(std::string_view name);
    [[nodiscard]] const AxisScale& xscale() const noexcept { return xScale_; }
    [[nodiscard]] const AxisScale& yscale() const noexcept { return yScale_; }
    [[nodiscard]] AxisScale& xscale() noexcept { return xScale_; }
    [[nodiscard]] AxisScale& yscale() noexcept { return yScale_; }

    /// Back-compat log toggles (equivalent to setXscale("log")).
    void setLogX(bool v) { if (v) xScale_ = AxisScale::log(); else xScale_ = {}; touch(); }
    void setLogY(bool v) { if (v) yScale_ = AxisScale::log(); else yScale_ = {}; touch(); }
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
        touch();
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
    void setRgrids(std::vector<float> radii) { rgrids_ = std::move(radii); touch(); }
    void setThetagrids(std::vector<float> degrees) { thetagrids_ = std::move(degrees); touch(); }
    void setThetaOffset(float radians) { projection_.thetaOffset = radians; touch(); }
    void setThetaDirection(int dir) { projection_.thetaDir = dir < 0 ? -1.0f : 1.0f; touch(); }
    void setThetaZeroLocation(std::string_view loc); ///< "N","E","S","W",...
    /// mpl `set_rlabel_position`: degrees — the radial direction along
    /// which r tick labels are drawn (default 22.5°).
    void setRlabelPosition(float deg) { rlabelPosition_ = deg; touch(); }
    [[nodiscard]] float rlabelPosition() const { return rlabelPosition_; }

    /// mpl GeoAxes.set_longitude_grid_ends: latitude cap (degrees) at
    /// which meridian grid lines stop (default 75°).
    void setLongitudeGridEnds(float deg) { geoGridEndsDeg_ = deg; touch(); }
    [[nodiscard]] float longitudeGridEnds() const { return geoGridEndsDeg_; }
    /// mpl polar `set_rmin`/`set_rmax`/`set_rorigin`: r is the y axis.
    void setRmin(float v) { auto yl = ylim(); setYlim(v, yl.max); }
    void setRmax(float v) { auto yl = ylim(); setYlim(yl.min, v); }
    void setRorigin(float v) { setRmin(v); }
    [[nodiscard]] const std::vector<float>& rgrids() const { return rgrids_; }
    [[nodiscard]] const std::vector<float>& thetagrids() const { return thetagrids_; }

    // --- Aspect ---
    void setAspect(AspectMode m) { aspect_ = m; touch(); }
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
    /// mpl tick_params / Axis.set_ticks_position: per-side tick
    /// furniture. `near` = bottom (x) / left (y), `far` = top / right.
    struct AxisFurniture {
        bool marksNear = true, marksFar = false;
        bool labelsNear = true, labelsFar = false;
        /// mpl Axis.set_label_position — the axis label sits on the far
        /// side ('top'/'right') instead of the near side.
        bool labelFar = false;
    };
    /// mpl `ax.xaxis.set_ticks_position`: 'top'/'bottom' move marks and
    /// labels to one side; 'both' adds marks on both sides (labels
    /// unchanged); 'none' clears marks (labels unchanged); 'default'
    /// resets to mpl defaults.
    void setXTicksPosition(std::string_view pos);
    void setYTicksPosition(std::string_view pos);
    /// mpl `ax.xaxis.set_label_position('top'|'bottom')`.
    void setXLabelPosition(std::string_view pos);
    void setYLabelPosition(std::string_view pos);
    /// mpl `ax.xaxis.tick_top()/tick_bottom()`, `ax.yaxis.tick_left()/
    /// tick_right()` — move marks and labels to that side, preserving a
    /// fully-disabled label state (mpl keeps labels off if they were off
    /// on both sides before the call).
    void xaxisTickTop();
    void xaxisTickBottom();
    void yaxisTickLeft();
    void yaxisTickRight();
    /// mpl `ax.xaxis.get_ticks_position()`: 'bottom'/'top'/'default'/
    /// 'unknown' ('default' = marks on both sides, labels near only).
    [[nodiscard]] std::string_view xTicksPosition() const;
    [[nodiscard]] std::string_view yTicksPosition() const;
    /// mpl `ax.xaxis.get_label_position()`: 'bottom'/'top' (x) or
    /// 'left'/'right' (y).
    [[nodiscard]] std::string_view xLabelPosition() const noexcept {
        return xFurn_.labelFar ? "top" : "bottom";
    }
    [[nodiscard]] std::string_view yLabelPosition() const noexcept {
        return yFurn_.labelFar ? "right" : "left";
    }
    [[nodiscard]] const AxisFurniture& xFurniture() const noexcept {
        return xFurn_;
    }
    [[nodiscard]] const AxisFurniture& yFurniture() const noexcept {
        return yFurn_;
    }
    [[nodiscard]] AxisFurniture& xFurniture() noexcept { return xFurn_; }
    [[nodiscard]] AxisFurniture& yFurniture() noexcept { return yFurn_; }
    /// Legacy shims (secondary axes, tick_params bindings). Secondary
    /// axes also move the axis label (mpl _SecondaryAxis).
    void setXTicksTop(bool top) {
        xFurn_.marksNear = !top; xFurn_.marksFar = top;
        xFurn_.labelsNear = !top; xFurn_.labelsFar = top;
        xFurn_.labelFar = top;
    }
    void setYTicksRight(bool right) {
        yFurn_.marksNear = !right; yFurn_.marksFar = right;
        yFurn_.labelsNear = !right; yFurn_.labelsFar = right;
        yFurn_.labelFar = right;
    }
    /// matplotlib tick_params(top=True, right=True): tick marks on the
    /// far side in addition to the near side; labels stay on the near
    /// side (unlike setXTicksTop/setYTicksRight which move both).
    void setXTickMarksTop(bool on) { xFurn_.marksFar = on; }
    void setYTickMarksRight(bool on) { yFurn_.marksFar = on; }

    /// Per-side spine state (matplotlib spines.Spine).
    struct SpineSpec {
        bool visible = true;
        /// mpl set_position mode: how the spine's cross-axis location is
        /// resolved. Outward moves the spine away from the axes by
        /// `amount` points; Axes places it at an axes fraction (0 = the
        /// spine's default side); Data places it at a data coordinate.
        enum class PosMode { Outward, Axes, Data };
        PosMode posMode = PosMode::Outward;
        float posAmount = 0.0f;
        bool positionSet = false;   ///< mpl set_position was called
        /// mpl set_bounds: restrict the spine's along-axis extent to a
        /// data-coordinate range (e.g. bottom spine spanning x∈[a,b]).
        std::optional<std::pair<float, float>> bounds;
        /// mpl set_color (nullopt → axes.edgecolor).
        std::optional<Color> color;
        /// mpl set_linewidth (nullopt → axes.linewidth).
        std::optional<float> lineWidth;
        /// mpl set_linestyle / set_dashes.
        std::optional<LineStyle> lineStyle;
        std::vector<float> dashes;
        /// mpl set_patch_arc — the spine draws as an arc segment
        /// instead of a straight line. `center` is in axes-fraction
        /// units, `radius` is mpl's patch-transform radius (the arc is
        /// scaled by radius*0.5 before being placed at `center`), and
        /// `theta1`/`theta2` are degrees CCW from +x (y-up).
        struct ArcSpec {
            float cx = 0.5f, cy = 0.5f;
            float radius = 1.0f;
            float theta1 = 0.0f, theta2 = 360.0f;
        };
        std::optional<ArcSpec> arc;
    };
    struct SpineSet {
        SpineSpec left, right, bottom, top;
        /// "left"/"right"/"bottom"/"top" → the spec; throws otherwise.
        [[nodiscard]] SpineSpec& side(std::string_view s);
        [[nodiscard]] const SpineSpec& side(std::string_view s) const;
    };
    /// `side`: "left", "right", "bottom", "top", or "all".
    void setSpineVisible(std::string_view side, bool visible);
    /// Mutable access to a single spine (mpl ax.spines[side]).
    [[nodiscard]] SpineSpec& spine(std::string_view side) {
        return spines_.side(side);
    }
    [[nodiscard]] const SpineSet& spines() const noexcept { return spines_; }
    /// Resolved pixel-space geometry of one spine: `pos` is the
    /// cross-axis pixel coordinate (y for bottom/top, x for left/right)
    /// and `from`/`to` are the along-axis pixel endpoints (set_bounds
    /// limited). Renderers use this for the spine line and its ticks.
    struct SpineLineGeom { float pos, from, to; };
    [[nodiscard]] SpineLineGeom spineLine(std::string_view side,
                                          Rect2D rect) const;
    /// True when labels sit exclusively on the far side (secondary
    /// axes / set_ticks_position('top'|'right')).
    [[nodiscard]] bool xTicksTop() const noexcept {
        return xFurn_.labelsFar && !xFurn_.labelsNear;
    }
    [[nodiscard]] bool yTicksRight() const noexcept {
        return yFurn_.labelsFar && !yFurn_.labelsNear;
    }
    [[nodiscard]] bool xTickMarksTop() const noexcept {
        return xFurn_.marksFar;
    }
    [[nodiscard]] bool yTickMarksRight() const noexcept {
        return yFurn_.marksFar;
    }

    // --- Axes visibility / frame (mpl axison, frame_on, axis()) ---
    /// mpl `axison`: when false, the axes patch, spines, ticks, grid
    /// lines, and tick/axis labels are not drawn; the title and plot
    /// artists remain visible (mpl set_axis_off).
    void setAxisOn(bool on) { axisOn_ = on; touch(); }
    void setAxisOff() { setAxisOn(false); }
    [[nodiscard]] bool axison() const noexcept { return axisOn_; }
    /// mpl `set_frame_on`: when false, the axes patch and spine frame
    /// are hidden while ticks and labels remain drawn.
    void setFrameOn(bool on) { frameOn_ = on; touch(); }
    [[nodiscard]] bool frameOn() const noexcept { return frameOn_; }
    /// mpl `ax.axis(arg)`: "off"/"on" toggle axis furniture;
    /// "equal"/"square" select equal aspect, "auto" resets it,
    /// "scaled"/"image" select equal aspect + adjustable="datalim",
    /// "tight" zeroes the margins. Returns false for unknown strings.
    bool axis(std::string_view option);
    /// mpl `ax.axis([xmin, xmax, ymin, ymax])`.
    void axis(float xmin, float xmax, float ymin, float ymax) {
        setXlim(xmin, xmax);
        setYlim(ymin, ymax);
    }

    // --- mpl Artist-level state on Axes ---
    /// mpl `Axes.set_visible`: hides the entire axes — patch, spines,
    /// ticks, labels, and all artists (unlike set_axis_off which keeps
    /// artists).
    void setVisible(bool v) { visible_ = v; touch(); }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    /// mpl zorder (default 0): axes are drawn in ascending zorder.
    void setZorder(float z) { zorder_ = z; touch(); }
    [[nodiscard]] float zorder() const noexcept { return zorder_; }
    /// mpl Axes.set_alpha → axes patch alpha (None = style default).
    void setAlpha(std::optional<float> a) { patchAlpha_ = a; touch(); }
    [[nodiscard]] std::optional<float> alpha() const noexcept { return patchAlpha_; }
    void setGid(std::string g) { gid_ = std::move(g); }
    [[nodiscard]] const std::string& gid() const noexcept { return gid_; }
    void setUrl(std::string u) { url_ = std::move(u); }
    [[nodiscard]] const std::string& url() const noexcept { return url_; }
    /// mpl Artist label (distinct from axis labels).
    void setLabel(std::string l) { label_ = std::move(l); }
    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    void setSnap(std::optional<bool> s) { snap_ = s; }
    [[nodiscard]] std::optional<bool> snap() const noexcept { return snap_; }
    void setRasterized(bool r) { rasterized_ = r; }
    [[nodiscard]] bool rasterized() const noexcept { return rasterized_; }
    void setAnimated(bool a) { animated_ = a; }
    [[nodiscard]] bool animated() const noexcept { return animated_; }
    void setMouseover(bool m) { mouseover_ = m; }
    [[nodiscard]] bool mouseover() const noexcept { return mouseover_; }
    /// mpl `set_picker`: bool enables, float enables + sets the radius.
    void setPicker(std::optional<float> p) { picker_ = p; }
    [[nodiscard]] std::optional<float> picker() const noexcept { return picker_; }
    [[nodiscard]] bool pickable() const noexcept { return picker_.has_value(); }
    void setPickradius(float r) { pickRadius_ = r; }
    [[nodiscard]] float pickradius() const noexcept { return pickRadius_; }

    // --- mpl navigation / layout bookkeeping ---
    /// mpl `set_navigate` (default True).
    void setNavigate(bool b) { navigate_ = b; }
    [[nodiscard]] bool navigate() const noexcept { return navigate_; }
    /// mpl `set_navigate_mode`: "PAN", "ZOOM", or "" (None).
    void setNavigateMode(std::string m) { navigateMode_ = std::move(m); }
    [[nodiscard]] const std::string& navigateMode() const noexcept {
        return navigateMode_;
    }

    /// mpl Axes.start_pan/drag_pan/end_pan — interactive pan & zoom.
    /// Coordinates are display pixels (Y-down); `button` follows mpl:
    /// 1 = pan, 3 = zoom-to-point. `key` may be "x"/"y"/"control"/"shift".
    void startPan(float x, float y);
    void dragPan(int button, const std::string& key, float x, float y);
    void endPan();
    /// mpl `set_in_layout` (default True): excluded axes are skipped by
    /// tight/constrained layout.
    void setInLayout(bool b) { inLayout_ = b; }
    [[nodiscard]] bool inLayout() const noexcept { return inLayout_; }

    /// mpl `Axes.contains_point`: display-pixel point inside the axes
    /// rect. `contains`/`in_axes` add the pickradius tolerance.
    [[nodiscard]] bool containsPoint(Point2D displayPx) const noexcept;
    [[nodiscard]] bool contains(Point2D displayPx) const noexcept;

    // --- mpl box geometry (set_aspect companions) ---
    /// mpl `set_box_aspect`: force the axes box w/h ratio; switches
    /// `adjustable` to 'datalim' (clearing keeps 'datalim', like mpl).
    void setBoxAspect(std::optional<float> aspect) {
        boxAspect_ = aspect;
        if (aspect) adjustable_ = Adjustable::DataLim;
        touch();
    }
    [[nodiscard]] std::optional<float> boxAspect() const noexcept {
        return boxAspect_;
    }
    /// mpl `set_anchor`: placement of the axes box inside its allocated
    /// cell when it is shrunk by adjustable='box' (anchor is the
    /// container-fraction point the box sticks to; 'C' = center).
    void setAnchor(float x, float y, std::string name = "") {
        anchorX_ = x; anchorY_ = y; anchorName_ = std::move(name);
        touch();
    }
    [[nodiscard]] float anchorX() const noexcept { return anchorX_; }
    [[nodiscard]] float anchorY() const noexcept { return anchorY_; }
    [[nodiscard]] const std::string& anchorName() const noexcept {
        return anchorName_;
    }
    /// mpl `apply_aspect`: apply the current aspect/box_aspect to the
    /// axes rect (adjustable='box') or the view limits ('datalim').
    void applyAspect();
    /// mpl `get_data_ratio`: abs(y-span)/abs(x-span) of the view limits
    /// in data units.
    [[nodiscard]] float dataRatio() const noexcept;
    /// mpl `get_data_ratio_log`: same in log10 space.
    [[nodiscard]] float dataRatioLog() const noexcept;

    /// mpl `ax.update_datalim(xys, updatex=True, updatey=True)`:
    /// merge data-space points into the data limits.
    void updateDataLim(std::span<const Point2D> pts, bool updatex = true,
                       bool updatey = true);
    /// mpl `ignore_existing_data_limits` (default False): when true,
    /// the next update_datalim starts from an empty bbox.
    bool ignoreExistingDataLimits = false;
    /// mpl `ax.sticky_edges` — axes-level sticky edges merged with the
    /// per-artist ones at autoscale.
    StickyEdges stickyEdges;

    // --- Grid position / shared-axis label suppression (label_outer) ---
    /// The SubplotSpec this axes occupies in its figure's grid (set by
    /// Figure::addAxes; empty for insets, twins, and fractional axes).
    void setSubplotSpec(const SubplotSpec& s) { subplotSpec_ = s; }
    /// mpl set_position: drops the grid placement (free axes).
    void clearSubplotSpec() { subplotSpec_.reset(); }
    [[nodiscard]] const std::optional<SubplotSpec>& subplotSpec() const {
        return subplotSpec_;
    }
    /// True when this axes sits on the last row of its grid. For
    /// non-grid placements, true when no sibling axes rect extends
    /// below this one while overlapping its x extent.
    [[nodiscard]] bool isLastRow() const;
    /// True when this axes sits in the first column of its grid.
    [[nodiscard]] bool isFirstCol() const;
    /// mpl ax.label_outer(): hide x tick labels unless this axes is on
    /// the last row and y tick labels unless it is in the first column
    /// (used with shared axes). `removeInnerTicks` (mpl kwarg) also
    /// hides the inner tick *marks*; the default keeps them.
    void labelOuter(bool removeInnerTicks = false) {
        labelOuter_ = true;
        removeInnerTicks_ = removeInnerTicks;
        touch();
    }
    [[nodiscard]] bool labelOuterActive() const noexcept {
        return labelOuter_;
    }
    /// Whether label_outer currently suppresses the x/y tick labels.
    [[nodiscard]] bool xTickLabelsHidden() const {
        return labelOuter_ && !isLastRow();
    }
    [[nodiscard]] bool yTickLabelsHidden() const {
        return labelOuter_ && !isFirstCol();
    }
    /// mpl label_outer(remove_inner_ticks=True): also drop inner marks.
    [[nodiscard]] bool innerTicksRemoved() const noexcept {
        return labelOuter_ && removeInnerTicks_;
    }

    // --- Tick locators / formatters (matplotlib axis.set_*_locator etc.) ---
    void setXLocator(std::shared_ptr<Locator> l) {
        style_.xAxis.ticks.locator = std::move(l);
        defaultTick_[0] &= ~kMajLoc;
    }
    void setYLocator(std::shared_ptr<Locator> l) {
        style_.yAxis.ticks.locator = std::move(l);
        defaultTick_[1] &= ~kMajLoc;
    }
    void setXMinorLocator(std::shared_ptr<Locator> l) {
        style_.xAxis.ticks.minorLocator = std::move(l);
        defaultTick_[0] &= ~kMinLoc;
    }
    void setYMinorLocator(std::shared_ptr<Locator> l) {
        style_.yAxis.ticks.minorLocator = std::move(l);
        defaultTick_[1] &= ~kMinLoc;
    }
    void setXFormatter(std::shared_ptr<Formatter> f) {
        style_.xAxis.ticks.formatter = std::move(f);
        defaultTick_[0] &= ~kMajFmt;
    }
    void setYFormatter(std::shared_ptr<Formatter> f) {
        style_.yAxis.ticks.formatter = std::move(f);
        defaultTick_[1] &= ~kMajFmt;
    }
    void setXMinorFormatter(std::shared_ptr<Formatter> f) {
        style_.xAxis.ticks.minorFormatter = std::move(f);
        defaultTick_[0] &= ~kMinFmt;
    }
    void setYMinorFormatter(std::shared_ptr<Formatter> f) {
        style_.yAxis.ticks.minorFormatter = std::move(f);
        defaultTick_[1] &= ~kMinFmt;
    }

    /// mpl Axis.get_major_locator etc. — nullptr = automatic default.
    [[nodiscard]] const std::shared_ptr<Locator>&
    xLocator() const { return style_.xAxis.ticks.locator; }
    [[nodiscard]] const std::shared_ptr<Locator>&
    yLocator() const { return style_.yAxis.ticks.locator; }
    [[nodiscard]] const std::shared_ptr<Locator>&
    xMinorLocator() const { return style_.xAxis.ticks.minorLocator; }
    [[nodiscard]] const std::shared_ptr<Locator>&
    yMinorLocator() const { return style_.yAxis.ticks.minorLocator; }
    [[nodiscard]] const std::shared_ptr<Formatter>&
    xFormatter() const { return style_.xAxis.ticks.formatter; }
    [[nodiscard]] const std::shared_ptr<Formatter>&
    yFormatter() const { return style_.yAxis.ticks.formatter; }
    [[nodiscard]] const std::shared_ptr<Formatter>&
    xMinorFormatter() const { return style_.xAxis.ticks.minorFormatter; }
    [[nodiscard]] const std::shared_ptr<Formatter>&
    yMinorFormatter() const { return style_.yAxis.ticks.minorFormatter; }

    /// mpl Axis.isDefault_majloc etc. — true until the user installs an
    /// explicit locator/formatter (including via set_ticks/set_ticklabels
    /// or minorticks_on/off).
    [[nodiscard]] bool isDefaultMajLoc(bool x) const {
        return defaultTick_[x ? 0 : 1] & kMajLoc;
    }
    [[nodiscard]] bool isDefaultMinLoc(bool x) const {
        return defaultTick_[x ? 0 : 1] & kMinLoc;
    }
    [[nodiscard]] bool isDefaultMajFmt(bool x) const {
        return defaultTick_[x ? 0 : 1] & kMajFmt;
    }
    [[nodiscard]] bool isDefaultMinFmt(bool x) const {
        return defaultTick_[x ? 0 : 1] & kMinFmt;
    }
    /// Mark the axis' major locator/formatter as user-set (set_ticks /
    /// set_ticklabels write TickConfig directly).
    void markMajorTicksSet(bool x) {
        defaultTick_[x ? 0 : 1] &= ~(kMajLoc | kMajFmt);
    }

    /// mpl Axis.minorticks_on/off for one axis. On a linear axis `on`
    /// enables AutoMinorLocator subdivisions; `off` installs a
    /// NullLocator so even log-family axes lose their minor ticks.
    void minorticksOnAxis(bool x) {
        auto& tc = x ? style_.xAxis.ticks : style_.yAxis.ticks;
        tc.minor = true;
        if (dynamic_cast<NullLocator*>(tc.minorLocator.get()))
            tc.minorLocator.reset();
    }
    void minorticksOffAxis(bool x) {
        auto& tc = x ? style_.xAxis.ticks : style_.yAxis.ticks;
        tc.minor = false;
        tc.minorLocator = std::make_shared<NullLocator>();
        defaultTick_[x ? 0 : 1] &= ~kMinLoc;
    }

    /// matplotlib ax.locator_params(axis='both', ...): forward
    /// parameters to the axis' tick locator. When no locator is set (or
    /// the locator is a MaxNLocator — mpl's default AutoLocator), a
    /// MaxNLocator is installed/configured; other locator types keep
    /// their settings (matplotlib ignores unsupported kwargs there).
    void locatorParams(std::string_view axis, const LocatorParams& p);
    /// Convenience: locatorParams(axis, {.nbins = n}).
    void locatorParams(std::string_view axis, int nbins) {
        LocatorParams p;
        p.nbins = nbins;
        locatorParams(axis, p);
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

    void setTitle(std::string t) { style_.title.text = std::move(t); touch(); }
    /// matplotlib ax.legend(): enable the legend and return its style for
    /// configuration (`axes.legend().location = "upper left";`).
    LegendStyle& legend() { style_.legend.visible = true; touch(); return style_.legend; }
    /// Replaces the style AND reseeds the prop cycler (matplotlib:
    /// style()/rcParams control the cycle for subsequently added plots).
    void setStyle(FigureStyle s);
    void reseedCycler();
    [[nodiscard]] const FigureStyle& style() const noexcept { return style_; }
    [[nodiscard]] FigureStyle& style() noexcept { return style_; }

    /// mpl `ax.set_sketch_params(scale, length, randomness)`: xkcd-style
    /// wobble for this axes' stroked artists. `scale` ≤ 0 or unset
    /// disables the sketch effect (mpl default). `length`/`randomness`
    /// are accepted for API parity (the wobble uses `scale` only).
    void setSketchParams(std::optional<float> scale = std::nullopt,
                         std::optional<float> length = std::nullopt,
                         std::optional<float> randomness = std::nullopt) {
        // mpl: scale=None disables; else length/randomness default to
        // 128.0/16.0 when unset or 0.
        style_.sketchScale = scale.value_or(0.0f);
        if (scale) {
            style_.sketchLength =
                (length && *length != 0.0f) ? *length : 128.0f;
            style_.sketchRandomness =
                (randomness && *randomness != 0.0f) ? *randomness : 16.0f;
        }
        touch();
    }

    /// Add a plot layer (scatter, line, bar, ...). Returns a raw pointer for further configuration.
    /// If the axes' property cycler is non-empty and the plot consumes it
    /// (IPlot::applyCycleProps), the cycle position advances.
    /// Takes shared ownership so artists detached via removePlot can be
    /// re-attached (unique_ptr converts implicitly at the call site).
    IPlot* addPlot(std::shared_ptr<IPlot> plot);
    /// mpl `Artist.remove()`: detach the plot layer from the axes and
    /// return ownership (nullptr when `p` isn't a child). The returned
    /// artist can be re-added via addPlot.
    std::shared_ptr<IPlot> removePlot(const IPlot* p);
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
    /// mpl `ax.axline(xy1, xy2)`: infinite line through two data points.
    class AxLine& axline(Point2D xy1, Point2D xy2,
                         Color color = Color::black(), float width = 1.0f);
    /// mpl `ax.axline(xy1, slope=s)`: infinite line through a point with slope.
    class AxLine& axline(Point2D xy1, float slope,
                         Color color = Color::black(), float width = 1.0f);
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
    /// mpl `ax.bxp(bxpstats)`: box-and-whisker plot from precomputed
    /// statistics. `manageTicks` (default on) installs a FixedLocator/
    /// FixedFormatter at positions with each stat's label.
    /// (BxpStats/BxpConfig live in <volcano/plot/plots/BoxPlot.hpp>.)
    class BoxPlot& bxp(std::vector<struct BxpStats> stats,
                       struct BxpConfig cfg);
    /// mpl `ax.pie(data)`: adds a PiePlot. With `frame=false` (the mpl
    /// default) the axes frame and ticks are hidden, equal aspect is
    /// set, and the view is pinned to the pie extent ±1.25 + center.
    class PiePlot& pie(PieData data);

    /// mpl `ax.quiverkey(Q, X, Y, U, label)`: reference arrow at axes
    /// fraction (X, Y) rendered with `ref`'s scale so its magnitude-U
    /// length matches the field arrows. `labelPos`: "N"/"S"/"E"/"W".
    class QuiverKeyPlot& quiverKey(const class QuiverPlot& ref,
                                   float x, float y, float u,
                                   std::string label,
                                   std::string_view labelPos = "N",
                                   float angleDeg = 0.0f);

    /// mpl `ax.imshow(grid)`: 2D image/heatmap display.
    /// `interpolation` mirrors mpl: "nearest" (default), "bilinear",
    /// "bicubic", "antialiased" (GPU-approximated by bilinear).
    /// `aspect` mirrors mpl imshow: "equal" (default — square pixels,
    /// mpl rcParams image.aspect) or "auto" (fill the axes box).
    /// `origin` mirrors mpl imshow: "upper" (default — row 0 on top,
    /// mpl rcParams image.origin) or "lower" (row 0 at the bottom).
    class HeatmapPlot& imshow(Grid2D grid,
        const Colormap& cmap = colormaps::viridis(),
        std::string_view interpolation = "nearest",
        std::string_view aspect = "equal",
        std::string_view origin = "upper");

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
    /// Mutable category lists (vp.units/vp.category UnitData mapping).
    std::vector<std::string>& xCategoriesMut() { return xCategories_; }
    std::vector<std::string>& yCategoriesMut() { return yCategories_; }

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

    [[nodiscard]] const std::vector<std::shared_ptr<IPlot>>& plots() const noexcept { return plots_; }
    /// Mutable access — needed by interactive 3D rotation to update
    /// per-plot cameras (IPlot::camera3D is non-const).
    [[nodiscard]] std::vector<std::shared_ptr<IPlot>>& plots() noexcept { return plots_; }
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
    /// Mutable text annotations (mpl ax.texts children).
    [[nodiscard]] std::vector<TextAnnotation>& texts() noexcept { return texts_; }
    [[nodiscard]] const std::vector<Annotation>& annotations() const noexcept { return annotations_; }

    /// Add an anchored scale bar (mpl_toolkits.axes_grid1
    /// AnchoredSizeBar). `size` is the bar length in data-x units.
    SizeBar* addSizeBar(SizeBar bar) {
        sizeBars_.push_back(std::move(bar));
        return &sizeBars_.back();
    }
    [[nodiscard]] const std::vector<SizeBar>& sizeBars() const noexcept { return sizeBars_; }
    [[nodiscard]] std::vector<SizeBar>& sizeBars() noexcept { return sizeBars_; }

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
    [[nodiscard]] std::vector<AnchoredText>&
    anchoredTexts() noexcept { return anchoredTexts_; }

    /// Compute the data viewport from all layers if not manually set (CPU).
    void autoscale();

    /// mpl `ax.relim(visible_only=False)`: recompute the raw data limits
    /// (dataLim) from the plot layers without changing the view limits.
    /// With `visibleOnly`, invisible artists are skipped.
    void relim(bool visibleOnly = false);
    /// The raw data limits (mpl dataLim): eagerly updated by artist
    /// adds and update_datalim; rebuilt from artists by relim.
    [[nodiscard]] const Viewport& dataLim() const noexcept {
        return dataLim_;
    }
    /// mpl `Axes.cla()`/`clear()`: remove all artists/texts/annotations
    /// and reset labels, title, limits, scales, legend and margins to
    /// defaults. Position, subplot spec, projection and style are kept.
    void clear();
    /// mpl `ax.autoscale_view(tight=None, scalex=True, scaley=True)`:
    /// apply dataLim + margins (and sticky edges) to the view limits on
    /// axes whose autoscale flag is on, preserving axis inversion.
    void autoscaleView(std::optional<bool> tight = std::nullopt,
                       bool scalex = true, bool scaley = true);
    /// mpl `ax.autoscale(enable=True, axis='both', tight=None)`: toggle
    /// autoscaling on the selected axis(es) and autoscale the view.
    /// enable=None leaves the flags unchanged.
    void autoscale(std::optional<bool> enable, std::string_view axis = "both",
                   std::optional<bool> tight = std::nullopt);
    /// mpl set_autoscalex_on/set_autoscaley_on — turning a flag off
    /// freezes the current view limits (manual), on re-enables autoscale.
    void setAutoscalexOn(bool v) { manualX_ = !v; touch(); }
    void setAutoscaleyOn(bool v) { manualY_ = !v; touch(); }
    [[nodiscard]] bool getAutoscalexOn() const noexcept { return !manualX_; }
    [[nodiscard]] bool getAutoscaleyOn() const noexcept { return !manualY_; }
    /// mpl `ax.use_sticky_edges` (default True): artist sticky edges
    /// (e.g. a bar's baseline at 0) clamp the autoscale margin.
    bool useStickyEdges = true;

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
    /// Extra outward label offset in figure px — set every frame by the
    /// renderer when Figure::alignXlabels/alignYlabels are active (mpl
    /// shifts labelpad, we keep it as a per-frame computed offset).
    float xLabelShiftPx = 0.0f;
    float yLabelShiftPx = 0.0f;

private:
    /// Apply 5% padding and degenerate-range fixup to a raw min/max viewport.
    /// `stickyX`/`stickyY` are the sorted mpl sticky-edge values the
    /// margin must not cross (empty when useStickyEdges is off).
    void finalizeAutoscale(Viewport& v, bool tight,
                           const std::vector<float>& stickyX,
                           const std::vector<float>& stickyY) const;
    /// Collect sorted sticky-edge values from all (visible) layers.
    [[nodiscard]] StickyEdges collectStickyEdges() const;
    /// Install a converter's axisInfo defaults on 'x' or 'y'.
    void applyAxisInfo(const UnitConverter& conv, char axis);
    /// Reinstall FixedLocator/FixedFormatter for the category list.
    void installCategoryTicks(char axis);

    Viewport viewport_{0,1,0,1};
    /// Raw artist-derived data limits from the last relim/autoscale;
    /// empty bbox (mpl dataLim) until artists contribute.
    Viewport dataLim_{std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()};
    /// Points merged via update_datalim (mpl: part of dataLim).
    std::optional<Viewport> manualLim_;
    bool manualX_ = false, manualY_ = false;
    /// Autoscale padding fraction (matplotlib axes.xmargin/ymargin = 0.05).
    float marginX_ = 0.05f, marginY_ = 0.05f;
    AxisScale xScale_, yScale_;
    Projection projection_;
    std::vector<float> rgrids_, thetagrids_;
    float rlabelPosition_ = 22.5f;  // mpl default rlabel_position
    float geoGridEndsDeg_ = 75.0f;  // mpl set_longitude_grid_ends default
    AspectMode aspect_ = AspectMode::Auto;
    Adjustable adjustable_ = Adjustable::Box;
    std::vector<Axes*> shareXWith_, shareYWith_;
    std::optional<SecondaryAxis> secondaryX_, secondaryY_;
    AxisFurniture xFurn_, yFurn_;
    SpineSet spines_{};
    /// mpl axison / frame_on.
    bool axisOn_ = true;
    bool frameOn_ = true;
    /// mpl Artist state on the axes itself.
    bool visible_ = true;
    float zorder_ = 0.0f;
    std::optional<float> patchAlpha_;
    std::string gid_, url_, label_;
    std::optional<bool> snap_;
    bool rasterized_ = false, animated_ = false, mouseover_ = false;
    std::optional<float> picker_;
    float pickRadius_ = 5.0f;  // mpl rcParams lines.markeredgewidth-ish
    /// mpl navigate / in_layout bookkeeping.
    bool navigate_ = true;
    std::string navigateMode_;
    bool inLayout_ = true;
    /// Frozen pan-start state (mpl `_pan_start`).
    struct PanState {
        Viewport lim;
        TransformPtr trans;
        TransformPtr transInverse;
        Rect2D bbox;
        float x = 0, y = 0;
    };
    std::optional<PanState> panStart_;
    /// mpl set_box_aspect / set_anchor state.
    std::optional<float> boxAspect_;
    float anchorX_ = 0.5f, anchorY_ = 0.5f;
    std::string anchorName_;
    /// mpl label_outer state.
    bool labelOuter_ = false;
    bool removeInnerTicks_ = false;
    /// mpl Axis.isDefault_* bookkeeping — per axis [x=0, y=1] bitmask of
    /// which tick machinery is still automatic.
    static constexpr uint8_t kMajLoc = 1, kMinLoc = 2, kMajFmt = 4,
                             kMinFmt = 8;
    uint8_t defaultTick_[2] = {0x0f, 0x0f};
    /// Grid cell this axes occupies (set by Figure::addAxes).
    std::optional<SubplotSpec> subplotSpec_;
    Figure* figure_ = nullptr;
    /// mpl `axes.stale`: set by mutators, cleared after a draw.
    bool stale_ = true;
    FigureStyle style_;
    Cycler cycler_;
    std::vector<std::shared_ptr<IPlot>> plots_;
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
