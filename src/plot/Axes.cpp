// volcano/plot/Axes.cpp
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Collections.hpp"
#include "volcano/plot/Dates.hpp"
#include "volcano/plot/Specialized.hpp"
#include "volcano/plot/plots/ReferenceLines.hpp"
#include "volcano/plot/plots/HeatmapPlot.hpp"
#include "volcano/plot/plots/QuiverKeyPlot.hpp"
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/plot/plots/PiePlot.hpp"
#include "volcano/plot/plots/BoxPlot.hpp"
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Rc.hpp"
#include "volcano/plot/Ticks.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace volcano::plot {

Axes::Axes() : style_(rc::params()) { reseedCycler(); }

void Axes::touch() noexcept {
    stale_ = true;
    if (figure_) figure_->markStale();
}

void IPlot::touch() noexcept {
    if (owner_) owner_->touch();
}

void Axes::setStyle(FigureStyle s) {
    style_ = std::move(s);
    reseedCycler();
    touch();
}

void Axes::reseedCycler() {
    // Seed the prop cycle from the style; default to tab10 like matplotlib.
    if (!style_.propCycle.empty())
        cycler_ = Cycler::ofEntries(style_.propCycle);
    else if (style_.colorCycle.size() > 0)
        cycler_ = Cycler::ofColors(style_.colorCycle.colors);
    else {
        std::vector<Color> tab10;
        for (int i = 0; i < 10; ++i) tab10.push_back(ColorCycle::at(i));
        cycler_ = Cycler::ofColors(std::move(tab10));
    }
}

IPlot* Axes::addPlot(std::shared_ptr<IPlot> plot) {
    IPlot* raw = plot.get();
    raw->setOwner(this);
    if (!cycler_.empty()) {
        const auto& props = cycler_.peek();
        if (raw->applyCycleProps(props)) cycler_.advance();
    }
    plots_.push_back(std::move(plot));
    // mpl: artist add merges into dataLim eagerly (update_datalim).
    auto [fx, fy] = raw->feedsData(*this);
    if (fx || fy) {
        Viewport tmp{std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity()};
        raw->contributeToAutoscaleScaled(tmp, xScale_, yScale_);
        if (fx) {
            dataLim_.x.min = std::min(dataLim_.x.min, tmp.x.min);
            dataLim_.x.max = std::max(dataLim_.x.max, tmp.x.max);
        }
        if (fy) {
            dataLim_.y.min = std::min(dataLim_.y.min, tmp.y.min);
            dataLim_.y.max = std::max(dataLim_.y.max, tmp.y.max);
        }
    }
    touch();
    return raw;
}

std::shared_ptr<IPlot> Axes::removePlot(const IPlot* p) {
    auto it = std::ranges::find_if(plots_, [&](const auto& up) {
        return up.get() == p;
    });
    if (it == plots_.end()) return nullptr;
    auto owned = std::move(*it);
    plots_.erase(it);
    owned->setOwner(nullptr);
    touch();
    return owned;
}

Patch& Axes::addPatch(Patch p) {
    auto coll = std::make_unique<PatchCollection>(std::vector<Patch>{std::move(p)});
    auto* raw = coll.get();
    addPlot(std::move(coll));
    return raw->patches.back();
}

Axes::SpineSpec& Axes::SpineSet::side(std::string_view s) {
    if (s == "left")   return left;
    if (s == "right")  return right;
    if (s == "bottom") return bottom;
    if (s == "top")    return top;
    throw std::invalid_argument(
        std::format("unknown spine side '{}'", s));
}
const Axes::SpineSpec& Axes::SpineSet::side(std::string_view s) const {
    return const_cast<SpineSet*>(this)->side(s);
}

void Axes::setSpineVisible(std::string_view side, bool visible) {
    if (side == "all") {
        spines_.left.visible = spines_.right.visible = visible;
        spines_.bottom.visible = spines_.top.visible = visible;
    } else {
        spines_.side(side).visible = visible;
    }
    touch();
}

// ─── mpl Axis.set_ticks_position / set_label_position ─────────────────
// Per-side furniture: 'top'/'bottom' move marks+labels to one side,
// 'both' adds marks to both sides (labels unchanged), 'none' clears
// marks (labels unchanged), 'default' resets (mpl: marks both, labels
// near — note mpl's default for set_ticks_position puts marks on both
// sides; rcParams xtick.top defaults False for a fresh axes, which our
// AxisFurniture{} matches).

void Axes::setXTicksPosition(std::string_view pos) {
    auto& f = xFurn_;
    if (pos == "top") {
        f.marksNear = false; f.marksFar = true;
        f.labelsNear = false; f.labelsFar = true;
    } else if (pos == "bottom") {
        f.marksNear = true; f.marksFar = false;
        f.labelsNear = true; f.labelsFar = false;
    } else if (pos == "both") {
        f.marksNear = f.marksFar = true;
    } else if (pos == "none") {
        f.marksNear = f.marksFar = false;
    } else if (pos == "default") {
        f.marksNear = true; f.marksFar = true;
        f.labelsNear = true; f.labelsFar = false;
    } else {
        throw std::invalid_argument(std::format(
            "set_ticks_position: unknown position '{}' "
            "(expected top/bottom/both/default/none)", pos));
    }
    touch();
}
void Axes::setYTicksPosition(std::string_view pos) {
    auto& f = yFurn_;
    if (pos == "right") {
        f.marksNear = false; f.marksFar = true;
        f.labelsNear = false; f.labelsFar = true;
    } else if (pos == "left") {
        f.marksNear = true; f.marksFar = false;
        f.labelsNear = true; f.labelsFar = false;
    } else if (pos == "both") {
        f.marksNear = f.marksFar = true;
    } else if (pos == "none") {
        f.marksNear = f.marksFar = false;
    } else if (pos == "default") {
        f.marksNear = true; f.marksFar = true;
        f.labelsNear = true; f.labelsFar = false;
    } else {
        throw std::invalid_argument(std::format(
            "set_ticks_position: unknown position '{}' "
            "(expected left/right/both/default/none)", pos));
    }
    touch();
}
void Axes::setXLabelPosition(std::string_view pos) {
    if (pos == "top") xFurn_.labelFar = true;
    else if (pos == "bottom") xFurn_.labelFar = false;
    else throw std::invalid_argument(std::format(
        "set_label_position: unknown position '{}' "
        "(expected top/bottom)", pos));
    touch();
}
void Axes::setYLabelPosition(std::string_view pos) {
    if (pos == "right") yFurn_.labelFar = true;
    else if (pos == "left") yFurn_.labelFar = false;
    else throw std::invalid_argument(std::format(
        "set_label_position: unknown position '{}' "
        "(expected left/right)", pos));
    touch();
}
// mpl Axis.tick_top()/tick_bottom()/tick_left()/tick_right(): move marks
// and labels to that side, but if labels were disabled on both sides
// (label1On and label2On both off) they stay off.

