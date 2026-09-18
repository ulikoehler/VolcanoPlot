// tests/test_annotation.cpp — tests for text annotations and arrows
#include <gtest/gtest.h>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Axes.hpp>
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ScatterPlot.hpp>

#include <cmath>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

// ─── Coordinate transforms ────────────────────────────────────────────────

TEST(AnnotationTransform, DataCoordsCenter) {
    // Data (0.5, 0.5) in viewport [0,1]×[0,1] should map to the center
    // of a 100×100 axes rect at (0,0).
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    auto p = toDisplay(0.5f, 0.5f, CoordSystem::Data, rect, fig, ax);
    EXPECT_NEAR(p.x, 50.0f, 0.5f);
    EXPECT_NEAR(p.y, 50.0f, 0.5f);
}

TEST(AnnotationTransform, DataCoordsOrigin) {
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    // (0, 0) in data → bottom-left of axes → pixel (0, 100) (Y-down)
    auto p = toDisplay(0.0f, 0.0f, CoordSystem::Data, rect, fig, ax);
    EXPECT_NEAR(p.x, 0.0f, 0.5f);
    EXPECT_NEAR(p.y, 100.0f, 0.5f);
}

TEST(AnnotationTransform, DataCoordsTopRight) {
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    auto p = toDisplay(1.0f, 1.0f, CoordSystem::Data, rect, fig, ax);
    EXPECT_NEAR(p.x, 100.0f, 0.5f);
    EXPECT_NEAR(p.y, 0.0f, 0.5f);
}

TEST(AnnotationTransform, AxesCoords) {
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{10, 20, 100, 200};
    Extent2D fig{200, 300};
    // (0, 0) in axes fraction → bottom-left of axes rect
    auto p0 = toDisplay(0.0f, 0.0f, CoordSystem::Axes, rect, fig, ax);
    EXPECT_NEAR(p0.x, 10.0f, 0.5f);
    EXPECT_NEAR(p0.y, 220.0f, 0.5f);  // 20 + 200
    // (1, 1) → top-right
    auto p1 = toDisplay(1.0f, 1.0f, CoordSystem::Axes, rect, fig, ax);
    EXPECT_NEAR(p1.x, 110.0f, 0.5f);  // 10 + 100
    EXPECT_NEAR(p1.y, 20.0f, 0.5f);
    // (0.5, 0.5) → center
    auto pc = toDisplay(0.5f, 0.5f, CoordSystem::Axes, rect, fig, ax);
    EXPECT_NEAR(pc.x, 60.0f, 0.5f);   // 10 + 50
    EXPECT_NEAR(pc.y, 120.0f, 0.5f);  // 20 + 100
}

TEST(AnnotationTransform, FigureCoords) {
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{10, 20, 100, 200};
    Extent2D fig{200, 300};
    // (0, 0) in figure fraction → bottom-left of figure
    auto p0 = toDisplay(0.0f, 0.0f, CoordSystem::Figure, rect, fig, ax);
    EXPECT_NEAR(p0.x, 0.0f, 0.5f);
    EXPECT_NEAR(p0.y, 300.0f, 0.5f);
    // (1, 1) → top-right
    auto p1 = toDisplay(1.0f, 1.0f, CoordSystem::Figure, rect, fig, ax);
    EXPECT_NEAR(p1.x, 200.0f, 0.5f);
    EXPECT_NEAR(p1.y, 0.0f, 0.5f);
}

TEST(AnnotationTransform, DisplayCoords) {
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    auto p = toDisplay(42.0f, 58.0f, CoordSystem::Display, rect, fig, ax);
    EXPECT_NEAR(p.x, 42.0f, 0.5f);
    EXPECT_NEAR(p.y, 58.0f, 0.5f);
}

TEST(AnnotationTransform, OffsetPoints) {
    // Data (0.5, 0.5) with offset (72, 72) points at 72 DPI
    // → 72pt = 72px right, 72px up
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    float dpi = 72.0f;
    auto p = toDisplay(0.5f, 0.5f, CoordSystem::OffsetPoints, rect, fig, ax,
                       dpi, 72.0f, 72.0f);
    // Base: (50, 50). Offset: +72px x, -72px y (Y-up → Y-down)
    EXPECT_NEAR(p.x, 122.0f, 0.5f);
    EXPECT_NEAR(p.y, -22.0f, 0.5f);
}

