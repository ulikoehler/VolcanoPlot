// volcano/plot/Widgets.cpp — interactive widget implementations
#include "volcano/plot/Widgets.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Plot.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace volcano::plot {

namespace {
constexpr Color kBg{0.92f, 0.92f, 0.92f, 1.0f};
constexpr Color kFg{0.2f, 0.2f, 0.2f, 1.0f};
constexpr Color kAccent{0.3f, 0.5f, 0.9f, 0.8f};
constexpr Color kSelect{0.3f, 0.5f, 0.9f, 0.25f};

bool isPress(const Event& e) { return e.type == Event::Type::ButtonPress && e.button == 1; }
bool isRelease(const Event& e) { return e.type == Event::Type::ButtonRelease && e.button == 1; }
bool isMotion(const Event& e) { return e.type == Event::Type::MotionNotify; }
} // namespace

// ---------------------------------------------------------------- Slider
Slider::Slider(Rect2D r, std::string lbl, float vmin, float vmax,
               float vinit, int steps)
    : valstep(steps > 0 ? (vmax - vmin) / float(steps) : 0.0f),
      label(std::move(lbl)), valmin(vmin), valmax(vmax), val_(vinit) {
    rect = r;
}

float Slider::handleX() const {
    float f = (valmax > valmin) ? (val_ - valmin) / (valmax - valmin) : 0.0f;
    return rect.x + std::clamp(f, 0.0f, 1.0f) * rect.width;
}

void Slider::setFromX(float px) {
    float f = rect.width > 0 ? (px - rect.x) / rect.width : 0.0f;
    float v = valmin + std::clamp(f, 0.0f, 1.0f) * (valmax - valmin);
    if (valstep > 0) v = std::round(v / valstep) * valstep;
    setVal(std::clamp(v, valmin, valmax));
}

void Slider::setVal(float v) {
    if (v == val_) return;
    val_ = v;
    if (onChanged) onChanged(val_);
}

bool Slider::handleEvent(const Event& e) {
    if (!active) return false;
    if (isPress(e) && hitTest(e.x, e.y)) {
        dragging_ = true;
        setFromX(e.x);
        return true;
    }
    if (isMotion(e) && (e.buttons & 1) && dragging_) {
        setFromX(e.x);
        return true;
    }
    if (isRelease(e)) dragging_ = false;
    return false;
}

void Slider::draw(WidgetPainter& p) {
    if (!visible) return;
    p.fillRect(rect, kBg);
    p.outlineRect(rect, kFg);
    float cy = rect.y + rect.height * 0.5f;
    Point2D track[2] = {{float(rect.x) + 6, cy},
                        {float(rect.x + rect.width) - 6, cy}};
    p.line(track, kFg, 2.0f);
    float hx = std::clamp(handleX(), float(rect.x) + 6,
                          float(rect.x + rect.width) - 6);
    Point2D handle[2] = {{hx, float(rect.y) + 4},
                        {hx, float(rect.y + rect.height) - 4}};
    p.line(handle, kAccent, 3.0f);
    p.text(std::format("{} {:.3g}", label, val_), rect.x + 6, rect.y - 2, kFg, 0.7f);
}

// ---------------------------------------------------------------- RangeSlider
RangeSlider::RangeSlider(Rect2D r, std::string lbl, float vmin, float vmax,
                         float lo, float hi)
    : valmin(vmin), valmax(vmax), label(std::move(lbl)), lo_(lo), hi_(hi) {
    rect = r;
}

float RangeSlider::toX(float v) const {
    float f = (valmax > valmin) ? (v - valmin) / (valmax - valmin) : 0.0f;
    return rect.x + std::clamp(f, 0.0f, 1.0f) * rect.width;
}

void RangeSlider::setFromX(float px, int which) {
    float f = rect.width > 0 ? (px - rect.x) / rect.width : 0.0f;
    float v = valmin + std::clamp(f, 0.0f, 1.0f) * (valmax - valmin);
    if (which == 0) lo_ = std::min(v, hi_); else hi_ = std::max(v, lo_);
    if (onChanged) onChanged(lo_, hi_);
}

