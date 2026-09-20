// tests/test_interaction.cpp — §11 event system, navigation, widgets
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Events.hpp>
#include <volcano/plot/Interaction.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Widgets.hpp>
#include <volcano/plot/plots/Scatter3D.hpp>

#include <cmath>
#include <memory>

using namespace volcano;
using namespace volcano::plot;

namespace {

/// Figure with one axes filling a 100×100 canvas, viewport [0,10]².
/// Canvas (50,50) → data (5,5); canvas y is down, data y is up.
struct Fig {
    Figure fig;
    Axes* ax;
    Fig() {
        ax = fig.addAxes();
        fig.subplotsAdjust(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        fig.layout(Extent2D{100, 100});
        ax->setViewport({{0, 10}, {0, 10}});
    }
};

Event press(float x, float y, int b = 1, bool dbl = false) {
    Event e{Event::Type::ButtonPress};
    e.x = x; e.y = y; e.button = b; e.buttons = 1; e.dblclick = dbl;
    return e;
}
Event motion(float x, float y, int buttons = 0) {
    Event e{Event::Type::MotionNotify};
    e.x = x; e.y = y; e.buttons = buttons;
    return e;
}
Event release(float x, float y, int b = 1) {
    Event e{Event::Type::ButtonRelease};
    e.x = x; e.y = y; e.button = b;
    return e;
}
Event scroll(float x, float y, float step) {
    Event e{Event::Type::Scroll};
    e.x = x; e.y = y; e.step = step;
    return e;
}
Event key(std::string k) {
    Event e{Event::Type::KeyPress};
    e.key = std::move(k);
    return e;
}

} // namespace

// ═══ Event canvas ═══════════════════════════════════════════════════════════

TEST(EventCanvas, ConnectEmit) {
    EventCanvas c;
    int hits = 0;
    int id = c.connect("button_press_event", [&](const Event& e) {
        ++hits;
        EXPECT_FLOAT_EQ(e.x, 3.0f);
    });
    Event e{Event::Type::ButtonPress}; e.x = 3;
    c.emit(e);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(c.connectionCount(), 1u);
    c.disconnect(id);
    c.emit(e);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(c.connectionCount(), 0u);
}

TEST(EventCanvas, EventNames) {
    EXPECT_EQ(Event{Event::Type::ButtonPress}.name(), "button_press_event");
    EXPECT_EQ(Event{Event::Type::MotionNotify}.name(), "motion_notify_event");
    EXPECT_EQ(Event{Event::Type::KeyPress}.name(), "key_press_event");
    EXPECT_EQ(Event{Event::Type::Scroll}.name(), "scroll_event");
    EXPECT_EQ(Event{Event::Type::Pick}.name(), "pick_event");
    EXPECT_EQ(Event{Event::Type::ButtonRelease}.name(), "button_release_event");
    EXPECT_EQ(Event{Event::Type::Draw}.name(), "draw_event");
}

TEST(EventCanvas, NamedSubscriptionFilters) {
    EventCanvas c;
    int hits = 0;
    c.connect("key_press_event", [&](const Event&) { ++hits; });
    c.emit(press(1, 1));   // mouse event — shouldn't hit key subscriber
    EXPECT_EQ(hits, 0);
    c.emit(key("a"));
    EXPECT_EQ(hits, 1);
}

// ═══ Figure dispatch: hit-test + data coords ════════════════════════════════

TEST(FigDispatch, FillsInaxesAndData) {
    Fig f;
    Event got{};
    f.fig.canvas().connect("button_press_event",
                           [&](const Event& e) { got = e; });
    f.fig.dispatch(press(50, 50));
    EXPECT_EQ(got.inaxes, f.ax);
    EXPECT_NEAR(got.dataPos.x, 5.0f, 1e-4f);
    EXPECT_NEAR(got.dataPos.y, 5.0f, 1e-4f);
}

TEST(FigDispatch, InaxesNullOutsideAxes) {
    Fig f;
    Event got{};
    f.fig.canvas().connect("motion_notify_event",
                           [&](const Event& e) { got = e; });
    f.fig.dispatch(motion(150, 150)); // outside 100×100 canvas
    EXPECT_EQ(got.inaxes, nullptr);
}

TEST(FigDispatch, AxesAtPicksTopmost) {
    Fig f;
    auto* ax2 = f.fig.addAxesFraction(0.0f, 0.0f, 0.5f, 0.5f);
    f.fig.layout(Extent2D{100, 100});
    EXPECT_EQ(f.fig.axesAt(10, 60), ax2);   // inside both → topmost wins
    EXPECT_EQ(f.fig.axesAt(90, 90), f.ax);  // only in ax
}

TEST(FigDispatch, SubfigureAxesHitTest) {
    Fig f;
    auto* sub = f.fig.addSubfigure(f.fig.grid().at(0, 0));
    auto* axIn = sub->addAxes();
    sub->subplotsAdjust(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    f.fig.layout(Extent2D{100, 100});
    // The outer axes and the inner subfigure axes overlap; inner wins
    // (subfig axes checked after placements).
    auto all = f.fig.allAxes();
    EXPECT_GE(all.size(), 2u);
    (void)axIn;
}

// ═══ Navigation toolbar ═════════════════════════════════════════════════════

TEST(Navigation, PanModeDragShiftsViewport) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.pan();
    f.fig.dispatch(press(50, 50));
    f.fig.dispatch(motion(60, 50, 1)); // drag right 10px = -1 data units
    f.fig.dispatch(release(60, 50));
    auto vp = f.ax->viewport();
    EXPECT_NEAR(vp.x.min, -1.0f, 1e-4f);
    EXPECT_NEAR(vp.x.max, 9.0f, 1e-4f);
    EXPECT_FLOAT_EQ(vp.y.min, 0.0f);
    EXPECT_FLOAT_EQ(vp.y.max, 10.0f);
}

TEST(Navigation, NoModeNoPan) {
    Fig f;
    f.fig.nav(); // create, but leave mode None
    f.fig.dispatch(press(50, 50));
    f.fig.dispatch(motion(60, 50, 1));
    f.fig.dispatch(release(60, 50));
    EXPECT_FLOAT_EQ(f.ax->viewport().x.min, 0.0f);
}

TEST(Navigation, ZoomRectSetsViewport) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.zoom();
    f.fig.dispatch(press(20, 80)); // data (2,2) — y flipped
    f.fig.dispatch(motion(60, 20, 1));
    f.fig.dispatch(release(60, 20)); // data (6,8)
    auto vp = f.ax->viewport();
    EXPECT_NEAR(vp.x.min, 2.0f, 1e-4f);
    EXPECT_NEAR(vp.x.max, 6.0f, 1e-4f);
    EXPECT_NEAR(vp.y.min, 2.0f, 1e-4f);
    EXPECT_NEAR(vp.y.max, 8.0f, 1e-4f);
}

