// volcano/plot/Widgets.hpp — matplotlib-style interactive widgets
#pragma once

#include "volcano/plot/Events.hpp"
#include "volcano/plot/Types.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace volcano::plot {

class Axes;
class Figure;

/// Drawing interface used by Widgets (implemented by the renderer —
/// widgets stay in the plot component and never touch Vulkan).
class WidgetPainter {
public:
    virtual ~WidgetPainter() = default;
    virtual void fillRect(Rect2D r, Color c) = 0;
    virtual void outlineRect(Rect2D r, Color c, float width = 1.0f) = 0;
    virtual void line(std::span<const Point2D> pts, Color c,
                      float width = 1.0f) = 0;
    virtual void text(std::string_view s, float x, float y, Color c,
                      float scale = 1.0f) = 0;
    struct Metrics { float width, height, ascent; };
    virtual Metrics measure(std::string_view s, float scale = 1.0f) = 0;
};

/// Base widget: a pixel-space UI element on the canvas (mpl's
/// widgets live in their own axes; here each widget owns a canvas rect).
class Widget {
public:
    Rect2D rect;      // canvas pixels
    bool active = true;
    bool visible = true;

    virtual ~Widget() = default;
    /// Returns true when the event was consumed (stops propagation).
    virtual bool handleEvent(const Event&) { return false; }
    virtual void draw(WidgetPainter& p) { (void)p; }

    [[nodiscard]] bool hitTest(float x, float y) const {
        return x >= rect.x && x <= rect.x + rect.width &&
               y >= rect.y && y <= rect.y + rect.height;
    }
};

/// Slider: a track with a draggable handle. onChanged(value).
class Slider : public Widget {
public:
    Slider(Rect2D rect, std::string label, float vmin, float vmax,
           float vinit = 0.0f, int steps = 0);
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;

    float val() const { return val_; }
    void setVal(float v);
    std::function<void(float)> onChanged;
    float valmin, valmax, valstep; // valstep 0 → continuous
    std::string label;

private:
    float val_;
    bool dragging_ = false;
    float handleX() const;
    void setFromX(float px);
};

/// RangeSlider: two handles selecting [lo, hi]. onChanged(lo, hi).
class RangeSlider : public Widget {
public:
    RangeSlider(Rect2D rect, std::string label, float vmin, float vmax,
                float lo, float hi);
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    std::pair<float, float> val() const { return {lo_, hi_}; }
    std::function<void(float, float)> onChanged;
    float valmin, valmax;
    std::string label;

private:
    float lo_, hi_;
    int dragHandle_ = -1; // 0 = lo, 1 = hi
    float toX(float v) const;
    void setFromX(float px, int which);
};

/// Button: click → onClick().
class Button : public Widget {
public:
    Button(Rect2D rect, std::string label);
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    std::function<void()> onClick;
    std::string label;
private:
    bool pressed_ = false;
};

/// CheckButtons: a vertical list of toggles. onChanged(index, checked).
class CheckButtons : public Widget {
public:
    CheckButtons(Rect2D rect, std::vector<std::string> labels,
                 std::vector<bool> checked = {});
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    [[nodiscard]] const std::vector<bool>& status() const { return checked_; }
    std::function<void(size_t, bool)> onChanged;
    std::vector<std::string> labels;
private:
    std::vector<bool> checked_;
    float rowH() const;
};

/// RadioButtons: exclusive choice list. onChanged(index).
class RadioButtons : public Widget {
public:
    RadioButtons(Rect2D rect, std::vector<std::string> labels,
                 int active = 0);
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    /// Index of the selected option (mpl `value_selected`).
    [[nodiscard]] int activeIndex() const { return active_; }
    void setActive(int i) {
        if (i >= 0 && i < int(labels.size())) active_ = i;
    }
    std::function<void(size_t)> onChanged;
    std::vector<std::string> labels;
private:
    int active_ = 0;
    float rowH() const;
};

