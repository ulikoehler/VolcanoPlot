// volcano/plot/Plot.cpp
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Interaction.hpp"
#include "volcano/plot/Rc.hpp"
#include "volcano/plot/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace volcano::plot {

void IPlot::contributeToAutoscaleGpu(
    [[maybe_unused]] render::primitives::ReduceRenderer& reducer,
    Viewport& v) const {
    // Default: fall back to the CPU autoscale contribution.
    contributeToAutoscale(v);
}

std::pair<bool, bool> IPlot::feedsData(const Axes& axes) const {
    if (!transform) return {true, true};
    // Blended transforms feed each axis independently (mpl
    // blended_transform_factory: x/y_transform may contain transData).
    if (const Transform* xb = transform->xBranch()) {
        const Transform* yb = transform->yBranch();
        return {xb->containsDataBranch(axes),
                yb ? yb->containsDataBranch(axes) : false};
    }
    bool d = transform->containsDataBranch(axes);
    return {d, d};
}

Figure::~Figure() = default;

Figure::Figure() : grid_(std::make_shared<GridSpec>(1, 1)), style_(rc::params()) {}

Figure::Figure(uint32_t rows, uint32_t cols)
    : grid_(std::make_shared<GridSpec>(rows, cols)), style_(rc::params()) {}

Figure::Figure(std::shared_ptr<GridSpec> grid)
    : grid_(std::move(grid)), style_(rc::params()) {}

Axes* Figure::addAxes(uint32_t row, uint32_t col, uint32_t rowSpan, uint32_t colSpan) {
    return addAxes(grid_->at(row, col, rowSpan, colSpan));
}

Axes* Figure::addAxes(const SubplotSpec& spec) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    raw->setFigure(this);
    raw->setSubplotSpec(spec);
    AxesPlacement p;
    p.axes = std::move(a);
    p.spec = spec;
    p.mode = PlacementMode::Grid;
    placements_.push_back(std::move(p));
    markStale();
    return raw;
}

Axes* Figure::addAxesFraction(float l, float b, float w, float h) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    raw->setFigure(this);
    AxesPlacement p;
    p.axes = std::move(a);
    p.mode = PlacementMode::FigureFraction;
    // matplotlib uses figure fraction with origin at bottom-left; the
    // framebuffer is top-left origin so convert y.
    p.fx = l; p.fy = b; p.fw = w; p.fh = h;
    placements_.push_back(std::move(p));
    markStale();
    return raw;
}

Axes* Figure::subplot2grid(std::pair<uint32_t, uint32_t> shape,
                           std::pair<uint32_t, uint32_t> loc,
                           uint32_t rowSpan, uint32_t colSpan) {
    if (grid_->rows() != shape.first || grid_->cols() != shape.second) {
        retiredGrids_.push_back(grid_);
        grid_ = std::make_shared<GridSpec>(shape.first, shape.second);
    }
    return addAxes(loc.first, loc.second, rowSpan, colSpan);
}

std::map<std::string, Axes*>
Figure::subplotMosaic(const std::vector<std::vector<std::string>>& layout) {
    if (layout.empty()) return {};
    uint32_t rows = static_cast<uint32_t>(layout.size());
    uint32_t cols = 0;
    for (const auto& row : layout)
        cols = std::max(cols, static_cast<uint32_t>(row.size()));
    retiredGrids_.push_back(grid_);
    grid_ = std::make_shared<GridSpec>(rows, cols);

    // Find the bounding box of each label.
    struct Ext { uint32_t r0 = ~0u, r1 = 0, c0 = ~0u, c1 = 0; };
    std::map<std::string, Ext> boxes;
    std::vector<std::string> order;
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < layout[r].size(); ++c) {
            const auto& label = layout[r][c];
            if (label.empty() || label == ".") continue;
            auto& e = boxes[label];
            if (e.r0 == ~0u) order.push_back(label);
            e.r0 = std::min(e.r0, r); e.r1 = std::max(e.r1, r);
            e.c0 = std::min(e.c0, c); e.c1 = std::max(e.c1, c);
        }
    }
    std::map<std::string, Axes*> out;
    for (const auto& label : order) {
        const auto& e = boxes[label];
        out[label] = addAxes(e.r0, e.c0, e.r1 - e.r0 + 1, e.c1 - e.c0 + 1);
    }
    return out;
}