void Axes::xaxisTickTop() {
    const bool label = xFurn_.labelsNear || xFurn_.labelsFar;
    setXTicksPosition("top");
    xFurn_.labelsFar = label;
}
void Axes::xaxisTickBottom() {
    const bool label = xFurn_.labelsNear || xFurn_.labelsFar;
    setXTicksPosition("bottom");
    xFurn_.labelsNear = label;
}
void Axes::yaxisTickLeft() {
    const bool label = yFurn_.labelsNear || yFurn_.labelsFar;
    setYTicksPosition("left");
    yFurn_.labelsNear = label;
}
void Axes::yaxisTickRight() {
    const bool label = yFurn_.labelsNear || yFurn_.labelsFar;
    setYTicksPosition("right");
    yFurn_.labelsFar = label;
}

// mpl Axis._get_ticks_position(): near-marks+near-labels only → 1 (the
// near side name); far-marks+far-labels only → 2 (the far side name);
// both-marks+near-labels → "default"; anything else → "unknown".

std::string_view Axes::xTicksPosition() const {
    const auto& f = xFurn_;
    if (f.marksNear && !f.marksFar && f.labelsNear && !f.labelsFar)
        return "bottom";
    if (f.marksFar && !f.marksNear && f.labelsFar && !f.labelsNear)
        return "top";
    if (f.marksNear && f.marksFar && f.labelsNear && !f.labelsFar)
        return "default";
    return "unknown";
}
std::string_view Axes::yTicksPosition() const {
    const auto& f = yFurn_;
    if (f.marksNear && !f.marksFar && f.labelsNear && !f.labelsFar)
        return "left";
    if (f.marksFar && !f.marksNear && f.labelsFar && !f.labelsNear)
        return "right";
    if (f.marksNear && f.marksFar && f.labelsNear && !f.labelsFar)
        return "default";
    return "unknown";
}

Axes::SpineLineGeom Axes::spineLine(std::string_view side,
                                    Rect2D rect) const {
    // mpl Spine.set_position: resolve the spine's cross-axis pixel
    // coordinate, then its along-axis span (set_bounds in data coords).
    const auto& s = spines_.side(side);
    const bool horiz = side == "bottom" || side == "top";
    const bool farSide = side == "top" || side == "right";
    const float x0 = float(rect.x), y0 = float(rect.y);
    const float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    // Default edge: bottom→y1, top→y0, left→x0, right→x1.
    float pos = horiz ? (farSide ? y0 : y1) : (farSide ? x1 : x0);
    if (s.positionSet) {
        switch (s.posMode) {
        case SpineSpec::PosMode::Axes:
            // Fraction of the axes box (0 = bottom/left edge).
            pos = horiz ? y1 - s.posAmount * float(rect.height)
                        : x0 + s.posAmount * float(rect.width);
            break;
        case SpineSpec::PosMode::Data: {
            float f = horiz ? dataToFraction({0.0f, s.posAmount}).y
                            : dataToFraction({s.posAmount, 0.0f}).x;
            pos = horiz ? y1 - f * float(rect.height)
                        : x0 + f * float(rect.width);
            break;
        }
        case SpineSpec::PosMode::Outward: {
            // mpl ('outward', pts): move away from the axes center.
            float d = horiz ? (farSide ? -1.0f : 1.0f)
                            : (farSide ? 1.0f : -1.0f);
            pos += d * s.posAmount;
            break;
        }
        }
    }
    // Along-axis span: default the full edge; set_bounds narrows it
    // (data coords → pixels, clipped to the axes box).
    float from = horiz ? x0 : y0, to = horiz ? x1 : y1;
    if (s.bounds) {
        float a = horiz ? dataToFraction({s.bounds->first, 0.0f}).x
                        : dataToFraction({0.0f, s.bounds->first}).y;
        float b = horiz ? dataToFraction({s.bounds->second, 0.0f}).x
                        : dataToFraction({0.0f, s.bounds->second}).y;
        if (a > b) std::swap(a, b);
        if (horiz) { from = x0 + a * float(rect.width);
                     to   = x0 + b * float(rect.width); }
        else       { to   = y1 - a * float(rect.height);
                     from = y1 - b * float(rect.height); }
    }
    return {pos, from, to};
}

void Axes::setXbound(float lower, float upper) {
    setXbound(std::optional<float>(lower), std::optional<float>(upper));
}

void Axes::setYbound(float lower, float upper) {
    setYbound(std::optional<float>(lower), std::optional<float>(upper));
}

void Axes::setXbound(std::optional<float> lower, std::optional<float> upper) {
    // mpl set_xbound: get the displayed (sorted) bounds, fill the None
    // sides from them, then assign sorted((lower, upper), reverse=
    // inverted) — so an inverted axis keeps its inversion and a bound
    // can never be written into the wrong slot mid-update.
    auto& r = viewport_.x;
    float lo = lower.value_or(std::min(r.min, r.max));
    float hi = upper.value_or(std::max(r.min, r.max));
    if (hi < lo) std::swap(lo, hi);
    if (xAxisInverted()) { r.min = hi; r.max = lo; }
    else                 { r.min = lo; r.max = hi; }
    manualX_ = true;
    touch();
}

void Axes::setYbound(std::optional<float> lower, std::optional<float> upper) {
    auto& r = viewport_.y;
    float lo = lower.value_or(std::min(r.min, r.max));
    float hi = upper.value_or(std::max(r.min, r.max));
    if (hi < lo) std::swap(lo, hi);
    if (yAxisInverted()) { r.min = hi; r.max = lo; }
    else                 { r.min = lo; r.max = hi; }
    manualY_ = true;
    touch();
}

void Axes::locatorParams(std::string_view axis, const LocatorParams& p) {
    auto apply = [&](TickConfig& tc, const AxisScale& scale,
                     float& margin) {
        auto* mnl = dynamic_cast<MaxNLocator*>(tc.locator.get());
        if (!mnl && scale.kind != ScaleKind::Linear && tc.locator == nullptr) {
            // mpl: on a non-linear axis the major locator is a
            // LogLocator/SymmetricalLogLocator/etc. — locator_params
            // forwards to set_params, which ignores the kwargs.
            if (p.tight) margin = 0.0f;
            return;
        }
        if (!mnl) {
            // mpl: locator_params forwards to the current locator's
            // set_params; locators other than MaxNLocator/AutoLocator
            // ignore (with a warning) params they don't understand.
            // 'tight' is an autoscale option and still applies.
            if (tc.locator) {
                if (p.tight) margin = 0.0f;
                return;
            }
            auto loc = std::make_shared<MaxNLocator>();
            // mpl AutoLocator: nbins='auto' — derived from the axis'
            // tick space by axisTicks (capped by tc.nbins).
            loc->setNbinsAuto();
            mnl = loc.get();
            tc.locator = std::move(loc);
        }
        if (p.nbins) mnl->setNbins(*p.nbins);
        if (p.steps) mnl->setSteps(*p.steps);
        if (p.integer) mnl->setInteger(*p.integer);
        if (p.symmetric) mnl->setSymmetric(*p.symmetric);
        if (p.prune) mnl->setPrune(*p.prune);
        if (p.minNTicks) mnl->setMinNTicks(*p.minNTicks);
        if (p.tight) margin = 0.0f;
    };
    if (axis == "x" || axis == "both")
        apply(style_.xAxis.ticks, xScale_, marginX_);
    if (axis == "y" || axis == "both")
        apply(style_.yAxis.ticks, yScale_, marginY_);
    touch();
}