TEST(AnnotationTransform, OffsetPointsDpi100) {
    // At 100 DPI, 72pt = 100px
    Axes ax; ax.setViewport({0, 1, 0, 1, 0, 1});
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    float dpi = 100.0f;
    auto p = toDisplay(0.0f, 0.0f, CoordSystem::OffsetPoints, rect, fig, ax,
                       dpi, 72.0f, 0.0f);
    // Base: (0, 100). Offset: +100px x
    EXPECT_NEAR(p.x, 100.0f, 0.5f);
    EXPECT_NEAR(p.y, 100.0f, 0.5f);
}

// ─── Text alignment ───────────────────────────────────────────────────────

TEST(AnnotationAlign, LeftBaseline) {
    auto p = alignText({100, 100}, HAlign::Left, VAlign::Baseline,
                       50, 20, 15);
    EXPECT_NEAR(p.x, 100.0f, 0.1f);
    EXPECT_NEAR(p.y, 100.0f, 0.1f);
}

TEST(AnnotationAlign, CenterBaseline) {
    auto p = alignText({100, 100}, HAlign::Center, VAlign::Baseline,
                       50, 20, 15);
    EXPECT_NEAR(p.x, 75.0f, 0.1f);  // 100 - 50/2
    EXPECT_NEAR(p.y, 100.0f, 0.1f);
}

TEST(AnnotationAlign, RightBaseline) {
    auto p = alignText({100, 100}, HAlign::Right, VAlign::Baseline,
                       50, 20, 15);
    EXPECT_NEAR(p.x, 50.0f, 0.1f);  // 100 - 50
    EXPECT_NEAR(p.y, 100.0f, 0.1f);
}

TEST(AnnotationAlign, LeftTop) {
    // Top alignment: y += ascent → text top at the position
    auto p = alignText({100, 100}, HAlign::Left, VAlign::Top,
                       50, 20, 15);
    EXPECT_NEAR(p.x, 100.0f, 0.1f);
    EXPECT_NEAR(p.y, 115.0f, 0.1f);  // 100 + 15
}

TEST(AnnotationAlign, LeftCenter) {
    auto p = alignText({100, 100}, HAlign::Left, VAlign::Center,
                       50, 20, 15);
    EXPECT_NEAR(p.x, 100.0f, 0.1f);
    EXPECT_NEAR(p.y, 105.0f, 0.1f);  // 100 + 15 - 20/2
}

TEST(AnnotationAlign, LeftBottom) {
    auto p = alignText({100, 100}, HAlign::Left, VAlign::Bottom,
                       50, 20, 15);
    EXPECT_NEAR(p.x, 100.0f, 0.1f);
    EXPECT_NEAR(p.y, 95.0f, 0.1f);  // 100 + 15 - 20
}

// ─── Axes::text() and Axes::annotate() ────────────────────────────────────

TEST(AxesText, AddTextAnnotation) {
    Axes ax;
    auto* t = ax.text(3.0f, 4.0f, "hello");
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(ax.texts().size(), 1u);
    EXPECT_EQ(ax.texts()[0].text, "hello");
    EXPECT_NEAR(ax.texts()[0].x, 3.0f, 0.01f);
    EXPECT_NEAR(ax.texts()[0].y, 4.0f, 0.01f);
    EXPECT_EQ(ax.texts()[0].coords, CoordSystem::Data);
}

TEST(AxesText, AddTextWithAxesCoords) {
    Axes ax;
    auto* t = ax.text(0.5f, 0.5f, "center", CoordSystem::Axes);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(ax.texts()[0].coords, CoordSystem::Axes);
}

TEST(AxesText, AddMultipleTexts) {
    Axes ax;
    ax.text(1, 2, "a");
    ax.text(3, 4, "b");
    ax.text(5, 6, "c");
    EXPECT_EQ(ax.texts().size(), 3u);
    EXPECT_EQ(ax.texts()[0].text, "a");
    EXPECT_EQ(ax.texts()[1].text, "b");
    EXPECT_EQ(ax.texts()[2].text, "c");
}