Figure* Figure::addSubfigure(const SubplotSpec& spec,
                             uint32_t rows, uint32_t cols) {
    auto f = std::make_unique<Figure>(rows, cols);
    Figure* raw = f.get();
    subfigs_.push_back({std::move(f), spec});
    return raw;
}

std::vector<Figure*> Figure::subfigures(uint32_t rows, uint32_t cols) {
    retiredGrids_.push_back(grid_);
    grid_ = std::make_shared<GridSpec>(rows, cols);
    std::vector<Figure*> out;
    for (uint32_t r = 0; r < rows; ++r)
        for (uint32_t c = 0; c < cols; ++c)
            out.push_back(addSubfigure(grid_->at(r, c)));
    return out;
}

Axes* Figure::twinx(Axes& parent) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    raw->setFigure(this);
    // The twin adopts the parent's x range (parent.shareX copies its
    // range + manual flag into the twin).
    parent.shareX(*raw);
    raw->setYTicksRight(true);
    // matplotlib twin axes have an invisible patch so the parent's
    // artists stay visible underneath the overlay.
    raw->style().faceColor = Color::transparent();
    AxesPlacement p;
    p.axes = std::move(a);
    p.mode = PlacementMode::Overlay;
    p.relTo = &parent;
    placements_.push_back(std::move(p));
    return raw;
}

Axes* Figure::twiny(Axes& parent) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    raw->setFigure(this);
    parent.shareY(*raw);
    raw->setXTicksTop(true);
    raw->style().faceColor = Color::transparent();
    AxesPlacement p;
    p.axes = std::move(a);
    p.mode = PlacementMode::Overlay;
    p.relTo = &parent;
    placements_.push_back(std::move(p));
    return raw;
}

Axes* Figure::insetAxes(Axes& parent, float x, float y, float w, float h) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    raw->setFigure(this);
    AxesPlacement p;
    p.axes = std::move(a);
    p.mode = PlacementMode::Inset;
    p.relTo = &parent;
    p.fx = x; p.fy = y; p.fw = w; p.fh = h;
    placements_.push_back(std::move(p));
    return raw;
}

Axes* Figure::appendAxes(Axes& parent, Side side, float size, float pad) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    raw->setFigure(this);
    AxesPlacement p;
    p.axes = std::move(a);
    p.mode = PlacementMode::Located;
    p.relTo = &parent;
    p.side = side;
    p.locatedSize = size;
    p.locatedPad = pad;
    placements_.push_back(std::move(p));
    return raw;
}

void Figure::subplotsAdjust(float left, float bottom, float right, float top,
                            float wspace, float hspace) {
    grid_->left = left;
    grid_->bottom = bottom;
    grid_->right = right;
    grid_->top = top;
    grid_->wspace = wspace;
    grid_->hspace = hspace;
    markStale();
}

void Figure::syncSharedAxes() {
    // Iteratively propagate shared ranges until stable (handles chains).
    // When exactly one side of a share pair has manual limits, those win;
    // otherwise the union is used (like matplotlib's shared autoscale).
    auto syncRange = [](Axes& a, Axes& b, Range Viewport::* axis,
                        bool aManual, bool bManual) {
        Range& ra = a.viewport().*axis;
        Range& rb = b.viewport().*axis;
        if (ra.min == rb.min && ra.max == rb.max) return false;
        if (aManual && !bManual)      rb = ra;
        else if (bManual && !aManual) ra = rb;
        else {
            Range u{std::min(ra.min, rb.min), std::max(ra.max, rb.max)};
            ra = u; rb = u;
        }
        return true;
    };
    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;
        for (auto& p : placements_) {
            for (Axes* other : p.axes->sharedX())
                changed |= syncRange(*p.axes, *other, &Viewport::x,
                                     p.axes->manualX(), other->manualX());
            for (Axes* other : p.axes->sharedY())
                changed |= syncRange(*p.axes, *other, &Viewport::y,
                                     p.axes->manualY(), other->manualY());
        }
        if (!changed) break;
    }
}