bool RangeSlider::handleEvent(const Event& e) {
    if (!active) return false;
    if (isPress(e) && hitTest(e.x, e.y)) {
        dragHandle_ = std::abs(e.x - toX(lo_)) < std::abs(e.x - toX(hi_)) ? 0 : 1;
        setFromX(e.x, dragHandle_);
        return true;
    }
    if (isMotion(e) && (e.buttons & 1) && dragHandle_ >= 0) {
        setFromX(e.x, dragHandle_);
        return true;
    }
    if (isRelease(e)) dragHandle_ = -1;
    return false;
}

void RangeSlider::draw(WidgetPainter& p) {
    if (!visible) return;
    p.fillRect(rect, kBg);
    p.outlineRect(rect, kFg);
    float cy = rect.y + rect.height * 0.5f;
    Point2D track[2] = {{float(rect.x) + 6, cy},
                        {float(rect.x + rect.width) - 6, cy}};
    p.line(track, kFg, 2.0f);
    float x0 = std::clamp(toX(lo_), float(rect.x) + 6,
                          float(rect.x + rect.width) - 6);
    float x1 = std::clamp(toX(hi_), float(rect.x) + 6,
                          float(rect.x + rect.width) - 6);
    Point2D span[2] = {{x0, cy}, {x1, cy}};
    p.line(span, kAccent, 4.0f);
    for (float hx : {x0, x1}) {
        Point2D h[2] = {{hx, float(rect.y) + 4},
                        {hx, float(rect.y + rect.height) - 4}};
        p.line(h, kAccent, 3.0f);
    }
    p.text(std::format("{} {:.3g}–{:.3g}", label, lo_, hi_),
           rect.x + 6, rect.y - 2, kFg, 0.7f);
}

// ---------------------------------------------------------------- Button
Button::Button(Rect2D r, std::string lbl) : label(std::move(lbl)) { rect = r; }

bool Button::handleEvent(const Event& e) {
    if (!active) return false;
    if (isPress(e) && hitTest(e.x, e.y)) { pressed_ = true; return true; }
    if (isRelease(e)) {
        bool was = pressed_;
        pressed_ = false;
        if (was && hitTest(e.x, e.y)) {
            if (onClick) onClick();
            return true;
        }
    }
    return false;
}

void Button::draw(WidgetPainter& p) {
    if (!visible) return;
    p.fillRect(rect, pressed_ ? kAccent : kBg);
    p.outlineRect(rect, kFg);
    auto m = p.measure(label, 0.8f);
    p.text(label, rect.x + (rect.width - m.width) * 0.5f,
           rect.y + (rect.height - m.height) * 0.5f + m.ascent * 0.5f,
           kFg, 0.8f);
}

// ---------------------------------------------------------------- CheckButtons
CheckButtons::CheckButtons(Rect2D r, std::vector<std::string> lbls,
                           std::vector<bool> checked)
    : labels(std::move(lbls)),
      checked_(std::move(checked)) {
    rect = r;
    checked_.resize(labels.size(), false);
}

float CheckButtons::rowH() const {
    return labels.empty() ? 0.0f : rect.height / float(labels.size());
}

bool CheckButtons::handleEvent(const Event& e) {
    if (!active || !isPress(e) || !hitTest(e.x, e.y)) return false;
    size_t i = size_t((e.y - rect.y) / rowH());
    if (i < labels.size()) {
        checked_[i] = !checked_[i];
        if (onChanged) onChanged(i, checked_[i]);
    }
    return true;
}

void CheckButtons::draw(WidgetPainter& p) {
    if (!visible) return;
    p.fillRect(rect, kBg);
    float rh = rowH();
    for (size_t i = 0; i < labels.size(); ++i) {
        float y = rect.y + float(i) * rh;
        Rect2D box{rect.x + 4, int32_t(y + rh * 0.5f - 5), 10, 10};
        p.outlineRect(box, kFg);
        if (checked_[i]) p.fillRect({box.x + 2, box.y + 2, box.width - 4, box.height - 4}, kAccent);
        p.text(labels[i], rect.x + 20, y + rh * 0.5f + 4, kFg, 0.7f);
    }
}

// ---------------------------------------------------------------- RadioButtons
RadioButtons::RadioButtons(Rect2D r, std::vector<std::string> lbls, int active)
    : labels(std::move(lbls)), active_(active) { rect = r; }