bool Axes::axis(std::string_view option) {
    if (option == "off") {
        setAxisOff();
    } else if (option == "on") {
        setAxisOn(true);
    } else if (option == "equal" || option == "square") {
        setAspect(AspectMode::Equal);
    } else if (option == "auto") {
        setAspect(AspectMode::Auto);
    } else if (option == "scaled" || option == "image") {
        // mpl: equal aspect with adjustable="datalim".
        setAspect(AspectMode::Equal);
        setAdjustable(Adjustable::DataLim);
    } else if (option == "tight") {
        margins(0.0f);
    } else {
        return false;
    }
    touch();
    return true;
}

bool Axes::isLastRow() const {
    if (subplotSpec_ && subplotSpec_->grid)
        return subplotSpec_->row + subplotSpec_->rowSpan ==
               subplotSpec_->grid->rows();
    if (!figure_) return true;
    // Non-grid placement (inset/twin/fraction): last row means no
    // sibling axes horizontally overlapping us extends below our rect.
    const float bottom = float(rect.y) + float(rect.height);
    for (const auto& p : figure_->placements()) {
        const Axes* sib = p.axes.get();
        if (sib == this) continue;
        const auto& r = sib->rect;
        bool overlapX = r.x < rect.x + rect.width &&
                        rect.x < r.x + r.width;
        if (overlapX && r.y + r.height > bottom + 1.0f)
            return false;
    }
    return true;
}

bool Axes::isFirstCol() const {
    if (subplotSpec_ && subplotSpec_->grid)
        return subplotSpec_->col == 0;
    if (!figure_) return true;
    for (const auto& p : figure_->placements()) {
        const Axes* sib = p.axes.get();
        if (sib == this) continue;
        const auto& r = sib->rect;
        bool overlapY = r.y < rect.y + rect.height &&
                        rect.y < r.y + r.height;
        if (overlapY && r.x < float(rect.x) - 1.0f)
            return false;
    }
    return true;
}

std::vector<const IPlot*> Axes::drawOrder() const {
    std::vector<const IPlot*> order;
    order.reserve(plots_.size());
    for (const auto& p : plots_) order.push_back(p.get());
    std::stable_sort(order.begin(), order.end(),
                     [](const IPlot* a, const IPlot* b) {
                         return a->zorder < b->zorder;
                     });
    return order;
}

std::vector<const IPlot*> Axes::pick(Point2D dataPt) const {
    auto order = drawOrder();
    std::vector<const IPlot*> hits;
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if ((*it)->contains(*this, dataPt)) hits.push_back(*it);
    return hits;
}

TablePlot& Axes::table(std::vector<std::vector<std::string>> cellText,
                       std::string loc) {
    auto t = std::make_unique<TablePlot>();
    t->cellText = std::move(cellText);
    t->loc = std::move(loc);
    auto* raw = t.get();
    addPlot(std::move(t));
    return *raw;
}

namespace {
template <class T, class... Args>
T& addOwned(Axes& ax, Args&&... args) {
    auto p = std::make_unique<T>(std::forward<Args>(args)...);
    auto* raw = p.get();
    ax.addPlot(std::move(p));
    return *raw;
}
} // namespace

AxhLine& Axes::axhline(float y, Color color, float width) {
    return addOwned<AxhLine>(*this, y, color, width);
}
AxvLine& Axes::axvline(float x, Color color, float width) {
    return addOwned<AxvLine>(*this, x, color, width);
}
AxLine& Axes::axline(Point2D xy1, Point2D xy2, Color color, float width) {
    return addOwned<AxLine>(*this, xy1, xy2, color, width);
}
AxLine& Axes::axline(Point2D xy1, float slope, Color color, float width) {
    // mpl axline(xy1, slope=s): second point at xy1 + (1, slope).
    Point2D xy2{xy1.x + 1.0f, xy1.y + slope};
    return addOwned<AxLine>(*this, xy1, xy2, color, width);
}
AxhSpan& Axes::axhspan(float y1, float y2, Color color) {
    return addOwned<AxhSpan>(*this, y1, y2, color);
}
AxvSpan& Axes::axvspan(float x1, float x2, Color color) {
    return addOwned<AxvSpan>(*this, x1, x2, color);
}
Hlines& Axes::hlines(std::vector<float> y, float xMin, float xMax,
                     Color color, float width) {
    return addOwned<Hlines>(*this, std::move(y), xMin, xMax, color, width);
}
Vlines& Axes::vlines(std::vector<float> x, float yMin, float yMax,
                     Color color, float width) {
    return addOwned<Vlines>(*this, std::move(x), yMin, yMax, color, width);
}
EventPlot& Axes::eventplot(std::vector<std::vector<float>> positions) {
    return addOwned<EventPlot>(*this, std::move(positions));
}
EventPlot& Axes::eventplot(std::vector<float> positions) {
    return addOwned<EventPlot>(*this, std::move(positions));
}
BoxPlot& Axes::bxp(std::vector<BxpStats> stats, BxpConfig cfg) {
    if (cfg.manageTicks) {
        std::vector<float> locs;
        std::vector<std::string> labs;
        locs.reserve(stats.size());
        labs.reserve(stats.size());
        for (size_t i = 0; i < stats.size(); ++i) {
            locs.push_back(i < cfg.positions.size() ? cfg.positions[i]
                                                  : float(i + 1));
            labs.push_back(stats[i].label);
        }
        style_.xAxis.ticks.locator =
            std::make_shared<FixedLocator>(std::move(locs));
        style_.xAxis.ticks.formatter =
            std::make_shared<FixedFormatter>(std::move(labs));
    }
    auto p = std::make_unique<BoxPlot>(std::move(stats), std::move(cfg));
    return *static_cast<BoxPlot*>(addPlot(std::move(p)));
}
PiePlot& Axes::pie(PieData data) {
    // mpl Axes.pie validation: negative wedge sizes, an unnormalized
    // pie summing past a full circle, and nonpositive radii all raise.
    for (float v : data.values)
        if (v < 0)
            throw std::invalid_argument(
                "pie: wedge sizes must be non negative values");
    if (!data.normalize) {
        float sum = 0;
        for (float v : data.values) sum += v;
        if (sum > 1.0f)
            throw std::invalid_argument(
                "pie: cannot plot an unnormalized pie with sum(x) > 1");
    }
    if (data.radius <= 0.0f)
        throw std::invalid_argument("pie: 'radius' must be positive");
    // mpl Axes.pie(frame=False) — the default — hides the axes frame
    // and ticks and pins the view to the pie extent (-1.25..1.25 + c).
    if (!data.frame) {
        setFrameOn(false);
        setAspect(AspectMode::Equal);
        style_.xAxis.ticks.locator =
            std::make_shared<FixedLocator>(std::vector<float>{});
        style_.yAxis.ticks.locator =
            std::make_shared<FixedLocator>(std::vector<float>{});
        setViewport({-1.25f + data.center.x, 1.25f + data.center.x,
                     -1.25f + data.center.y, 1.25f + data.center.y});
    }
    return *static_cast<PiePlot*>(
        addPlot(std::make_unique<PiePlot>(std::move(data))));
}