TEST(AxesText, CustomizeReturnedAnnotation) {
    Axes ax;
    auto* t = ax.text(1, 2, "label");
    t->color = Color::red();
    t->fontSize = 2.0f;
    t->halign = HAlign::Center;
    t->valign = VAlign::Top;
    EXPECT_NEAR(ax.texts()[0].color.r, Color::red().r, 0.01f);
    EXPECT_NEAR(ax.texts()[0].color.g, Color::red().g, 0.01f);
    EXPECT_NEAR(ax.texts()[0].fontSize, 2.0f, 0.01f);
    EXPECT_EQ(ax.texts()[0].halign, HAlign::Center);
    EXPECT_EQ(ax.texts()[0].valign, VAlign::Top);
}

TEST(AxesAnnotate, AddAnnotation) {
    Axes ax;
    auto* a = ax.annotate(3, 4, 5, 7, "peak");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(ax.annotations().size(), 1u);
    EXPECT_EQ(ax.annotations()[0].text, "peak");
    EXPECT_NEAR(ax.annotations()[0].xy[0], 3.0f, 0.01f);
    EXPECT_NEAR(ax.annotations()[0].xy[1], 4.0f, 0.01f);
    EXPECT_NEAR(ax.annotations()[0].xyText[0], 5.0f, 0.01f);
    EXPECT_NEAR(ax.annotations()[0].xyText[1], 7.0f, 0.01f);
}

TEST(AxesAnnotate, CustomizeReturnedAnnotation) {
    Axes ax;
    auto* a = ax.annotate(1, 2, 3, 4, "point");
    a->arrowColor = Color::blue();
    a->arrowStyle = ArrowStyle::Arrow;
    a->arrowHeadSize = 15.0f;
    EXPECT_NEAR(ax.annotations()[0].arrowColor.b, Color::blue().b, 0.01f);
    EXPECT_EQ(ax.annotations()[0].arrowStyle, ArrowStyle::Arrow);
    EXPECT_NEAR(ax.annotations()[0].arrowHeadSize, 15.0f, 0.01f);
}

TEST(AxesAnnotate, MultipleAnnotations) {
    Axes ax;
    ax.annotate(1, 1, 2, 2, "a");
    ax.annotate(3, 3, 4, 4, "b");
    EXPECT_EQ(ax.annotations().size(), 2u);
}

TEST(AxesAnnotate, DifferentCoordSystems) {
    Axes ax;
    auto* a = ax.annotate(0.5, 0.5, 0.8, 0.9, "note", CoordSystem::Axes);
    EXPECT_EQ(a->xyCoords, CoordSystem::Axes);
    EXPECT_EQ(a->xyTextCoords, CoordSystem::Axes);
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration tests — render with annotations and verify pixels
// ═══════════════════════════════════════════════════════════════════════════

namespace {

struct AnnFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit AnnFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.grid().left = 0.0f; figure.grid().right = 1.0f;
        figure.grid().bottom = 0.0f; figure.grid().top = 1.0f;
    }

    Image render() { return harness.render(figure); }
};

bool isNotWhite(const Pixel& p) {
    return !(p.r > 230 && p.g > 230 && p.b > 230);
}

bool isBlackish(const Pixel& p) {
    return p.r < 80 && p.g < 80 && p.b < 80 && p.a > 100;
}

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

} // namespace

TEST(AnnotationRegression, TextRendersInDataCoords) {
    // Place a text annotation at data (0.5, 0.5) — center of axes.
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "HELLO");
    t->color = Color::black();
    t->halign = HAlign::Center;
    t->valign = VAlign::Center;
    auto img = cf.render();

    // The text should produce some dark pixels near the center.
    size_t darkCount = countPixels(img, isBlackish);
    EXPECT_GT(darkCount, 10u) << "Text should render dark pixels";
}

TEST(AnnotationRegression, TextInAxesCoords) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "X", CoordSystem::Axes);
    t->color = Color::black();
    t->halign = HAlign::Center;
    t->valign = VAlign::Center;
    auto img = cf.render();

    // Should render at the center of the axes.
    size_t darkCount = countPixels(img, isBlackish);
    EXPECT_GT(darkCount, 5u) << "Text in axes coords should render";
}