float RadioButtons::rowH() const {
    return labels.empty() ? 0.0f : rect.height / float(labels.size());
}

bool RadioButtons::handleEvent(const Event& e) {
    if (!active || !isPress(e) || !hitTest(e.x, e.y)) return false;
    size_t i = size_t((e.y - rect.y) / rowH());
    if (i < labels.size()) {
        active_ = int(i);
        if (onChanged) onChanged(i);
    }
    return true;
}

void RadioButtons::draw(WidgetPainter& p) {
    if (!visible) return;
    p.fillRect(rect, kBg);
    float rh = rowH();
    for (size_t i = 0; i < labels.size(); ++i) {
        float y = rect.y + float(i) * rh;
        Rect2D box{rect.x + 4, int32_t(y + rh * 0.5f - 5), 10, 10};
        p.outlineRect(box, kFg);
        if (int(i) == active_)
            p.fillRect({box.x + 2, box.y + 2, box.width - 4, box.height - 4}, kAccent);
        p.text(labels[i], rect.x + 20, y + rh * 0.5f + 4, kFg, 0.7f);
    }
}

// ---------------------------------------------------------------- TextBox
TextBox::TextBox(Rect2D r, std::string lbl, std::string initial)
    : label(std::move(lbl)), text_(std::move(initial)) { rect = r; }

bool TextBox::handleEvent(const Event& e) {
    if (!active) return false;
    if (isPress(e)) {
        focused_ = hitTest(e.x, e.y);
        return focused_;
    }
    if (!focused_) return false;
    if (e.type == Event::Type::KeyPress) {
        if (e.key == "\r" || e.key == "\n" || e.key == "Enter" ||
            e.key == "enter") {
            if (onSubmit) onSubmit(text_);
            return true;
        }
        if (e.key == "Escape" || e.key == "escape") {
            focused_ = false;
            return true;
        }
        if (e.key == "Backspace" || e.key == "backspace" ||
            e.key == "\x7f") {
            if (!text_.empty()) text_.pop_back();
            return true;
        }
        // Printable single characters (SDL sends keycodes; test events use
        // single-char strings).
        if (e.key.size() == 1 && std::isprint(e.key[0])) {
            text_ += e.key[0];
            return true;
        }
    }
    return false;
}

void TextBox::draw(WidgetPainter& p) {
    if (!visible) return;
    p.fillRect(rect, kBg);
    p.outlineRect(rect, focused_ ? kAccent : kFg, focused_ ? 2.0f : 1.0f);
    p.text(label, rect.x + 4, rect.y + rect.height * 0.5f + 4, kFg, 0.7f);
    auto m = p.measure(label, 0.7f);
    p.text(text_, rect.x + 8 + m.width, rect.y + rect.height * 0.5f + 4,
           kFg, 0.7f);
}

// ---------------------------------------------------------------- SpanSelector
SpanSelector::SpanSelector(Axes* ax, std::string dir)
    : direction(std::move(dir)), ax_(ax) {}

bool SpanSelector::handleEvent(const Event& e) {
    if (!active || !ax_ || e.inaxes != ax_) {
        if (isRelease(e)) dragging_ = false;
        return false;
    }
    bool horiz = direction != "y";
    float v = horiz ? e.dataPos.x : e.dataPos.y;
    if (isPress(e)) {
        dragging_ = true;
        anchor_ = v;
        selection = std::pair{anchor_, v};
        return true;
    }
    if (dragging_ && isMotion(e)) {
        selection = std::pair{std::min(anchor_, v), std::max(anchor_, v)};
        return true;
    }
    if (dragging_ && isRelease(e)) {
        dragging_ = false;
        selection = std::pair{std::min(anchor_, v), std::max(anchor_, v)};
        if (onSelect) onSelect(selection->first, selection->second);
        return true;
    }
    return false;
}