HeatmapPlot& Axes::imshow(Grid2D grid, const Colormap& cmap,
                          std::string_view interpolation,
                          std::string_view aspect,
                          std::string_view origin) {
    grid.interpolation = std::string(interpolation);
    grid.origin = std::string(origin);
    // mpl imshow defaults to aspect="equal" (rcParams image.aspect);
    // "auto" stretches the image to fill the axes box.
    if (aspect == "equal")
        setAspect(AspectMode::Equal);
    // mpl AxesImage.set_extent: viewLim := the extent verbatim (so the
    // default origin='upper' extent (l, r, h-0.5, -0.5) inverts the y
    // axis), gated on autoscale-on; the autoscale flags themselves are
    // preserved and later autoscale keeps the inversion direction.
    const Range exX = grid.xRange, exY = grid.yRange;
    auto& h = addOwned<HeatmapPlot>(*this, std::move(grid), cmap);
    if (!manualX_) viewport_.x = exX;
    if (!manualY_) viewport_.y = exY;
    touch();
    return h;
}

QuiverKeyPlot& Axes::quiverKey(const QuiverPlot& ref, float x, float y,
                               float u, std::string label,
                               std::string_view labelPos,
                               float angleDeg) {
    QuiverKeyPlot::Config cfg{};
    cfg.label = std::move(label);
    cfg.angleDeg = angleDeg;
    if (labelPos == "S" || labelPos == "s")
        cfg.labelPos = QuiverKeyPlot::LabelPos::S;
    else if (labelPos == "E" || labelPos == "e")
        cfg.labelPos = QuiverKeyPlot::LabelPos::E;
    else if (labelPos == "W" || labelPos == "w")
        cfg.labelPos = QuiverKeyPlot::LabelPos::W;
    auto* raw = new QuiverKeyPlot(x, y, u, &ref, std::move(cfg));
    addPlot(std::unique_ptr<IPlot>(raw));
    return *raw;
}

WordCloudPlot& Axes::wordcloud(
        std::vector<std::pair<std::string, double>> words) {
    auto p = std::make_unique<WordCloudPlot>();
    p->words.reserve(words.size());
    for (auto& [t, w] : words)
        p->words.push_back({std::move(t), w});
    auto* raw = p.get();
    addPlot(std::move(p));
    return *raw;
}

NetworkPlot& Axes::network(
        uint32_t nodeCount,
        std::vector<std::pair<uint32_t, uint32_t>> edges) {
    return addOwned<NetworkPlot>(*this, nodeCount, std::move(edges));
}

void Axes::setXscale(std::string_view name) {
    if (name == "linear")      xScale_ = AxisScale::linear();
    else if (name == "log")    xScale_ = AxisScale::log();
    else if (name == "symlog") xScale_ = AxisScale::symlog();
    else if (name == "logit")  xScale_ = AxisScale::logit();
    else if (name == "asinh")  xScale_ = AxisScale::asinh();
    else if (name == "mercator") xScale_ = AxisScale::mercator();
    touch();
}

void Axes::setYscale(std::string_view name) {
    if (name == "linear")      yScale_ = AxisScale::linear();
    else if (name == "log")    yScale_ = AxisScale::log();
    else if (name == "symlog") yScale_ = AxisScale::symlog();
    else if (name == "logit")  yScale_ = AxisScale::logit();
    else if (name == "asinh")  yScale_ = AxisScale::asinh();
    else if (name == "mercator") yScale_ = AxisScale::mercator();
    touch();
}

void Axes::setThetaZeroLocation(std::string_view loc) {
    constexpr float kHalfPi = 1.5707963267948966f;
    constexpr float kPi = 3.14159265358979323846f;
    if (loc == "N")      projection_.thetaOffset = kHalfPi;
    else if (loc == "S") projection_.thetaOffset = -kHalfPi;
    else if (loc == "W") projection_.thetaOffset = kPi;
    else if (loc == "E") projection_.thetaOffset = 0.0f;
    else if (loc == "NE") projection_.thetaOffset = kHalfPi / 2.0f;
    else if (loc == "NW") projection_.thetaOffset = 3.0f * kHalfPi / 2.0f;
    else if (loc == "SE") projection_.thetaOffset = -kHalfPi / 2.0f;
    else if (loc == "SW") projection_.thetaOffset = -3.0f * kHalfPi / 2.0f;
    touch();
}

void Axes::shareX(Axes& other) {
    shareXWith_.push_back(&other);
    other.shareXWith_.push_back(this);
    other.viewport_.x = viewport_.x;
    other.manualX_ = manualX_;
    touch();
    other.touch();
}

void Axes::shareY(Axes& other) {
    shareYWith_.push_back(&other);
    other.shareYWith_.push_back(this);
    other.viewport_.y = viewport_.y;
    other.manualY_ = manualY_;
    touch();
    other.touch();
}

Axes* Axes::twinx() {
    return figure_ ? figure_->twinx(*this) : nullptr;
}

Axes* Axes::twiny() {
    return figure_ ? figure_->twiny(*this) : nullptr;
}

Axes* Axes::insetAxes(float x, float y, float w, float h) {
    return figure_ ? figure_->insetAxes(*this, x, y, w, h) : nullptr;
}

void Axes::secondaryXaxis(std::function<float(float)> forward,
                          std::function<float(float)> inverse,
                          std::string label) {
    secondaryX_ = SecondaryAxis{std::move(forward), std::move(inverse),
                                std::move(label), true};
    touch();
}

void Axes::secondaryYaxis(std::function<float(float)> forward,
                          std::function<float(float)> inverse,
                          std::string label) {
    secondaryY_ = SecondaryAxis{std::move(forward), std::move(inverse),
                                std::move(label), true};
    touch();
}