TEST(AnnotationRegression, TextWithBackgroundBox) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "BOX");
    t->color = Color::black();
    t->halign = HAlign::Center;
    t->valign = VAlign::Center;
    t->bboxFaceColor = Color::fromRgba8(255, 0, 0, 255);
    t->bboxEdgeColor = Color::black();
    t->bboxPadding = 8.0f;
    auto img = cf.render();

    // The red background box should produce red pixels.
    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y) {
        for (uint32_t x = 0; x < img.width(); ++x) {
            auto p = img.get(x, y);
            if (p.r > 200 && p.g < 50 && p.b < 50) ++redCount;
        }
    }
    EXPECT_GT(redCount, 100u) << "Background box should render red pixels";
}

TEST(AnnotationRegression, AnnotationWithArrowRenders) {
    // Annotate a point with an arrow.
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    // Add a scatter point at (0.5, 0.5) so the data has content.
    Series2D s;
    s.points = {{0.5f, 0.5f}};
    s.color = Color::red();
    s.size = 10.0f;
    cf.axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    // Annotate it with text at (0.8, 0.8).
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.8f, 0.8f, "peak");
    a->color = Color::black();
    a->arrowColor = Color::black();
    a->arrowStyle = ArrowStyle::Simple;
    a->arrowWidth = 2.0f;
    a->arrowHeadSize = 12.0f;
    auto img = cf.render();

    // The arrow and text should produce dark pixels.
    size_t darkCount = countPixels(img, isBlackish);
    EXPECT_GT(darkCount, 20u) << "Annotation arrow+text should render dark pixels";
}

TEST(AnnotationRegression, MultipleTextsRender) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    cf.axes->text(0.25f, 0.25f, "A")->color = Color::black();
    cf.axes->text(0.75f, 0.75f, "B")->color = Color::black();
    cf.axes->text(0.25f, 0.75f, "C")->color = Color::black();
    cf.axes->text(0.75f, 0.25f, "D")->color = Color::black();
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlackish);
    EXPECT_GT(darkCount, 20u) << "Multiple texts should render";
}

TEST(AnnotationRegression, TextDoesNotCrashWithEmptyString) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    cf.axes->text(0.5f, 0.5f, "");
    auto img = cf.render();
    // Should not crash; no specific pixel assertions.
    SUCCEED();
}

TEST(AnnotationRegression, AnnotationWithoutArrowRendersText) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.7f, 0.7f, "label");
    a->arrowStyle = ArrowStyle::None;
    a->color = Color::black();
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlackish);
    EXPECT_GT(darkCount, 5u) << "Annotation text should render without arrow";
}

TEST(AnnotationRegression, TextColorIsRespected) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "RED");
    t->color = Color::red();
    t->halign = HAlign::Center;
    t->valign = VAlign::Center;
    auto img = cf.render();

    // Should produce red pixels from the text color.
    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y) {
        for (uint32_t x = 0; x < img.width(); ++x) {
            auto p = img.get(x, y);
            if (p.r > 200 && p.g < 50 && p.b < 50 && p.a > 100) ++redCount;
        }
    }
    EXPECT_GT(redCount, 5u) << "Text color should be respected";
}

// ═══ arrowstyle: parser (micro) ═══════════════════════════════════════════

TEST(ArrowStyleParse, SimpleNames) {
    using E = ArrowStyleSpec::End;
    EXPECT_EQ(parseArrowStyle("-").headA, E::None);
    EXPECT_EQ(parseArrowStyle("-").headB, E::None);
    EXPECT_EQ(parseArrowStyle("->").headB, E::Open);
    EXPECT_EQ(parseArrowStyle("<-").headA, E::Open);
    EXPECT_EQ(parseArrowStyle("<->").headA, E::Open);
    EXPECT_EQ(parseArrowStyle("<->").headB, E::Open);
    EXPECT_EQ(parseArrowStyle("-|>").headB, E::Filled);
    EXPECT_EQ(parseArrowStyle("<|-").headA, E::Filled);
    EXPECT_EQ(parseArrowStyle("<|-|>").headA, E::Filled);
    EXPECT_EQ(parseArrowStyle("<|-|>").headB, E::Filled);
    EXPECT_EQ(parseArrowStyle("-[").headB, E::Bracket);
    EXPECT_EQ(parseArrowStyle("]-[").headA, E::Bracket);
    EXPECT_EQ(parseArrowStyle("]-[").headB, E::Bracket);
    EXPECT_EQ(parseArrowStyle("|-|").headA, E::Bar);
    EXPECT_EQ(parseArrowStyle("|-|").headB, E::Bar);
}