void SpanSelector::draw(WidgetPainter& p) {
    if (!visible || !ax_ || !selection) return;
    auto& r = ax_->rect;
    bool horiz = direction != "y";
    if (horiz) {
        auto f0 = ax_->dataToFraction({selection->first, 0}).x;
        auto f1 = ax_->dataToFraction({selection->second, 0}).x;
        p.fillRect({r.x + int32_t(f0 * r.width), r.y,
                    uint32_t((f1 - f0) * r.width), r.height}, kSelect);
    } else {
        auto f0 = ax_->dataToFraction({0, selection->first}).y;
        auto f1 = ax_->dataToFraction({0, selection->second}).y;
        float y0 = r.y + (1 - f1) * r.height;
        p.fillRect({r.x, int32_t(y0), r.width,
                    uint32_t((f1 - f0) * r.height)}, kSelect);
    }
}

// ---------------------------------------------------------------- RectangleSelector
bool RectangleSelector::handleEvent(const Event& e) {
    if (!active || !ax_ || e.inaxes != ax_) {
        if (isRelease(e)) dragging_ = false;
        return false;
    }
    if (isPress(e)) {
        dragging_ = true;
        anchor_ = e.dataPos;
        selection = std::pair{anchor_, e.dataPos};
        return true;
    }
    if (dragging_ && isMotion(e)) {
        selection = std::pair{anchor_, e.dataPos};
        return true;
    }
    if (dragging_ && isRelease(e)) {
        dragging_ = false;
        selection = std::pair{anchor_, e.dataPos};
        if (onSelect)
            onSelect(std::min(anchor_.x, e.dataPos.x),
                     std::min(anchor_.y, e.dataPos.y),
                     std::max(anchor_.x, e.dataPos.x),
                     std::max(anchor_.y, e.dataPos.y));
        return true;
    }
    return false;
}

void RectangleSelector::draw(WidgetPainter& p) {
    if (!visible || !ax_ || !selection) return;
    auto& r = ax_->rect;
    auto toPx = [&](Point2D d) {
        auto f = ax_->dataToFraction(d);
        return Point2D{float(r.x) + f.x * float(r.width),
                        float(r.y) + (1 - f.y) * float(r.height)};
    };
    auto a = toPx(selection->first), b = toPx(selection->second);
    Rect2D box{int32_t(std::min(a.x, b.x)), int32_t(std::min(a.y, b.y)),
               uint32_t(std::abs(b.x - a.x)), uint32_t(std::abs(b.y - a.y))};
    p.fillRect(box, kSelect);
    p.outlineRect(box, kAccent);
}

// ---------------------------------------------------------------- PolygonSelector
bool PolygonSelector::handleEvent(const Event& e) {
    if (!active || !ax_) return false;
    if (e.type == Event::Type::KeyPress &&
        (e.key == "\r" || e.key == "Enter" || e.key == "enter") &&
        !verts_.empty()) {
        if (onSelect) onSelect(verts_);
        verts_.clear();
        return true;
    }
    if (e.inaxes != ax_) {
        if (isMotion(e)) hasHover_ = false;
        return false;
    }
    if (isMotion(e)) { hover_ = {e.x, e.y}; hasHover_ = true; return false; }
    if (isPress(e) && e.dblclick) {
        if (!verts_.empty()) {
            if (onSelect) onSelect(verts_);
            verts_.clear();
        }
        return true;
    }
    if (isPress(e)) {
        verts_.push_back(e.dataPos);
        return true;
    }
    return false;
}

void PolygonSelector::draw(WidgetPainter& p) {
    if (!visible || !ax_ || verts_.empty()) return;
    auto& r = ax_->rect;
    auto toPx = [&](Point2D d) {
        auto f = ax_->dataToFraction(d);
        return Point2D{float(r.x) + f.x * float(r.width),
                        float(r.y) + (1 - f.y) * float(r.height)};
    };
    std::vector<Point2D> pts;
    for (auto& v : verts_) pts.push_back(toPx(v));
    if (hasHover_) pts.push_back(hover_);
    if (pts.size() >= 2) p.line(pts, kAccent, 1.5f);
}

// ---------------------------------------------------------------- LassoSelector
bool LassoSelector::handleEvent(const Event& e) {
    if (!active || !ax_) return false;
    if (isPress(e) && e.inaxes == ax_) {
        dragging_ = true;
        path_.clear();
        path_.push_back(e.dataPos);
        return true;
    }
    if (dragging_ && isMotion(e)) {
        if (e.inaxes == ax_) path_.push_back(e.dataPos);
        return true;
    }
    if (dragging_ && isRelease(e)) {
        dragging_ = false;
        if (e.inaxes == ax_) path_.push_back(e.dataPos);
        if (onSelect && path_.size() > 1) onSelect(path_);
        return true;
    }
    return false;
}