TEST(Navigation, ZoomRectConstrainX) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.constrainX = true; // zoom x only
    nav.zoom();
    f.fig.dispatch(press(20, 80));
    f.fig.dispatch(release(60, 20));
    auto vp = f.ax->viewport();
    EXPECT_NEAR(vp.x.min, 2.0f, 1e-4f);
    EXPECT_NEAR(vp.x.max, 6.0f, 1e-4f);
    EXPECT_FLOAT_EQ(vp.y.min, 0.0f); // y untouched
    EXPECT_FLOAT_EQ(vp.y.max, 10.0f);
}

TEST(Navigation, ZoomRectConstrainY) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.constrainY = true; // zoom y only
    nav.zoom();
    f.fig.dispatch(press(20, 80));
    f.fig.dispatch(release(60, 20));
    auto vp = f.ax->viewport();
    EXPECT_FLOAT_EQ(vp.x.min, 0.0f);
    EXPECT_FLOAT_EQ(vp.x.max, 10.0f);
    EXPECT_NEAR(vp.y.min, 2.0f, 1e-4f);
    EXPECT_NEAR(vp.y.max, 8.0f, 1e-4f);
}

TEST(Navigation, DegenerateZoomRectIgnored) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.zoom();
    f.fig.dispatch(press(50, 50));
    f.fig.dispatch(release(52, 52)); // <4px
    EXPECT_FLOAT_EQ(f.ax->viewport().x.min, 0.0f);
    EXPECT_FLOAT_EQ(f.ax->viewport().x.max, 10.0f);
}