void Figure::computeTightMargins(Extent2D extent) {
    // Estimate required margins from axis decoration presence.
    // Units: figure fractions.
    float needLeft = 0.02f, needRight = 0.02f;
    float needBottom = 0.02f, needTop = 0.02f;
    for (const auto& p : placements_) {
        const auto& st = p.axes->style();
        float fontPx = st.fontSize * st.dpi / 72.0f;
        // Left: tick labels (~40px) + tick length + optional ylabel.
        float left = (4.0f + 4.0f + 40.0f) / extent.width;
        if (st.yAxis.visible && !st.yAxis.label.empty())
            left += (fontPx + 8.0f) / extent.width;
        needLeft = std::max(needLeft, left);
        // Bottom: tick labels (~16px) + ticks + optional xlabel.
        float bottom = (4.0f + 4.0f + 16.0f) / extent.height;
        if (st.xAxis.visible && !st.xAxis.label.empty())
            bottom += (fontPx + 8.0f) / extent.height;
        needBottom = std::max(needBottom, bottom);
        // Top: axes title (its own font size, mpl titlesize='large').
        if (!st.title.text.empty())
            needTop = std::max(needTop,
                (st.title.font.size * st.dpi / 72.0f + st.title.pad +
                 6.0f) / extent.height);
        // Right: colorbar (strip + gap + tick labels, ~72px for labels).
        // fraction/pad reserve (fraction + pad) of the axes width; the
        // strip itself narrows to height/aspect, so ~72px covers labels.
        if (st.colorbar.visible) {
            if (st.colorbar.orientation == "horizontal")
                needBottom = std::max(needBottom,
                    st.colorbar.fraction + st.colorbar.pad +
                        28.0f / extent.height);
            else
                needRight = std::max(needRight,
                    st.colorbar.fraction + st.colorbar.pad +
                        72.0f / extent.width);
        }
        // Right-side y ticks (twinx) or a secondary axis need margin.
        if ((p.axes->yTicksRight() || p.axes->secondaryY()) &&
            st.yAxis.visible)
            needRight = std::max(needRight,
                (4.0f + 4.0f + 40.0f) / extent.width);
        // mpl yaxis.set_label_position('right'): the ylabel needs right
        // margin even when the tick labels stay on the left.
        if (p.axes->yFurniture().labelFar && !st.yAxis.label.empty())
            needRight += (fontPx + 8.0f) / extent.width;
        if (p.axes->xTicksTop() && st.xAxis.visible)
            needTop = std::max(needTop, (4.0f + 4.0f + 16.0f) / extent.height);
        // mpl xaxis.set_label_position('top'): the xlabel needs top
        // margin alongside the title.
        if (p.axes->xFurniture().labelFar && !st.xAxis.label.empty())
            needTop += (fontPx + 8.0f) / extent.height;
    }
    // Figure suptitle (figure.titlesize='large', drawn at the top edge).
    if (!style_.title.text.empty())
        needTop += (style_.title.font.size * style_.dpi / 72.0f +
                    style_.title.pad + 4.0f) / extent.height;

    // mpl TightLayoutEngine `pad` scales all decoration margins.
    needLeft *= tightPadScale; needRight *= tightPadScale;
    needBottom *= tightPadScale; needTop *= tightPadScale;
    grid_->left = needLeft;
    grid_->right = 1.0f - needRight;
    grid_->bottom = needBottom;
    grid_->top = 1.0f - needTop;
    // mpl TightLayoutEngine `rect`: the subplot region must fit inside
    // the given (left, bottom, right, top) figure-fraction bounds.
    if (tightRect) {
        grid_->left = std::max(grid_->left, (*tightRect)[0]);
        grid_->bottom = std::max(grid_->bottom, (*tightRect)[1]);
        grid_->right = std::min(grid_->right, (*tightRect)[2]);
        grid_->top = std::min(grid_->top, (*tightRect)[3]);
    }
    if (tightLayout_) {
        // mpl TightLayoutEngine also expands inter-axes spacing so
        // titles/labels between subplots fit: reserve a row gap covering
        // the lower axes' title + the upper axes' x decorations, and a
        // column gap for y decorations between columns.
        float rowGapPx = 0.0f, colGapPx = 0.0f;
        uint32_t nrows = 1, ncols = 1;
        for (const auto& p : placements_) {
            if (p.mode != PlacementMode::Grid || !p.spec.grid) continue;
            nrows = std::max(nrows, p.spec.grid->rows());
            ncols = std::max(ncols, p.spec.grid->cols());
            const auto& st = p.axes->style();
            float fontPx = st.fontSize * st.dpi / 72.0f;
            float v = 12.0f + 24.0f;  // pad + x tick labels
            if (st.xAxis.visible && !st.xAxis.label.empty())
                v += fontPx + 8.0f;
            if (!st.title.text.empty())
                v += st.title.font.size * st.dpi / 72.0f +
                     st.title.pad + 6.0f;
            rowGapPx = std::max(rowGapPx, v);
            float h = 8.0f;
            if (st.yAxis.visible && !st.yAxis.label.empty())
                h += fontPx + 6.0f;
            colGapPx = std::max(colGapPx, h);
        }
        if (nrows > 1) {
            float axesHPx =
                (grid_->top - grid_->bottom) / float(nrows) *
                extent.height;
            if (axesHPx > 0.0f)
                grid_->hspace = std::max(grid_->hspace,
                                         rowGapPx / axesHPx);
        }
        if (ncols > 1) {
            float axesWPx =
                (grid_->right - grid_->left) / float(ncols) *
                extent.width;
            if (axesWPx > 0.0f)
                grid_->wspace = std::max(grid_->wspace,
                                         colGapPx / axesWPx);
        }
    }
    if (constrainedLayout_) {
        // Constrained layout additionally enforces inter-axes padding for
        // decorations between subplots.
        grid_->wspace = std::max(grid_->wspace, constrainedWSpace);
        grid_->hspace = std::max(grid_->hspace, constrainedHSpace);
    }
}