/// TextBox: click to focus, key events edit, Enter → onSubmit(text).
class TextBox : public Widget {
public:
    TextBox(Rect2D rect, std::string label, std::string initial = "");
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    [[nodiscard]] const std::string& text() const { return text_; }
    std::function<void(std::string_view)> onSubmit;
    std::string label;
private:
    std::string text_;
    bool focused_ = false;
};

/// SpanSelector: drag on an axes to select a value span
/// (direction "x" or "y"). onSelect(min, max) in data coords.
class SpanSelector : public Widget {
public:
    /// `ax` is the axes whose rect the selector covers.
    SpanSelector(Axes* ax, std::string direction = "x");
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    std::function<void(float, float)> onSelect;
    std::string direction; // "x" | "y"
    /// Active selection in data coords (nullopt until first drag).
    std::optional<std::pair<float, float>> selection;
private:
    Axes* ax_;
    bool dragging_ = false;
    float anchor_ = 0.0f; // data coord
};

/// RectangleSelector: drag → onSelect(x0, y0, x1, y1) in data coords.
class RectangleSelector : public Widget {
public:
    explicit RectangleSelector(Axes* ax) : ax_(ax) { }
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    std::function<void(float, float, float, float)> onSelect;
    std::optional<std::pair<Point2D, Point2D>> selection; // data coords
private:
    Axes* ax_;
    bool dragging_ = false;
    Point2D anchor_{};
};

/// EllipseSelector: like RectangleSelector; onSelect gives the bounding
/// rect of the ellipse.
class EllipseSelector : public RectangleSelector {
public:
    using RectangleSelector::RectangleSelector;
};

/// PolygonSelector: clicks add vertices; double-click or Enter closes
/// → onSelect(vertices in data coords).
class PolygonSelector : public Widget {
public:
    explicit PolygonSelector(Axes* ax) : ax_(ax) {}
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    std::function<void(std::span<const Point2D>)> onSelect;
    [[nodiscard]] const std::vector<Point2D>& vertices() const { return verts_; }
private:
    Axes* ax_;
    std::vector<Point2D> verts_;      // data coords
    Point2D hover_{};                 // canvas px for rubber band
    bool hasHover_ = false;
};

/// Lasso / LassoSelector: freehand drag → onSelect(vertex path, data).
class LassoSelector : public Widget {
public:
    explicit LassoSelector(Axes* ax) : ax_(ax) {}
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    std::function<void(std::span<const Point2D>)> onSelect;
    [[nodiscard]] const std::vector<Point2D>& path() const { return path_; }
private:
    Axes* ax_;
    std::vector<Point2D> path_; // data coords
    bool dragging_ = false;
};
/// mpl's Lasso is the same freehand gesture.
using Lasso = LassoSelector;

/// Cursor: crosshair lines following the mouse inside an axes.
class Cursor : public Widget {
public:
    explicit Cursor(Axes* ax) : ax_(ax) {}
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    Color color = Color::black();
    float lineWidth = 1.0f;
    std::string lineStyle = "-";
private:
    Axes* ax_;
    Point2D pos_{};   // canvas px
    bool hasPos_ = false;
};

/// MultiCursor: crosshair synchronized across a set of axes.
class MultiCursor : public Widget {
public:
    explicit MultiCursor(std::vector<Axes*> axes) : axes_(std::move(axes)) {}
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
    Color color = Color::black();
private:
    std::vector<Axes*> axes_;
    Point2D pos_{};
    bool hasPos_ = false;
};

/// SubplotTool: sliders adjusting the figure's subplot margins
/// (left/right/bottom/top/wspace/hspace). Emits via figure.subplotsAdjust.
class SubplotTool : public Widget {
public:
    explicit SubplotTool(Figure* fig);
    bool handleEvent(const Event& e) override;
    void draw(WidgetPainter& p) override;
private:
    void apply();
    void layoutSliders();
    Figure* fig_;
    std::vector<std::unique_ptr<Slider>> sliders_;
};

} // namespace volcano::plot