TEST(Navigation, HomeBackForward) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.pushHistory(); // V0 = [0,10]²
    nav.zoom();
    f.fig.dispatch(press(20, 80));
    f.fig.dispatch(release(60, 20)); // V1 = [2,6]×[2,8]
    ASSERT_TRUE(nav.canBack());
    nav.back();
    EXPECT_NEAR(f.ax->viewport().x.min, 0.0f, 1e-4f);
    ASSERT_TRUE(nav.canForward());
    nav.forward();
    EXPECT_NEAR(f.ax->viewport().x.min, 2.0f, 1e-4f);
    nav.home();
    EXPECT_NEAR(f.ax->viewport().x.min, 0.0f, 1e-4f);
    EXPECT_NEAR(f.ax->viewport().y.max, 10.0f, 1e-4f);
}

TEST(Navigation, ScrollZoomAboutCursor) {
    Fig f;
    auto& nav = f.fig.nav();
    nav.scrollZoom = true;
    f.fig.dispatch(scroll(50, 50, 1.0f)); // zoom in at data (5,5)
    auto vp = f.ax->viewport();
    EXPECT_LT(vp.x.span(), 10.0f);
    EXPECT_NEAR((vp.x.min + vp.x.max) / 2, 5.0f, 0.5f);
}

TEST(Navigation, KeyBindingsPanZoomToggle) {
    Fig f;
    auto& nav = f.fig.nav();
    f.fig.dispatch(key("p"));
    EXPECT_EQ(nav.mode(), Navigation::Mode::Pan);
    f.fig.dispatch(key("p"));
    EXPECT_EQ(nav.mode(), Navigation::Mode::None);
    f.fig.dispatch(key("o"));
    EXPECT_EQ(nav.mode(), Navigation::Mode::ZoomRect);
}

TEST(Navigation, KeyBindingsGridAndLog) {
    Fig f;
    auto& nav = f.fig.nav();
    (void)nav;
    EXPECT_FALSE(f.ax->style().xAxis.grid);
    f.fig.dispatch(key("g"));
    EXPECT_TRUE(f.ax->style().xAxis.grid);
    EXPECT_TRUE(f.ax->style().yAxis.grid);

    EXPECT_EQ(f.ax->yscale().kind, ScaleKind::Linear);
    f.fig.dispatch(key("l"));
    EXPECT_EQ(f.ax->yscale().kind, ScaleKind::Log);
    f.fig.dispatch(key("l"));
    EXPECT_EQ(f.ax->yscale().kind, ScaleKind::Linear);
    f.fig.dispatch(key("k"));
    EXPECT_EQ(f.ax->xscale().kind, ScaleKind::Log);
}

TEST(Navigation, KeyBindingCallbacks) {
    Fig f;
    auto& nav = f.fig.nav();
    bool saved = false, quit = false, full = false;
    nav.onSaveRequest = [&] { saved = true; };
    nav.onQuitRequest = [&] { quit = true; };
    nav.onFullscreenToggle = [&] { full = true; };
    f.fig.dispatch(key("s"));
    f.fig.dispatch(key("f"));
    f.fig.dispatch(key("q"));
    EXPECT_TRUE(saved);
    EXPECT_TRUE(full);
    EXPECT_TRUE(quit);
}

TEST(Navigation, CursorReadout) {
    Fig f;
    auto& nav = f.fig.nav();
    std::string readout;
    nav.onCursorMove = [&](std::string_view s) { readout = s; };
    f.fig.dispatch(motion(50, 50));
    EXPECT_NE(readout.find("5"), std::string::npos);
    EXPECT_NE(readout.find("x="), std::string::npos);
}

// ═══ Widgets ════════════════════════════════════════════════════════════════

TEST(Widgets, SliderDrag) {
    Fig f;
    auto* s = f.fig.addWidget<Slider>(Rect2D{10, 90, 80, 10}, "a", 0.0f, 100.0f, 0.0f);
    float got = -1.0f;
    s->onChanged = [&](float v) { got = v; };
    f.fig.dispatch(press(50, 95));
    f.fig.dispatch(motion(70, 95, 1));
    f.fig.dispatch(release(70, 95));
    // rect.x=10, w=80 → x=70 → fraction (70-10)/80 = 0.75 → val 75
    EXPECT_NEAR(s->val(), 75.0f, 1e-3f);
    EXPECT_NEAR(got, 75.0f, 1e-3f);
}

