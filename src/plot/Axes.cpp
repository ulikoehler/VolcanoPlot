// volcano/plot/Axes.cpp
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Collections.hpp"
#include "volcano/plot/Dates.hpp"
#include "volcano/plot/Specialized.hpp"
#include "volcano/plot/plots/ReferenceLines.hpp"
#include "volcano/plot/plots/HeatmapPlot.hpp"
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Rc.hpp"
#include "volcano/plot/Ticks.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace volcano::plot {

Axes::Axes() : style_(rc::params()) {
    // Seed the prop cycle from rcParams; default to tab10 like matplotlib.
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

IPlot* Axes::addPlot(std::unique_ptr<IPlot> plot) {
    IPlot* raw = plot.get();
    if (!cycler_.empty()) {
        const auto& props = cycler_.peek();
        if (raw->applyCycleProps(props)) cycler_.advance();
    }
    plots_.push_back(std::move(plot));
    return raw;
}

Patch& Axes::addPatch(Patch p) {
    auto coll = std::make_unique<PatchCollection>(std::vector<Patch>{std::move(p)});
    auto* raw = coll.get();
    addPlot(std::move(coll));
    return raw->patches.back();
}

void Axes::setSpineVisible(std::string_view side, bool visible) {
    if (side == "left")        spines_.left = visible;
    else if (side == "right")  spines_.right = visible;
    else if (side == "bottom") spines_.bottom = visible;
    else if (side == "top")    spines_.top = visible;
    else if (side == "all")
        spines_ = {visible, visible, visible, visible};
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
HeatmapPlot& Axes::imshow(Grid2D grid, const Colormap& cmap) {
    return addOwned<HeatmapPlot>(*this, std::move(grid), cmap);
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
}

void Axes::setYscale(std::string_view name) {
    if (name == "linear")      yScale_ = AxisScale::linear();
    else if (name == "log")    yScale_ = AxisScale::log();
    else if (name == "symlog") yScale_ = AxisScale::symlog();
    else if (name == "logit")  yScale_ = AxisScale::logit();
    else if (name == "asinh")  yScale_ = AxisScale::asinh();
    else if (name == "mercator") yScale_ = AxisScale::mercator();
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
}

void Axes::shareX(Axes& other) {
    shareXWith_.push_back(&other);
    other.shareXWith_.push_back(this);
    other.viewport_.x = viewport_.x;
    other.manualX_ = manualX_;
}

void Axes::shareY(Axes& other) {
    shareYWith_.push_back(&other);
    other.shareYWith_.push_back(this);
    other.viewport_.y = viewport_.y;
    other.manualY_ = manualY_;
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
}

void Axes::secondaryYaxis(std::function<float(float)> forward,
                          std::function<float(float)> inverse,
                          std::string label) {
    secondaryY_ = SecondaryAxis{std::move(forward), std::move(inverse),
                                std::move(label), true};
}

void Axes::finalizeAutoscale(Viewport& v) {
    // Check each axis independently — a plot may only contribute to one
    // axis (e.g., AxhLine only contributes y, AxvLine only contributes x).
    if (v.x.min > v.x.max) v.x = {0,1};
    if (v.y.min > v.y.max) v.y = {0,1};
    // Handle zero-span axes (e.g., single horizontal line at one y value).
    if (v.x.span() == 0) { v.x.min -= 0.5f; v.x.max += 0.5f; }
    if (v.y.span() == 0) { v.y.min -= 0.5f; v.y.max += 0.5f; }
    // 5% padding
    float padx = v.x.span() * 0.05f;
    float pady = v.y.span() * 0.05f;
    v.x.min -= padx; v.x.max += padx;
    v.y.min -= pady; v.y.max += pady;
}

void Axes::autoscale() {
    if (manualX_ && manualY_) return;
    Viewport v{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                0, 1 };
    for (const auto& p : plots_) p->contributeToAutoscale(v);
    finalizeAutoscale(v);
    if (!manualX_) viewport_.x = v.x;
    if (!manualY_) viewport_.y = v.y;
    viewport_.z = v.z;
}

void Axes::autoscaleGpu(render::primitives::ReduceRenderer& reducer) {
    if (manualX_ && manualY_) return;
    Viewport v{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                0, 1 };
    // Each layer contributes via GPU reduce where possible, falling back to
    // CPU per-layer (the default IPlot::contributeToAutoscaleGpu behavior).
    for (const auto& p : plots_) p->contributeToAutoscaleGpu(reducer, v);
    finalizeAutoscale(v);
    if (!manualX_) viewport_.x = v.x;
    if (!manualY_) viewport_.y = v.y;
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
}

void Axes::grid(bool on, std::string_view which, std::string_view axis) {
    auto apply = [&](AxisStyle& a) {
        a.grid = on;
        a.gridWhich = std::string(which);
        if (which != "major") a.ticks.minor = true; // need minor ticks
    };
    if (axis == "x" || axis == "both") apply(style_.xAxis);
    if (axis == "y" || axis == "both") apply(style_.yAxis);
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
}

void Axes::yaxis_date() {
    registerBuiltinConverters();
    dates::DateConverter conv;
    applyAxisInfo(conv, 'y');
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
}

void Axes::setYCategories(std::vector<std::string> labels) {
    yCategories_ = std::move(labels);
    installCategoryTicks('y');
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

} // namespace volcano::plot