void Axes::finalizeAutoscale(Viewport& v, bool tight,
                             const std::vector<float>& stickyX,
                             const std::vector<float>& stickyY) const {
    // Check each axis independently — a plot may only contribute to one
    // axis (e.g., AxhLine only contributes y, AxvLine only contributes x).
    if (v.x.min > v.x.max) v.x = {0,1};
    if (v.y.min > v.y.max) v.y = {0,1};
    if (v.z.min > v.z.max) v.z = {0,1};
    // Handle zero-span axes (e.g., single horizontal line at one y value).
    if (v.x.span() == 0) { v.x.min -= 0.5f; v.x.max += 0.5f; }
    if (v.y.span() == 0) { v.y.min -= 0.5f; v.y.max += 0.5f; }
    // Margins (mpl axes.xmargin/ymargin, default 5%). For non-linear
    // scales, pad in display (transformed) space and map back — padding
    // raw data space can push the lower bound outside the scale domain
    // (e.g., negative values on log).
    // mpl autoscale_view: sticky edges bound the margin expansion — the
    // padded limit is clamped back to the nearest sticky data value
    // (e.g. a bar's baseline at 0 keeps ylim from padding below 0).
    auto padAxis = [](Range& r, const AxisScale& s, float margin,
                      const std::vector<float>& sticky) {
        float lo = r.min - r.span() * margin;
        float hi = r.max + r.span() * margin;
        if (s.kind != ScaleKind::Linear) {
            float a = s.forward(r.min), b = s.forward(r.max);
            if (a > b) std::swap(a, b);
            float tpad = (b - a) * margin;
            lo = s.inverse(a - tpad);
            hi = s.inverse(b + tpad);
        }
        if (!sticky.empty()) {
            // mpl handle_single_axis: largest sticky <= lo-side data
            // limit (+tol) bounds the padded minimum; smallest sticky
            // >= hi-side data limit (-tol) bounds the padded maximum.
            float tol = 1e-5f * std::fabs(r.span());
            auto i0 = std::upper_bound(sticky.begin(), sticky.end(),
                                       r.min + tol);
            if (i0 != sticky.begin()) lo = std::max(lo, *std::prev(i0));
            auto i1 = std::lower_bound(sticky.begin(), sticky.end(),
                                       r.max - tol);
            if (i1 != sticky.end()) hi = std::min(hi, *i1);
        }
        r.min = lo; r.max = hi;
    };
    if (!tight) {
        padAxis(v.x, xScale_, marginX_, stickyX);
        padAxis(v.y, yScale_, marginY_, stickyY);
    }
}

StickyEdges Axes::collectStickyEdges() const {
    StickyEdges out;
    if (!useStickyEdges) return out;
    for (const auto& p : plots_) {
        auto e = p->stickyEdges();
        out.x.insert(out.x.end(), e.x.begin(), e.x.end());
        out.y.insert(out.y.end(), e.y.begin(), e.y.end());
    }
    // mpl: the axes' own sticky_edges merge with the artists'.
    out.x.insert(out.x.end(), stickyEdges.x.begin(), stickyEdges.x.end());
    out.y.insert(out.y.end(), stickyEdges.y.begin(), stickyEdges.y.end());
    // mpl: sticky edges don't apply on log scales for nonpositive values.
    std::erase_if(out.x, [](float v) { return !std::isfinite(v); });
    std::erase_if(out.y, [](float v) { return !std::isfinite(v); });
    if (xScale_.kind == ScaleKind::Log)
        std::erase_if(out.x, [](float v) { return v <= 0.0f; });
    if (yScale_.kind == ScaleKind::Log)
        std::erase_if(out.y, [](float v) { return v <= 0.0f; });
    std::ranges::sort(out.x);
    std::ranges::sort(out.y);
    return out;
}

void Axes::updateDataLim(std::span<const Point2D> pts, bool updatex,
                         bool updatey) {
    // mpl: ignore_existing_data_limits makes the next update start from
    // an empty bbox; the flag is consumed (reset) by each update.
    if (ignoreExistingDataLimits) {
        manualLim_.reset();
        dataLim_ = { std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity() };
        ignoreExistingDataLimits = false;
    }
    Viewport& m = manualLim_.emplace(
        Viewport{ std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity() });
    for (auto p : pts) {
        if (updatex) {
            m.x.min = std::min(m.x.min, p.x);
            m.x.max = std::max(m.x.max, p.x);
            dataLim_.x.min = std::min(dataLim_.x.min, p.x);
            dataLim_.x.max = std::max(dataLim_.x.max, p.x);
        }
        if (updatey) {
            m.y.min = std::min(m.y.min, p.y);
            m.y.max = std::max(m.y.max, p.y);
            dataLim_.y.min = std::min(dataLim_.y.min, p.y);
            dataLim_.y.max = std::max(dataLim_.y.max, p.y);
        }
    }
    touch();
}

bool Axes::containsPoint(Point2D displayPx) const noexcept {
    // mpl Bbox.contains: axes rect in display px (canvas Y-down).
    return displayPx.x >= float(rect.x) &&
           displayPx.x <= float(rect.x) + float(rect.width) &&
           displayPx.y >= float(rect.y) &&
           displayPx.y <= float(rect.y) + float(rect.height);
}

bool Axes::contains(Point2D displayPx) const noexcept {
    // mpl Artist.contains uses pickradius tolerance around the edge.
    if (!pickable()) return false;
    float r = pickRadius_;
    return displayPx.x >= float(rect.x) - r &&
           displayPx.x <= float(rect.x) + float(rect.width) + r &&
           displayPx.y >= float(rect.y) - r &&
           displayPx.y <= float(rect.y) + float(rect.height) + r;
}

float Axes::dataRatio() const noexcept {
    // mpl get_data_ratio: yspan/xspan of the view interval, floored at
    // 1e-300 against degenerate ranges.
    const auto& vp = viewport_;
    double xs = std::max(std::abs(double(vp.x.span())), 1e-300);
    double ys = std::max(std::abs(double(vp.y.span())), 1e-300);
    return static_cast<float>(ys / xs);
}

float Axes::dataRatioLog() const noexcept {
    // mpl get_data_ratio_log: same over log10 of the view interval.
    const auto& vp = viewport_;
    double xs = std::max(std::abs(std::log10(std::abs(double(vp.x.max))) -
                                std::log10(std::abs(double(vp.x.min)))),
                         1e-300);
    double ys = std::max(std::abs(std::log10(std::abs(double(vp.y.max))) -
                                std::log10(std::abs(double(vp.y.min)))),
                         1e-300);
    return static_cast<float>(ys / xs);
}