TEST(ArrowStyleParse, NamedBodies) {
    using B = ArrowStyleSpec::Body;
    auto s = parseArrowStyle("simple");
    EXPECT_EQ(s.body, B::Simple);
    EXPECT_FLOAT_EQ(s.headLength, 0.5f);
    EXPECT_FLOAT_EQ(s.headWidth, 0.5f);
    EXPECT_FLOAT_EQ(s.tailWidth, 0.2f);
    auto f = parseArrowStyle("fancy");
    EXPECT_EQ(f.body, B::Fancy);
    EXPECT_FLOAT_EQ(f.tailWidth, 0.4f);
    auto w = parseArrowStyle("wedge");
    EXPECT_EQ(w.body, B::Wedge);
    EXPECT_FLOAT_EQ(w.tailWidth, 0.3f);
}

TEST(ArrowStyleParse, ParamOverrides) {
    auto s = parseArrowStyle("-|>, head_length=0.8, head_width=0.6");
    EXPECT_FLOAT_EQ(s.headLength, 0.8f);
    EXPECT_FLOAT_EQ(s.headWidth, 0.6f);
    auto w = parseArrowStyle("wedge, tail_width=0.9, shrink_factor=0.25");
    EXPECT_FLOAT_EQ(w.tailWidth, 0.9f);
    EXPECT_FLOAT_EQ(w.shrinkFactor, 0.25f);
}

TEST(ArrowStyleParse, UnknownFallsBackToArrow) {
    auto s = parseArrowStyle("bogus");
    EXPECT_EQ(s.headB, ArrowStyleSpec::End::Open);
}

// ═══ arrowstyle: geometry (micro) ═════════════════════════════════════════

namespace {

std::vector<Point2D> straightPath() {
    return {{0.0f, 0.0f}, {50.0f, 0.0f}, {100.0f, 0.0f}};
}

} // namespace

TEST(ArrowGeom, FilledHeadTriangle) {
    // "-|>" with ms=10: hl=4, hw(half-width)=2 → tip (100,0), base (96,±2).
    auto g = buildArrowGeometry(straightPath(),
                                parseArrowStyle("-|>"), 1.0f);
    ASSERT_EQ(g.fills.size(), 1u);
    ASSERT_EQ(g.fills[0].size(), 3u);
    // apex overshoots the tip by pad = 0.5·lw/sin(θ) ≈ 1.118 (mpl)
    EXPECT_NEAR(g.fills[0][0].x, 101.118f, 1e-2f); // apex
    EXPECT_NEAR(g.fills[0][1].x, 96.0f, 1e-4f);    // base corners
    EXPECT_NEAR(std::abs(g.fills[0][1].y), 2.0f, 1e-4f);
    EXPECT_NEAR(g.fills[0][2].y, -g.fills[0][1].y, 1e-4f);
}

TEST(ArrowGeom, OpenHeadIsTwoStrokes) {
    auto g = buildArrowGeometry(straightPath(),
                                parseArrowStyle("->"), 1.0f);
    // shaft + 2 head strokes
    EXPECT_EQ(g.strokes.size(), 3u);
    EXPECT_TRUE(g.fills.empty());
    // Both head strokes end at the overshot apex (~101.118).
    for (size_t i = 1; i < g.strokes.size(); ++i) {
        EXPECT_NEAR(g.strokes[i].back().x, 101.118f, 1e-2f);
    }
}

TEST(ArrowGeom, DoubleHeadedHasTwoFills) {
    auto g = buildArrowGeometry(straightPath(),
                                parseArrowStyle("<|-|>"), 1.0f);
    EXPECT_EQ(g.fills.size(), 2u);
    // One head at x≈0 (A end), one at x≈100 (B end).
    float minX = 1e9f, maxX = -1e9f;
    for (auto& f : g.fills) {
        minX = std::min(minX, f[0].x);
        maxX = std::max(maxX, f[0].x);
    }
    EXPECT_NEAR(minX, -1.118f, 1e-2f);
    EXPECT_NEAR(maxX, 101.118f, 1e-2f);
}