std::vector<const AxesPlacement*> Figure::axesDrawOrder() const {
    std::vector<const AxesPlacement*> out;
    out.reserve(placements_.size());
    for (const auto& p : placements_) out.push_back(&p);
    std::ranges::stable_sort(out, [](const AxesPlacement* a,
                                     const AxesPlacement* b) {
        return a->axes->zorder() < b->axes->zorder();
    });
    return out;
}

void Figure::applyAspect() {
    for (auto& p : placements_) {
        // Axes::applyAspect covers aspect='equal' (box or datalim
        // adjustable) and an explicit box_aspect; both early-out when
        // neither applies.
        p.axes->applyAspect();
    }
}

void Figure::layout(Extent2D extent) {
    layoutInRect(Rect2D{0, 0, extent.width, extent.height});
}

void Figure::layoutInRect(Rect2D rect) {
    figRect_ = rect;
    // Propagate shared axis ranges before computing rects.
    syncSharedAxes();

    Rect2D fig{0, 0, rect.width, rect.height};
    fig.x = rect.x; fig.y = rect.y;

    // Resolve this figure's grid region inside `rect`.
    // For a subfigure, `rect` is the cell rect; for the top figure it's the
    // whole canvas. grid_->region applies left/right/bottom/top margins.
    if (tightLayout_ || constrainedLayout_)
        computeTightMargins(Extent2D{rect.width, rect.height});
    // matplotlib draws the axes title inside the top margin — no shrink.

    Rect2D region = grid_->region(fig);

    // First pass: grid + figure-fraction placements.
    for (auto& p : placements_) {
        switch (p.mode) {
        case PlacementMode::Grid:
            p.axes->rect = p.spec.grid
                ? p.spec.grid->cellRect(p.spec.grid->resolveRegion(fig),
                                        p.spec)
                : region;
            break;
        case PlacementMode::FigureFraction:
            p.axes->rect = Rect2D{
                fig.x + static_cast<int32_t>(p.fx * fig.width),
                fig.y + static_cast<int32_t>((1.0f - p.fy - p.fh) * fig.height),
                static_cast<uint32_t>(p.fw * fig.width),
                static_cast<uint32_t>(p.fh * fig.height)};
            break;
        default:
            break; // handled in second pass
        }
        // matplotlib shrinks the axes to make room for the colorbar
        // (strip + padding + tick labels live in the reclaimed space).
        const auto& cbs = p.axes->style().colorbar;
        p.axes->setColorbarRegion({});
        if (cbs.visible && !cbs.caxMode &&
            (p.mode == PlacementMode::Grid ||
             p.mode == PlacementMode::FigureFraction)) {
            if (cbs.orientation == "horizontal") {
                // mpl make_axes horizontal: the parent keeps the top
                // (1 - fraction - pad) of its box; the colorbar region
                // is the bottom `fraction` slice.
                float origH = float(p.axes->rect.height);
                float reserve = origH * (cbs.fraction + cbs.pad);
                if (reserve < origH) {
                    float cby = p.axes->rect.y +
                                origH * (1.0f - cbs.fraction);
                    p.axes->setColorbarRegion(Rect2D{
                        p.axes->rect.x, static_cast<int32_t>(cby),
                        p.axes->rect.width,
                        static_cast<uint32_t>(origH * cbs.fraction)});
                    p.axes->rect.height -= static_cast<uint32_t>(reserve);
                }
            } else {
                // mpl make_axes: the parent keeps the left
                // (1 - fraction - pad) of its original box; the colorbar
                // region is the right `fraction` slice.
                float origW = float(p.axes->rect.width);
                float reserve = origW * (cbs.fraction + cbs.pad);
                if (reserve < origW) {
                    float cbx = p.axes->rect.x + origW * (1.0f - cbs.fraction);
                    p.axes->setColorbarRegion(Rect2D{
                        static_cast<int32_t>(cbx), p.axes->rect.y,
                        static_cast<uint32_t>(origW * cbs.fraction),
                        p.axes->rect.height});
                    p.axes->rect.width -= static_cast<uint32_t>(reserve);
                }
            }
        }
    }

    // Aspect-ratio adjustment on grid rects (before dependent placements).
    applyAspect();

    // Second pass: placements relative to a parent axes.
    for (auto& p : placements_) {
        if (!p.relTo) continue;
        const Rect2D pr = p.relTo->rect;
        switch (p.mode) {
        case PlacementMode::Overlay:
            p.axes->rect = pr;
            break;
        case PlacementMode::Inset:
            p.axes->rect = Rect2D{
                pr.x + static_cast<int32_t>(p.fx * pr.width),
                pr.y + static_cast<int32_t>((1.0f - p.fy - p.fh) * pr.height),
                static_cast<uint32_t>(p.fw * pr.width),
                static_cast<uint32_t>(p.fh * pr.height)};
            break;
        case PlacementMode::Located: {
            float padPxX = p.locatedPad * rect.width;
            float padPxY = p.locatedPad * rect.height;
            Rect2D r = pr;
            switch (p.side) {
            case Side::Right:
                r.x = pr.x + static_cast<int32_t>(pr.width + padPxX);
                r.width = static_cast<uint32_t>(p.locatedSize * pr.width);
                break;
            case Side::Left:
                r.width = static_cast<uint32_t>(p.locatedSize * pr.width);
                r.x = pr.x - static_cast<int32_t>(r.width + padPxX);
                break;
            case Side::Top:
                r.height = static_cast<uint32_t>(p.locatedSize * pr.height);
                r.y = pr.y - static_cast<int32_t>(r.height + padPxY);
                break;
            case Side::Bottom:
                r.y = pr.y + static_cast<int32_t>(pr.height + padPxY);
                r.height = static_cast<uint32_t>(p.locatedSize * pr.height);
                break;
            }
            p.axes->rect = r;
            break;
        }
        default:
            break;
        }
    }

    // Recurse into subfigures: each gets its cell rect.
    for (auto& s : subfigs_) {
        Rect2D cell = s.spec.grid ? s.spec.grid->cellRect(region, s.spec)
                                  : region;
        // A subfigure occupies the full cell (its own grid applies margins
        // relative to that rect).
        auto sub = s.figure.get();
        auto* g = &sub->grid();
        float sl = g->left, sr = g->right, sb = g->bottom, st = g->top;
        g->left = 0.0f; g->right = 1.0f; g->bottom = 0.0f; g->top = 1.0f;
        sub->layoutInRect(cell);
        g->left = sl; g->right = sr; g->bottom = sb; g->top = st;
    }
}