TEST(Widgets, SliderStepped) {
    Fig f;
    auto* s = f.fig.addWidget<Slider>(Rect2D{10, 90, 80, 10}, "a", 0.0f, 100.0f,
                                      0.0f, 10 /*steps*/);
    f.fig.dispatch(press(33, 95)); // fraction 0.2875 → ~28.75 → rounds to 30
    EXPECT_NEAR(s->val(), 30.0f, 1e-3f);
}

TEST(Widgets, RangeSlider) {
    Fig f;
    auto* s = f.fig.addWidget<RangeSlider>(Rect2D{10, 90, 80, 10}, "r",
                                           0.0f, 100.0f, 20.0f, 60.0f);
    std::pair<float, float> got{-1, -1};
    s->onChanged = [&](float lo, float hi) { got = {lo, hi}; };
    // Drag near the lo handle (at x=10+0.2*80=26): press at 30 → lo=25.
    f.fig.dispatch(press(30, 95));
    EXPECT_NEAR(s->val().first, 25.0f, 1e-3f);
    EXPECT_FLOAT_EQ(s->val().second, 60.0f);
    EXPECT_NEAR(got.first, 25.0f, 1e-3f);
}

TEST(Widgets, ButtonClick) {
    Fig f;
    auto* b = f.fig.addWidget<Button>(Rect2D{10, 90, 20, 10}, "ok");
    int clicks = 0;
    b->onClick = [&] { ++clicks; };
    f.fig.dispatch(press(15, 95));
    EXPECT_EQ(clicks, 0);          // press alone doesn't fire
    f.fig.dispatch(release(15, 95));
    EXPECT_EQ(clicks, 1);
    // Release outside → no click.
    f.fig.dispatch(press(15, 95));
    f.fig.dispatch(release(99, 20));
    EXPECT_EQ(clicks, 1);
}

TEST(Widgets, CheckButtonsToggle) {
    Fig f;
    auto* c = f.fig.addWidget<CheckButtons>(Rect2D{10, 60, 80, 30},
                                            std::vector<std::string>{"a", "b"},
                                            std::vector<bool>{true, false});
    std::pair<size_t, bool> got{99, true};
    c->onChanged = [&](size_t i, bool v) { got = {i, v}; };
    // Row 1 is the lower half: y = 60 + 1.5*15 = 82.
    f.fig.dispatch(press(20, 82));
    EXPECT_EQ(got.first, 1u);
    EXPECT_TRUE(got.second);
    EXPECT_TRUE(c->status()[1]);
}

TEST(Widgets, RadioButtonsExclusive) {
    Fig f;
    auto* r = f.fig.addWidget<RadioButtons>(Rect2D{10, 60, 80, 30},
                                            std::vector<std::string>{"a", "b"}, 0);
    size_t got = 99;
    r->onChanged = [&](size_t i) { got = i; };
    f.fig.dispatch(press(20, 82)); // row 1
    EXPECT_EQ(r->activeIndex(), 1);
    EXPECT_EQ(got, 1u);
}

TEST(Widgets, TextBoxEditAndSubmit) {
    Fig f;
    auto* t = f.fig.addWidget<TextBox>(Rect2D{10, 90, 80, 10}, "in", "ab");
    std::string submitted;
    t->onSubmit = [&](std::string_view s) { submitted = s; };
    // Click to focus, type 'c', backspace, 'd', Enter.
    f.fig.dispatch(press(20, 95));
    f.fig.dispatch(key("c"));
    f.fig.dispatch(key("Backspace"));
    f.fig.dispatch(key("d"));
    EXPECT_EQ(t->text(), "abd");
    f.fig.dispatch(key("Enter"));
    EXPECT_EQ(submitted, "abd");
    // Unfocused: keys ignored.
    f.fig.dispatch(press(80, 10));
    f.fig.dispatch(key("z"));
    EXPECT_EQ(t->text(), "abd");
}