TEST(ArrowGeom, BracketIsStrokeAcrossTip) {
    // mpl _get_bracket: a stroked "[" — crossbar ±widthB·ms at the tip
    // plus stubs lengthB·ms back along the path.
    auto g = buildArrowGeometry(straightPath(),
                                parseArrowStyle("-["), 1.0f);
    EXPECT_TRUE(g.fills.empty());
    ASSERT_EQ(g.strokes.size(), 2u);  // shaft + bracket
    const auto& br = g.strokes[1];
    ASSERT_EQ(br.size(), 4u);
    // Crossbar ends at ±widthB·ms = ±10, stubs lengthB·ms = 2 back.
    EXPECT_NEAR(br[1].y, 10.0f, 1e-4f);
    EXPECT_NEAR(br[2].y, -10.0f, 1e-4f);
    EXPECT_NEAR(br[0].x, 100.0f - 2.0f, 1e-4f);
    EXPECT_NEAR(br[3].x, 100.0f - 2.0f, 1e-4f);
    EXPECT_NEAR(br[1].x, 100.0f, 1e-4f);
}

TEST(ArrowGeom, SimpleBodyIsSinglePolygon) {
    // mpl Simple transmute: one closed bezier outline — parallel tail,
    // wedged head, tip at the path end.
    auto g = buildArrowGeometry(straightPath(),
                                parseArrowStyle("simple"), 1.0f);
    ASSERT_EQ(g.fills.size(), 1u);
    EXPECT_TRUE(g.strokes.empty());
    auto& o = g.fills[0];
    ASSERT_GT(o.size(), 10u);   // flattened curves, not 5 corners
    EXPECT_NEAR(o.front().x, o.back().x, 1e-3f);  // closed
    EXPECT_NEAR(o.front().y, o.back().y, 1e-3f);
    // Tip at B(100,0); head base at hl = 0.5*ms = 5 back; head half-width
    // hw·ms/2 = 2.5; tail half-width tw·ms/2 = 1.0.
    float maxX = 0, minY = 1e9f, maxY = -1e9f;
    for (auto p : o) {
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    EXPECT_NEAR(maxX, 100.0f, 0.5f);
    EXPECT_NEAR(maxY - minY, 5.0f, 0.5f);   // head half-width 2.5 each side
    // Tail start half-width ≈ 1.0: find verts near x≈0.
    float tailHalf = 0;
    for (auto p : o) if (p.x < 2.0f) tailHalf = std::max(tailHalf, std::abs(p.y));
    EXPECT_NEAR(tailHalf, 1.0f, 0.2f);
}

TEST(ArrowGeom, WedgeIsTaperedPolygon) {
    // mpl Wedge: half-width tail_width·ms/2 = 1.5 at A, ×shrink_factor
    // (0.5) at the middle, 0 at B — a smooth tapered outline.
    auto g = buildArrowGeometry(straightPath(),
                                parseArrowStyle("wedge"), 1.0f);
    ASSERT_EQ(g.fills.size(), 1u);
    auto& o = g.fills[0];
    ASSERT_GT(o.size(), 10u);
    EXPECT_NEAR(o.front().x, o.back().x, 1e-3f);
    EXPECT_NEAR(o.front().y, o.back().y, 1e-3f);
    // Half-width as a function of x along the straight path.
    auto halfAt = [&](float x) {
        float h = 0;
        for (auto p : o)
            if (std::abs(p.x - x) < 3.0f) h = std::max(h, std::abs(p.y));
        return h;
    };
    EXPECT_NEAR(halfAt(0.0f), 1.5f, 0.2f);    // tw·ms/2
    EXPECT_NEAR(halfAt(50.0f), 0.75f, 0.2f);  // ×shrink at mid
    float tip = 0;
    for (auto p : o) tip = std::max(tip, p.x);
    EXPECT_NEAR(tip, 100.0f, 0.5f);           // apex at B
}

TEST(ArrowGeom, MutationScaleScalesHead) {
    auto spec = parseArrowStyle("-|>");
    spec.mutationSize = 20.0f;  // hl = 0.4*20 = 8, +pad ≈ 1.118
    auto g = buildArrowGeometry(straightPath(), spec, 1.0f);
    ASSERT_EQ(g.fills.size(), 1u);
    EXPECT_NEAR(g.fills[0][0].x - g.fills[0][1].x, 9.118f, 1e-2f);
}

TEST(ArrowGeom, NoEndsDrawsOnlyShaft) {
    auto g = buildArrowGeometry(straightPath(), parseArrowStyle("-"), 1.0f);
    EXPECT_EQ(g.strokes.size(), 1u);
    EXPECT_TRUE(g.fills.empty());
}

// ═══ arrowstyle: rendering (macro) ════════════════════════════════════════

TEST(ArrowSpecRegression, FilledHeadRenders) {
    // "-|>" should produce a filled triangle at the annotate target.
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.8f, 0.8f, "");
    a->arrowSpec = parseArrowStyle("-|>");
    a->arrowSpec->mutationSize = 24.0f;
    a->arrowWidth = 2.0f;
    a->arrowColor = Color::black();
    a->shrinkA = 0.0f; a->shrinkB = 0.0f;
    auto img = cf.render();
    // Filled head near the target pixel (128, 128): expect dark pixels
    // in a small neighborhood (the open '->' head is only 2 thin lines;
    // a filled triangle covers more area).
    size_t near = 0;
    for (uint32_t y = 116; y <= 140; ++y)
        for (uint32_t x = 116; x <= 140; ++x)
            if (isBlackish(img.get(x, y))) ++near;
    EXPECT_GT(near, 30u) << "Filled arrow head should cover pixels near tip";
}