void Axes::applyAspect() {
    // mpl apply_aspect: adjustable='box' shrinks the axes rect (anchor
    // positions it inside the allocated cell); 'datalim' expands the
    // view limits instead. box_aspect overrides the target w/h ratio.
    float w = static_cast<float>(rect.width),
          h = static_cast<float>(rect.height);
    if (w <= 0.0f || h <= 0.0f) return;
    float xs, ys;
    if (projection_.kind == ProjectionKind::Rectilinear) {
        xs = std::fabs(viewport_.x.span());
        ys = std::fabs(viewport_.y.span());
    } else {
        auto b = transform().view;
        xs = std::fabs(b.x.span());
        ys = std::fabs(b.y.span());
    }
    float target;   // required w/h
    if (boxAspect_) {
        target = *boxAspect_;
    } else {
        if (aspect_ != AspectMode::Equal || xs <= 0.0f || ys <= 0.0f)
            return;
        target = xs / ys;
    }
    if (adjustable_ == Adjustable::Box) {
        Rect2D r = rect;
        if (w / h > target) {
            uint32_t nw = static_cast<uint32_t>(h * target);
            r.x += static_cast<int32_t>((w - nw) * anchorX_);
            r.width = nw;
        } else {
            uint32_t nh = static_cast<uint32_t>(w / target);
            r.y += static_cast<int32_t>((h - nh) * anchorY_);
            r.height = nh;
        }
        rect = r;
    } else if (xs > 0.0f && ys > 0.0f) {
        // adjustable='datalim': expand the smaller data range until
        // xs/ys == target (px/unit equal when aspect='equal', or the
        // box ratio when box_aspect is set).
        float pxPerX = w / xs, pxPerY = h / ys;
        if (pxPerX > pxPerY) {
            float need = w / pxPerY;
            float mid = (viewport_.x.min + viewport_.x.max) / 2.0f;
            viewport_.x = {mid - need / 2.0f, mid + need / 2.0f};
        } else {
            float need = h / pxPerX;
            float mid = (viewport_.y.min + viewport_.y.max) / 2.0f;
            viewport_.y = {mid - need / 2.0f, mid + need / 2.0f};
        }
    }
    touch();
}

void Axes::clear() {
    // mpl Axes.cla(): remove every artist and reset the labeling,
    // limits, scales, legend and margins. Position (rect, subplot spec),
    // projection, sharing links and the style itself are kept.
    plots_.clear();
    texts_.clear();
    annotations_.clear();
    sizeBars_.clear();
    insetIndicators_.clear();
    anchoredTexts_.clear();
    xCategories_.clear();
    yCategories_.clear();
    viewport_ = {0.0f, 1.0f, 0.0f, 1.0f};
    dataLim_ = {std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()};
    manualLim_.reset();
    manualX_ = manualY_ = false;
    marginX_ = marginY_ = 0.05f;
    xScale_ = yScale_ = AxisScale{};
    rgrids_.clear();
    thetagrids_.clear();
    rlabelPosition_ = 22.5f;
    aspect_ = AspectMode::Auto;
    adjustable_ = Adjustable::Box;
    boxAspect_.reset();
    secondaryX_.reset();
    secondaryY_.reset();
    style_.xAxis.label.clear();
    style_.yAxis.label.clear();
    style_.title.text.clear();
    style_.legend.visible = false;
    style_.colorbar.visible = false;
    touch();
}

void Axes::relim(bool visibleOnly) {
    Viewport v{ std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() };
    for (const auto& p : plots_) {
        if (visibleOnly && !p->visible) continue;
        // mpl: artists whose transform has no transData branch don't
        // feed the data limits; blended transforms feed each axis
        // independently (x=data/y=axes feeds only xlim).
        auto [fx, fy] = p->feedsData(*this);
        if (!fx && !fy) continue;
        if (fx && fy) {
            p->contributeToAutoscaleScaled(v, xScale_, yScale_);
            continue;
        }
        Viewport tmp{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest() };
        p->contributeToAutoscaleScaled(tmp, xScale_, yScale_);
        auto& r = fx ? v.x : v.y;
        const auto& s = fx ? tmp.x : tmp.y;
        r.min = std::min(r.min, s.min);
        r.max = std::max(r.max, s.max);
    }
    dataLim_ = v;
    // mpl: update_datalim contributions live in dataLim; keep them
    // merged across implicit relims (autoscale).
    if (manualLim_) {
        dataLim_.x.min = std::min(dataLim_.x.min, manualLim_->x.min);
        dataLim_.x.max = std::max(dataLim_.x.max, manualLim_->x.max);
        dataLim_.y.min = std::min(dataLim_.y.min, manualLim_->y.min);
        dataLim_.y.max = std::max(dataLim_.y.max, manualLim_->y.max);
    }
}

void Axes::autoscaleView(std::optional<bool> tight, bool scalex,
                       bool scaley) {
    // mpl autoscale_view: apply dataLim + margins to autoscale-enabled
    // axes, preserving inversion (set_bound keeps the direction). mpl's
    // 'tight' only skips the margin expansion; it does not zero the
    // margins (unlike autoscale(tight=...)). The manual flags are the
    // mpl autoscale-on flags — set*bound would flip them, so restore.
    Viewport v = dataLim_;
    bool t = tight.value_or(false);
    for (const auto& p : plots_)
        t |= p->tightAutoscale();
    auto sticky = collectStickyEdges();
    finalizeAutoscale(v, t, sticky.x, sticky.y);
    bool mx = manualX_, my = manualY_;
    if (scalex && !mx && v.x.min <= v.x.max) setXbound(v.x.min, v.x.max);
    if (scaley && !my && v.y.min <= v.y.max) setYbound(v.y.min, v.y.max);
    manualX_ = mx; manualY_ = my;
    touch();
}

void Axes::autoscale(std::optional<bool> enable, std::string_view axis,
                     std::optional<bool> tight) {
    bool sx = axis == "x" || axis == "both";
    bool sy = axis == "y" || axis == "both";
    if (enable) {
        if (sx) manualX_ = !*enable;
        if (sy) manualY_ = !*enable;
        sx = sx && !manualX_;
        sy = sy && !manualY_;
    }
    if (tight && *tight) {
        if (sx) marginX_ = 0.0f;
        if (sy) marginY_ = 0.0f;
    }
    if (sx || sy) {
        relim();
        // Apply without flipping the manual flags back on.
        Viewport v = dataLim_;
        bool t = tight.value_or(false);
        for (const auto& p : plots_)
            t |= p->tightAutoscale();
        auto sticky = collectStickyEdges();
        finalizeAutoscale(v, t, sticky.x, sticky.y);
        // mpl autoscale_view gates on get_autoscale*_on() — a manually
        // fixed axis is left alone when enable=None.
        bool mx = manualX_, my = manualY_;
        if (sx && !mx && v.x.min <= v.x.max) setXbound(v.x.min, v.x.max);
        if (sy && !my && v.y.min <= v.y.max) setYbound(v.y.min, v.y.max);
        manualX_ = mx; manualY_ = my;
        touch();
    }
}