// ---------------------------------------------------------------- Interaction

std::vector<Axes*> Figure::allAxes() {
    std::vector<Axes*> out;
    for (auto& p : placements_) out.push_back(p.axes.get());
    for (auto& s : subfigs_)
        for (auto* ax : s.figure->allAxes()) out.push_back(ax);
    return out;
}

std::vector<const Axes*> Figure::allAxes() const {
    std::vector<const Axes*> out;
    for (auto& p : placements_) out.push_back(p.axes.get());
    for (auto& s : subfigs_)
        for (auto* ax : s.figure->allAxes()) out.push_back(ax);
    return out;
}

Axes* Figure::attachAxes(std::unique_ptr<Axes> ax) {
    Axes* raw = ax.get();
    raw->setFigure(this);
    AxesPlacement p;
    p.axes = std::move(ax);
    if (raw->subplotSpec()) {
        p.spec = *raw->subplotSpec();
        p.mode = PlacementMode::Grid;
    } else {
        p.mode = PlacementMode::FigureFraction;
        p.fx = 0.0f; p.fy = 0.0f; p.fw = 1.0f; p.fh = 1.0f;
    }
    placements_.push_back(std::move(p));
    markStale();
    return raw;
}

void Figure::setAxesFraction(Axes* ax, float l, float b, float w,
                             float h) {
    for (auto& p : placements_) {
        if (p.axes.get() != ax) continue;
        p.mode = PlacementMode::FigureFraction;
        p.fx = l; p.fy = b; p.fw = w; p.fh = h;
        p.spec = SubplotSpec{};
        ax->clearSubplotSpec();
        markStale();
        return;
    }
}