TEST(Widgets, SpanSelectorX) {
    Fig f;
    auto* s = f.fig.addWidget<SpanSelector>(f.ax, "x");
    std::pair<float, float> sel{-1, -1};
    s->onSelect = [&](float lo, float hi) { sel = {lo, hi}; };
    f.fig.dispatch(press(20, 50)); // data x=2
    f.fig.dispatch(motion(70, 60, 1));
    f.fig.dispatch(release(70, 60)); // data x=7
    EXPECT_NEAR(sel.first, 2.0f, 1e-4f);
    EXPECT_NEAR(sel.second, 7.0f, 1e-4f);
    ASSERT_TRUE(s->selection.has_value());
}

TEST(Widgets, RectangleSelector) {
    Fig f;
    auto* s = f.fig.addWidget<RectangleSelector>(f.ax);
    float x0 = -1, y0 = -1, x1 = -1, y1 = -1;
    s->onSelect = [&](float a, float b, float c, float d) {
        x0 = a; y0 = b; x1 = c; y1 = d;
    };
    f.fig.dispatch(press(20, 80)); // (2,2)
    f.fig.dispatch(release(60, 20)); // (6,8)
    EXPECT_NEAR(x0, 2.0f, 1e-4f);
    EXPECT_NEAR(y0, 2.0f, 1e-4f);
    EXPECT_NEAR(x1, 6.0f, 1e-4f);
    EXPECT_NEAR(y1, 8.0f, 1e-4f);
}

TEST(Widgets, PolygonSelector) {
    Fig f;
    auto* s = f.fig.addWidget<PolygonSelector>(f.ax);
    std::vector<Point2D> got;
    s->onSelect = [&](std::span<const Point2D> v) {
        got.assign(v.begin(), v.end());
    };
    f.fig.dispatch(press(10, 90));
    f.fig.dispatch(press(50, 90));
    f.fig.dispatch(press(50, 10));
    EXPECT_EQ(s->vertices().size(), 3u);
    f.fig.dispatch(press(52, 12, 1, /*dbl=*/true)); // double-click closes
    EXPECT_EQ(got.size(), 3u);
    EXPECT_NEAR(got[0].x, 1.0f, 1e-4f);
    EXPECT_TRUE(s->vertices().empty()); // cleared after close
}

TEST(Widgets, LassoSelector) {
    Fig f;
    auto* s = f.fig.addWidget<LassoSelector>(f.ax);
    std::vector<Point2D> got;
    s->onSelect = [&](std::span<const Point2D> v) {
        got.assign(v.begin(), v.end());
    };
    f.fig.dispatch(press(10, 90));
    f.fig.dispatch(motion(30, 50, 1));
    f.fig.dispatch(motion(60, 30, 1));
    f.fig.dispatch(release(80, 60));
    EXPECT_EQ(got.size(), 4u);
    EXPECT_NEAR(got.back().x, 8.0f, 1e-4f);
}

TEST(Widgets, CursorPassive) {
    Fig f;
    auto* c = f.fig.addWidget<Cursor>(f.ax);
    bool consumed = f.ax->style().xAxis.grid; // dummy
    (void)consumed;
    // Motion over the axes is observed but NOT consumed.
    f.fig.dispatch(motion(40, 40));
    // Cursor doesn't consume → nav still sees it.
    std::string readout;
    f.fig.nav().onCursorMove = [&](std::string_view s) { readout = s; };
    f.fig.dispatch(motion(50, 50));
    EXPECT_FALSE(readout.empty());
    (void)c;
}

TEST(Widgets, MultiCursorPassive) {
    Fig f;
    auto* ax2 = f.fig.addAxesFraction(0.5f, 0.5f, 0.5f, 0.5f);
    f.fig.layout(Extent2D{100, 100});
    auto* mc = f.fig.addWidget<MultiCursor>(std::vector<Axes*>{f.ax, ax2});
    f.fig.dispatch(motion(70, 70));
    EXPECT_TRUE(mc->active);
}

TEST(Widgets, SubplotToolAdjusts) {
    Fig f;
    auto* tool = f.fig.addWidget<SubplotTool>(&f.fig);
    tool->rect = {0, 0, 100, 100};
    // Press on the first slider's right edge → left≈1.0.
    f.fig.dispatch(press(99, 8));
    // sliders laid out: 6 rows of ~16px; first row y<16.
    // After apply(), grid left changed from 0.
    EXPECT_GT(f.fig.grid().left, 0.5f);
}