void Axes::autoscale() {
    // Always iterate — the z (value) range feeds the colorbar even when
    // both spatial axes are manually fixed (mpl contourf sets the norm
    // independently of xlim/ylim).
    relim();
    Viewport v = dataLim_;
    bool tight = false;
    for (const auto& p : plots_)
        tight |= p->tightAutoscale();
    auto sticky = collectStickyEdges();
    finalizeAutoscale(v, tight, sticky.x, sticky.y);
    // mpl autoscale_view preserves the axis direction — an inverted
    // viewport (e.g. imshow origin='upper') stays inverted.
    if (!manualX_)
        viewport_.x = xAxisInverted() ? Range{v.x.max, v.x.min} : v.x;
    if (!manualY_)
        viewport_.y = yAxisInverted() ? Range{v.y.max, v.y.min} : v.y;
    if (v.z.min <= v.z.max) viewport_.z = v.z;
}

void Axes::autoscaleGpu(render::primitives::ReduceRenderer& reducer) {
    if (manualX_ && manualY_) {
        // Still collect the z range for the colorbar.
        for (const auto& p : plots_) {
            Viewport zv{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest() };
            p->contributeToAutoscaleScaled(zv, xScale_, yScale_);
            if (zv.z.min <= zv.z.max) viewport_.z = zv.z;
        }
        return;
    }
    Viewport v{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest() };
    // Each layer contributes via GPU reduce where possible, falling back to
    // CPU per-layer (the default IPlot::contributeToAutoscaleGpu behavior).
    bool tight = false;
    // Domain-limited scales (log/logit) must drop out-of-domain points,
    // which the raw GPU min/max reduce can't express — use the CPU path.
    bool clip = xScale_.clipsDomain() || yScale_.clipsDomain();
    for (const auto& p : plots_) {
        if (clip) p->contributeToAutoscaleScaled(v, xScale_, yScale_);
        else      p->contributeToAutoscaleGpu(reducer, v);
        tight |= p->tightAutoscale();
    }
    auto sticky = collectStickyEdges();
    // dataLim tracks the *raw* data limits (mpl dataLim); finalizeAutoscale
    // pads v in place, so capture the raw limits first.
    dataLim_ = v;
    finalizeAutoscale(v, tight, sticky.x, sticky.y);
    if (!manualX_)
        viewport_.x = xAxisInverted() ? Range{v.x.max, v.x.min} : v.x;
    if (!manualY_)
        viewport_.y = yAxisInverted() ? Range{v.y.max, v.y.min} : v.y;
    viewport_.z = v.z;
}

Transform2D Axes::transform() const {
    Transform2D t;
    t.scaleX = xScale_;
    t.scaleY = yScale_;
    t.projection = projection_;
    t.logX = logX();
    t.logY = logY();
    if (projection_.kind == ProjectionKind::Rectilinear) {
        // View in display space: apply each axis scale to its limits.
        t.view.x = {xScale_.forward(viewport_.x.min),
                    xScale_.forward(viewport_.x.max)};
        t.view.y = {yScale_.forward(viewport_.y.min),
                    yScale_.forward(viewport_.y.max)};
        t.view.z = viewport_.z;
    } else if (projection_.kind == ProjectionKind::Polar) {
        // Data is (theta, r); the view is the projected plane bounds.
        float rmax = std::max(std::fabs(viewport_.y.min),
                              std::fabs(viewport_.y.max));
        if (rmax <= 0.0f) rmax = 1.0f;
        t.view = projection_.bounds(rmax);
    } else {
        // Geo projections: fixed natural bounds of the projected plane.
        t.view = projection_.bounds();
    }
    return t;
}

Point2D Axes::dataToFraction(Point2D p) const {
    if (projection_.kind != ProjectionKind::Rectilinear) {
        auto pp = projection_.forward({xScale_.forward(p.x),
                                     yScale_.forward(p.y)});
        auto b = transform().view;
        float fx = b.x.span() != 0 ? (pp.x - b.x.min) / b.x.span() : 0.5f;
        float fy = b.y.span() != 0 ? (pp.y - b.y.min) / b.y.span() : 0.5f;
        return {fx, fy};
    }
    float fx = viewport_.x.span() != 0
        ? (xScale_.forward(p.x) - xScale_.forward(viewport_.x.min)) /
          (xScale_.forward(viewport_.x.max) - xScale_.forward(viewport_.x.min))
        : 0.5f;
    float fy = viewport_.y.span() != 0
        ? (yScale_.forward(p.y) - yScale_.forward(viewport_.y.min)) /
          (yScale_.forward(viewport_.y.max) - yScale_.forward(viewport_.y.min))
        : 0.5f;
    return {fx, fy};
}

Point2D Axes::fractionToData(Point2D f) const {
    // Invert scale + viewport; projection inverse is not available.
    float dx = xScale_.forward(viewport_.x.min);
    float dxs = xScale_.forward(viewport_.x.max) - dx;
    float dy = yScale_.forward(viewport_.y.min);
    float dys = yScale_.forward(viewport_.y.max) - dy;
    return {xScale_.inverse(dx + f.x * dxs),
            yScale_.inverse(dy + f.y * dys)};
}

Point2D Axes::canvasToFraction(Point2D px) const {
    float w = std::max(float(rect.width), 1.0f);
    float h = std::max(float(rect.height), 1.0f);
    return {(px.x - rect.x) / w,
            1.0f - (px.y - rect.y) / h};
}

void Axes::tickParams(std::string_view axis, std::string_view direction,
                      float majorSize, float minorSize,
                      float majorWidth, float minorWidth) {
    auto apply = [&](TickConfig& t) {
        if (!direction.empty()) t.direction = std::string(direction);
        if (majorSize >= 0.0f) t.majorSize = majorSize;
        if (minorSize >= 0.0f) t.minorSize = minorSize;
        if (majorWidth >= 0.0f) t.majorWidth = majorWidth;
        if (minorWidth >= 0.0f) t.minorWidth = minorWidth;
    };
    if (axis == "x" || axis == "both") apply(style_.xAxis.ticks);
    if (axis == "y" || axis == "both") apply(style_.yAxis.ticks);
    touch();
}

void Axes::ticklabelFormat(std::string_view axis, std::string_view style,
                           std::pair<int, int> scilimits,
                           bool useOffset, bool useMathText) {
    auto f = std::make_shared<ScalarFormatter>();
    f->scilimits = scilimits;
    f->useOffset = useOffset;
    f->useMathText = useMathText;
    if (style == "sci" || style == "scientific") {
        f->forceSci = true;
    } else if (style == "plain") {
        f->useOffset = false;
        f->scilimits = {INT32_MIN / 2, INT32_MAX / 2};
    }
    if (axis == "x" || axis == "both") style_.xAxis.ticks.formatter = f;
    if (axis == "y" || axis == "both") style_.yAxis.ticks.formatter = f;
    touch();
}