void Figure::setAxesSubplotSpec(Axes* ax, const SubplotSpec& spec) {
    for (auto& p : placements_) {
        if (p.axes.get() != ax) continue;
        p.mode = PlacementMode::Grid;
        p.spec = spec;
        ax->setSubplotSpec(spec);
        markStale();
        return;
    }
}

TextAnnotation& Figure::text(float x, float y, std::string s) {
    TextAnnotation t;
    t.x = x;
    t.y = y;
    t.coords = CoordSystem::Figure;
    t.text = std::move(s);
    figTexts_.push_back(std::move(t));
    markStale();
    return figTexts_.back();
}

void Figure::clear() {
    // mpl Figure.clear(): drop all axes, subfigures, figure artists,
    // legends and sup-labels; keep dpi/facecolor/style.
    placements_.clear();
    subfigs_.clear();
    retiredGrids_.clear();
    figTexts_.clear();
    figLegend_.visible = false;
    style_.title.text.clear();
    supXlabel_.clear();
    supYlabel_.clear();
    alignXLabels_ = alignYLabels_ = false;
    tightLayout_ = false;
    constrainedLayout_ = false;
    grid_ = std::make_shared<GridSpec>(1, 1);
    markStale();
}

std::unique_ptr<Axes> Figure::removeAxes(const Axes* ax) {
    for (auto it = placements_.begin(); it != placements_.end(); ++it) {
        if (it->axes.get() == ax) {
            auto owned = std::move(it->axes);
            placements_.erase(it);
            markStale();
            return owned;
        }
    }
    for (auto& s : subfigs_)
        if (auto owned = s.figure->removeAxes(ax)) return owned;
    return nullptr;
}

bool Figure::stale() const {
    if (stale_) return true;
    for (const auto* ax : allAxes())
        if (ax->stale()) return true;
    return false;
}

void Figure::setStale(bool v) {
    stale_ = v;
    for (auto* ax : allAxes()) ax->setStale(v);
}

Navigation& Figure::nav() {
    if (!nav_) nav_ = std::make_unique<Navigation>(*this);
    return *nav_;
}

Axes* Figure::axesAt(float x, float y) {
    // Topmost axes wins: later placements render on top.
    for (auto it = placements_.rbegin(); it != placements_.rend(); ++it) {
        auto& r = it->axes->rect;
        if (x >= r.x && x <= r.x + r.width &&
            y >= r.y && y <= r.y + r.height)
            return it->axes.get();
    }
    for (auto it = subfigs_.rbegin(); it != subfigs_.rend(); ++it)
        if (auto* ax = (*it).figure->axesAt(x, y)) return ax;
    return nullptr;
}