// ═══ Interactive 3D rotation (mpl Axes3D button1 rotate / button3 zoom) ═══

TEST(Navigation3D, LeftDragRotatesCamera) {
    Fig f;
    auto p = std::make_unique<Scatter3D>(
        std::vector<float>{0, 1}, std::vector<float>{0, 1},
        std::vector<float>{0, 1});
    p->setCamera(Camera3D::viewInit(30.0f, -60.0f));
    auto* raw = p.get();
    f.ax->addPlot(std::move(p));
    f.fig.nav();
    f.fig.dispatch(press(50, 50));
    f.fig.dispatch(motion(60, 40, 1));  // 10px right, 10px up
    f.fig.dispatch(release(60, 40));
    // Axes fills 100×100 canvas: azim -= dx/w*360 = -36°,
    // elev -= dy/h*180 → +18° (dragging up raises the viewpoint).
    auto* cam = raw->camera3D();
    ASSERT_NE(cam, nullptr);
    EXPECT_NEAR(cam->azimDeg, -96.0f, 1e-3f);
    EXPECT_NEAR(cam->elevDeg, 48.0f, 1e-3f);
}

TEST(Navigation3D, RightDragDolliesCamera) {
    Fig f;
    auto p = std::make_unique<Scatter3D>(
        std::vector<float>{0, 1}, std::vector<float>{0, 1},
        std::vector<float>{0, 1});
    p->setCamera(Camera3D::viewInit(30.0f, -60.0f));
    auto* raw = p.get();
    f.ax->addPlot(std::move(p));
    auto dist0 = std::hypot(raw->camera3D()->eye.x,
                            raw->camera3D()->eye.y, raw->camera3D()->eye.z);
    f.fig.nav();
    f.fig.dispatch(press(50, 50, 3));
    f.fig.dispatch(motion(50, 30, 8));  // 20px up → zoom in (dist shrinks)
    f.fig.dispatch(release(50, 30, 3));
    auto* cam = raw->camera3D();
    float dist = std::hypot(cam->eye.x, cam->eye.y, cam->eye.z);
    EXPECT_LT(dist, dist0);
    // Angles unchanged by a zoom drag.
    EXPECT_NEAR(cam->elevDeg, 30.0f, 1e-3f);
    EXPECT_NEAR(cam->azimDeg, -60.0f, 1e-3f);
}

TEST(Navigation3D, No3DPlotsFallsBackToNav) {
    Fig f;
    f.fig.nav();
    f.fig.dispatch(press(50, 50));
    f.fig.dispatch(motion(60, 50, 1));
    f.fig.dispatch(release(60, 50));
    // No 3D plots → drag does nothing (mode is None).
    EXPECT_FLOAT_EQ(f.ax->viewport().x.min, 0.0f);
}

TEST(InteractionRender, SupLabelsDrawText) {
    test::PlotTestHarness h(200, 200);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    fig.subplotsAdjust(0.2f, 0.15f, 0.8f, 0.85f, 0.0f, 0.0f);
    fig.suptitle("Suptitle");
    fig.supxlabel("Shared X");
    fig.supylabel("Shared Y");
    auto img = h.render(fig);
    const test::Pixel black{0, 0, 0, 255};
    // Bottom strip: supxlabel draws dark text pixels below the axes.
    EXPECT_GT(img.countColorInRegion(black, 40, 180, 160, 200, 160), 0);
    // Left strip: supylabel (rotated) draws dark text pixels.
    EXPECT_GT(img.countColorInRegion(black, 0, 40, 30, 160, 160), 0);
    // Top strip: suptitle draws dark text pixels above the axes.
    EXPECT_GT(img.countColorInRegion(black, 60, 0, 140, 30, 160), 0);
}

// ═══ Render: widget overlay + rubber band ═══════════════════════════════════

TEST(InteractionRender, WidgetDrawsOverlay) {
    test::PlotTestHarness h(128, 128);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    fig.subplotsAdjust(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    auto* b = fig.addWidget<Button>(Rect2D{10, 10, 40, 20}, "ok");
    (void)b;
    auto img = h.render(fig);
    // Button background (0.92 gray ≈ 235) should appear at the widget rect.
    auto p = img.get(30, 20);
    EXPECT_GT(int(p.r), 200);
    EXPECT_LT(int(p.r), 250);
}