void LassoSelector::draw(WidgetPainter& p) {
    if (!visible || !ax_ || path_.size() < 2) return;
    auto& r = ax_->rect;
    std::vector<Point2D> pts;
    pts.reserve(path_.size());
    for (auto& d : path_) {
        auto f = ax_->dataToFraction(d);
        pts.push_back({float(r.x) + f.x * float(r.width),
                            float(r.y) + (1 - f.y) * float(r.height)});
    }
    p.line(pts, kAccent, 1.5f);
}

// ---------------------------------------------------------------- Cursor
bool Cursor::handleEvent(const Event& e) {
    if (!active || !ax_) return false;
    if (isMotion(e) && e.inaxes == ax_) {
        pos_ = {e.x, e.y};
        hasPos_ = true;
        return false; // passive: don't consume
    }
    if (isMotion(e)) hasPos_ = false;
    return false;
}

void Cursor::draw(WidgetPainter& p) {
    if (!visible || !ax_ || !hasPos_) return;
    auto& r = ax_->rect;
    Point2D v[2] = {{pos_.x, float(r.y)}, {pos_.x, float(r.y + r.height)}};
    Point2D h[2] = {{float(r.x), pos_.y}, {float(r.x + r.width), pos_.y}};
    p.line(v, color, lineWidth);
    p.line(h, color, lineWidth);
}

// ---------------------------------------------------------------- MultiCursor
bool MultiCursor::handleEvent(const Event& e) {
    if (!active) return false;
    if (isMotion(e)) {
        bool inside = e.inaxes && std::ranges::find(axes_, e.inaxes) != axes_.end();
        hasPos_ = inside;
        pos_ = {e.x, e.y};
    }
    return false;
}

void MultiCursor::draw(WidgetPainter& p) {
    if (!visible || !hasPos_) return;
    for (auto* ax : axes_) {
        auto& r = ax->rect;
        Point2D h[2] = {{float(r.x), pos_.y}, {float(r.x + r.width), pos_.y}};
        Point2D v[2] = {{pos_.x, float(r.y)}, {pos_.x, float(r.y + r.height)}};
        // Vertical line only inside the axes the cursor is over is mpl
        // behavior for the y line; mpl MultiCursor draws the horizontal
        // line in all axes at the same canvas y.
        p.line(h, color, 1.0f);
        p.line(v, color, 1.0f);
    }
}

// ---------------------------------------------------------------- SubplotTool
SubplotTool::SubplotTool(Figure* fig) : fig_(fig) {
    // Sliders are laid out in the widget rect when drawn/handled; here we
    // create one per parameter, values 0..1.
    for (const char* name : {"left", "bottom", "right", "top", "wspace", "hspace"}) {
        auto s = std::make_unique<Slider>(Rect2D{0, 0, 0, 0}, name, 0.0f, 1.0f, 0.0f);
        s->onChanged = [this](float) { apply(); };
        sliders_.push_back(std::move(s));
    }
}

void SubplotTool::apply() {
    if (!fig_) return;
    auto v = [&](size_t i) { return sliders_[i]->val(); };
    fig_->subplotsAdjust(v(0), v(1), v(2), v(3), v(4), v(5));
}

bool SubplotTool::handleEvent(const Event& e) {
    if (!active) return false;
    layoutSliders();
    for (auto& s : sliders_)
        if (s->handleEvent(e)) return true;
    return false;
}

void SubplotTool::draw(WidgetPainter& p) {
    if (!visible) return;
    layoutSliders();
    p.fillRect(rect, kBg);
    for (auto& s : sliders_) s->draw(p);
}

void SubplotTool::layoutSliders() {
    float n = float(sliders_.size());
    float h = n > 0 ? rect.height / n : rect.height;
    for (size_t i = 0; i < sliders_.size(); ++i)
        sliders_[i]->rect = {rect.x, rect.y + int32_t(float(i) * h),
                             rect.width, uint32_t(h - 4)};
}

} // namespace volcano::plot