void Figure::dispatch(Event e) {
    // Fill hit-test fields for mouse-like events.
    if (e.type == Event::Type::ButtonPress ||
        e.type == Event::Type::ButtonRelease ||
        e.type == Event::Type::MotionNotify ||
        e.type == Event::Type::Scroll) {
        e.inaxes = axesAt(e.x, e.y);
        if (e.inaxes)
            e.dataPos = e.inaxes->fractionToData(
                e.inaxes->canvasToFraction({e.x, e.y}));
    }
    // Canvas subscribers first (mpl order), then legend dragging, then
    // widgets (topmost first — later widgets draw on top), then the
    // navigation controller.
    canvas_.emit(e);
    if (legendDragEvent(e)) return;
    if (artistDragEvent(e)) return;
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it)
        if ((*it)->handleEvent(e)) return;
    if (nav_) nav_->handleEvent(e);
}

bool Figure::legendDragEvent(const Event& e) {
    if (e.type == Event::Type::ButtonPress && e.button == 1) {
        // Topmost axes first (later placements render on top).
        auto tryAxes = [&](Axes* ax) -> bool {
            return ax && ax->style().legend.visible &&
                   ax->style().legend.draggable &&
                   ax->legendContains(e.x, e.y);
        };
        for (auto it = placements_.rbegin(); it != placements_.rend(); ++it)
            if (tryAxes(it->axes.get())) {
                legendDragAxes_ = it->axes.get();
                legendDragLast_ = {e.x, e.y};
                return true;
            }
        for (auto it = subfigs_.rbegin(); it != subfigs_.rend(); ++it)
            for (auto* ax : (*it).figure->allAxes())
                if (tryAxes(ax)) {
                    legendDragAxes_ = ax;
                    legendDragLast_ = {e.x, e.y};
                    return true;
                }
        return false;
    }
    if (!legendDragAxes_) return false;
    if (e.type == Event::Type::ButtonRelease && e.button == 1) {
        legendDragAxes_ = nullptr;
        return true;
    }
    if (e.type == Event::Type::MotionNotify) {
        auto& lg = legendDragAxes_->style().legend;
        lg.dragOffset.x += e.x - legendDragLast_.x;
        lg.dragOffset.y += e.y - legendDragLast_.y;
        legendDragLast_ = {e.x, e.y};
        return true;
    }
    return false;
}

bool Figure::artistDragEvent(const Event& e) {
    if (e.type == Event::Type::ButtonPress && e.button == 1) {
        auto hit = [](Rect2D r, float x, float y) {
            return r.width > 0 && x >= float(r.x) &&
                   x <= float(r.x) + float(r.width) && y >= float(r.y) &&
                   y <= float(r.y) + float(r.height);
        };
        // Topmost artist first: annotations then texts, both in reverse
        // draw order; axes in reverse placement order.
        auto tryAxes = [&](Axes* ax) -> Point2D* {
            if (!ax) return nullptr;
            for (auto it = ax->annotations().rbegin();
                 it != ax->annotations().rend(); ++it)
                if (it->draggable && !it->detached &&
                    hit(it->drawBox, e.x, e.y))
                    return &it->dragOffset;
            for (auto it = ax->texts().rbegin(); it != ax->texts().rend();
                 ++it)
                if (it->draggable && !it->detached &&
                    hit(it->drawBox, e.x, e.y))
                    return &it->dragOffset;
            return nullptr;
        };
        for (auto it = placements_.rbegin(); it != placements_.rend(); ++it)
            if (auto* off = tryAxes(it->axes.get())) {
                artistDrag_ = off;
                artistDragLast_ = {e.x, e.y};
                return true;
            }
        for (auto it = subfigs_.rbegin(); it != subfigs_.rend(); ++it)
            for (auto* ax : (*it).figure->allAxes())
                if (auto* off = tryAxes(ax)) {
                    artistDrag_ = off;
                    artistDragLast_ = {e.x, e.y};
                    return true;
                }
        return false;
    }
    if (!artistDrag_) return false;
    if (e.type == Event::Type::ButtonRelease && e.button == 1) {
        artistDrag_ = nullptr;
        return true;
    }
    if (e.type == Event::Type::MotionNotify) {
        artistDrag_->x += e.x - artistDragLast_.x;
        artistDrag_->y += e.y - artistDragLast_.y;
        artistDragLast_ = {e.x, e.y};
        return true;
    }
    return false;
}

TransformPtr Figure::transFigure() const {
    return plot::transFigure(*this);
}

} // namespace volcano::plot