TEST(ArrowSpecRegression, DoubleHeadedRendersBothEnds) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.8f, 0.8f, "");
    a->arrowSpec = parseArrowStyle("<|-|>");
    a->arrowSpec->mutationSize = 20.0f;
    a->arrowWidth = 2.0f;
    a->shrinkA = 0.0f; a->shrinkB = 0.0f;
    auto img = cf.render();
    // A-end head near text position (0.8,0.8) → pixel (205,51).
    size_t nearA = 0;
    for (uint32_t y = 45; y <= 60; ++y)
        for (uint32_t x = 195; x <= 215; ++x)
            if (isBlackish(img.get(x, y))) ++nearA;
    EXPECT_GT(nearA, 5u) << "Head should also render at the A end";
}

TEST(ArrowSpecRegression, BracketEndRenders) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.8f, 0.8f, "");
    a->arrowSpec = parseArrowStyle("-[");
    a->arrowSpec->mutationSize = 20.0f;
    a->arrowWidth = 2.0f;
    a->shrinkA = 0.0f; a->shrinkB = 0.0f;
    auto img = cf.render();
    // Bracket bar at the tip (128,128) → a cluster of dark pixels.
    size_t near = 0;
    for (uint32_t y = 122; y <= 134; ++y)
        for (uint32_t x = 122; x <= 134; ++x)
            if (isBlackish(img.get(x, y))) ++near;
    EXPECT_GT(near, 10u) << "Bracket should render a bar at the tip";
}

TEST(ArrowSpecRegression, WedgeBodyRenders) {
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.8f, 0.8f, "");
    a->arrowSpec = parseArrowStyle("wedge");
    a->arrowSpec->mutationSize = 30.0f;
    a->shrinkA = 0.0f; a->shrinkB = 0.0f;
    auto img = cf.render();
    // The wedge fills a triangular region between text and target —
    // dark pixels should appear OFF the shaft line too.
    size_t total = countPixels(img, isBlackish);
    EXPECT_GT(total, 200u) << "Wedge should fill a visible triangular area";
}

TEST(ArrowSpecRegression, SpecNoneStillWorks) {
    // arrowSpec unset → existing enum path unchanged.
    AnnFigure cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.5f, 0.5f, 0.8f, 0.8f, "");
    a->arrowStyle = ArrowStyle::Simple;
    a->arrowWidth = 2.0f;
    a->shrinkA = 0.0f; a->shrinkB = 0.0f;
    auto img = cf.render();
    EXPECT_GT(countPixels(img, isBlackish), 10u);
}