void Axes::grid(bool on, std::string_view which, std::string_view axis) {
    auto apply = [&](AxisStyle& a) {
        a.grid = on;
        a.gridWhich = std::string(which);
        if (which != "major") a.ticks.minor = true; // need minor ticks
    };
    if (axis == "x" || axis == "both") apply(style_.xAxis);
    if (axis == "y" || axis == "both") apply(style_.yAxis);
    touch();
}

// ── Units / categorical & date axes ─────────────────────────────────────────

LinePlot& Axes::plot(const std::vector<float>& x,
                     const std::vector<float>& y) {
    Series2D s;
    const size_t n = std::min(x.size(), y.size());
    s.points.reserve(n);
    for (size_t i = 0; i < n; ++i) s.points.push_back({x[i], y[i]});
    auto p = std::make_unique<LinePlot>(std::move(s));
    auto& ref = *p;
    addPlot(std::move(p));
    return ref;
}

void Axes::applyAxisInfo(const UnitConverter& conv, char axis) {
    auto info = conv.axisInfo(axis == 'y' ? "y" : "x", this);
    auto& st = axis == 'y' ? style_.yAxis : style_.xAxis;
    if (info.locator) st.ticks.locator = info.locator;
    if (info.formatter) st.ticks.formatter = info.formatter;
    if (info.minorLocator) st.ticks.minorLocator = info.minorLocator;
    if (info.minorFormatter)
        st.ticks.minorFormatter = info.minorFormatter;
    if (!info.label.empty()) st.label = info.label;
}

void Axes::xaxis_date() {
    registerBuiltinConverters();
    dates::DateConverter conv;
    applyAxisInfo(conv, 'x');
    touch();
}

void Axes::yaxis_date() {
    registerBuiltinConverters();
    dates::DateConverter conv;
    applyAxisInfo(conv, 'y');
    touch();
}

void Axes::installCategoryTicks(char axis) {
    const auto& cats = axis == 'y' ? yCategories_ : xCategories_;
    std::vector<float> locs(cats.size());
    std::iota(locs.begin(), locs.end(), 0.0f);
    auto loc = std::make_shared<FixedLocator>(std::move(locs));
    auto fmt = std::make_shared<FixedFormatter>(cats);
    if (axis == 'y') {
        style_.yAxis.ticks.locator = std::move(loc);
        style_.yAxis.ticks.formatter = std::move(fmt);
    } else {
        style_.xAxis.ticks.locator = std::move(loc);
        style_.xAxis.ticks.formatter = std::move(fmt);
    }
}

void Axes::setXCategories(std::vector<std::string> labels) {
    xCategories_ = std::move(labels);
    installCategoryTicks('x');
    touch();
}

void Axes::setYCategories(std::vector<std::string> labels) {
    yCategories_ = std::move(labels);
    installCategoryTicks('y');
    touch();
}

int Axes::xCategoryIndex(std::string_view label) {
    for (size_t i = 0; i < xCategories_.size(); ++i)
        if (xCategories_[i] == label) return int(i);
    xCategories_.emplace_back(label);
    installCategoryTicks('x');
    return int(xCategories_.size()) - 1;
}

int Axes::yCategoryIndex(std::string_view label) {
    for (size_t i = 0; i < yCategories_.size(); ++i)
        if (yCategories_[i] == label) return int(i);
    yCategories_.emplace_back(label);
    installCategoryTicks('y');
    return int(yCategories_.size()) - 1;
}


// ── mpl Axes pan/zoom navigation ──────────────────────────────────────

void Axes::startPan(float x, float y) {
    auto t = transData();
    panStart_ = PanState{viewport_, t, t->inverted(), rect, x, y};
}

void Axes::endPan() { panStart_.reset(); }

void Axes::dragPan(int button, const std::string& key, float x, float y) {
    if (!panStart_) return;
    const auto& p = *panStart_;
    float dx = x - p.x, dy = y - p.y;
    if (dx == 0 && dy == 0) return;
    // mpl _get_pan_points format_deltas.
    auto formatDeltas = [&](float ddx, float ddy)
        -> std::pair<float, float> {
        if (key == "control") {
            if (std::abs(ddx) > std::abs(ddy)) ddy = ddx;
            else ddx = ddy;
        } else if (key == "x") {
            ddy = 0;
        } else if (key == "y") {
            ddx = 0;
        } else if (key == "shift") {
            if (2 * std::abs(ddx) < std::abs(ddy)) ddx = 0;
            else if (2 * std::abs(ddy) < std::abs(ddx)) ddy = 0;
            else if (std::abs(ddx) > std::abs(ddy))
                ddy = std::copysign(std::abs(ddx), ddy);
            else ddx = std::copysign(std::abs(ddy), ddx);
        }
        return {ddx, ddy};
    };
    Point2D lo, hi;
    if (button == 1) {  // mpl BUTTON1: pan
        auto [ddx, ddy] = formatDeltas(dx, dy);
        // Our display is Y-down: the bottom pixel maps to ymin, the top
        // to ymax (mpl's Y-up bbox needs no such reordering).
        float x0 = float(p.bbox.x) - ddx;
        float yBottom = float(p.bbox.y + p.bbox.height) - ddy;
        float x1 = x0 + float(p.bbox.width);
        float yTop = yBottom - float(p.bbox.height);
        lo = p.transInverse->apply({x0, yBottom});
        hi = p.transInverse->apply({x1, yTop});
    } else if (button == 3) {  // mpl BUTTON3: zoom to point
        dx = -dx / float(p.bbox.width);
        // mpl negates dy for its Y-up display; ours is Y-down (drag up
        // → dy<0), so the sign is already correct for zoom-in-on-up.
        dy = dy / float(p.bbox.height);
        auto [ddx, ddy] = formatDeltas(dx, dy);
        if (aspect_ != AspectMode::Auto) ddx = ddy = 0.5f * (ddx + ddy);
        float ax = std::pow(10.0f, ddx), ay = std::pow(10.0f, ddy);
        auto olo = p.trans->apply({p.lim.x.min, p.lim.y.min});
        auto ohi = p.trans->apply({p.lim.x.max, p.lim.y.max});
        lo = p.transInverse->apply({p.x + ax * (olo.x - p.x),
                                   p.y + ay * (olo.y - p.y)});
        hi = p.transInverse->apply({p.x + ax * (ohi.x - p.x),
                                   p.y + ay * (ohi.y - p.y)});
    } else {
        return;
    }
    // mpl keeps the prior coordinate where the new limit is non-finite
    // (typically log-scale underflow).
    auto finite = [&](Point2D q) {
        auto r = p.trans->apply(q);
        return std::isfinite(r.x) && std::isfinite(r.y);
    };
    if (!finite(lo)) lo = {p.lim.x.min, p.lim.y.min};
    if (!finite(hi)) hi = {p.lim.x.max, p.lim.y.max};
    setXlim(lo.x, hi.x);
    setYlim(lo.y, hi.y);
}

} // namespace volcano::plot
