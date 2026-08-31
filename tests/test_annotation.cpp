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
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    auto p = toDisplay(0.5f, 0.5f, CoordSystem::Data, rect, fig, vp);
    EXPECT_NEAR(p.x, 50.0f, 0.5f);
    EXPECT_NEAR(p.y, 50.0f, 0.5f);
}

TEST(AnnotationTransform, DataCoordsOrigin) {
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    // (0, 0) in data → bottom-left of axes → pixel (0, 100) (Y-down)
    auto p = toDisplay(0.0f, 0.0f, CoordSystem::Data, rect, fig, vp);
    EXPECT_NEAR(p.x, 0.0f, 0.5f);
    EXPECT_NEAR(p.y, 100.0f, 0.5f);
}

TEST(AnnotationTransform, DataCoordsTopRight) {
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    auto p = toDisplay(1.0f, 1.0f, CoordSystem::Data, rect, fig, vp);
    EXPECT_NEAR(p.x, 100.0f, 0.5f);
    EXPECT_NEAR(p.y, 0.0f, 0.5f);
}

TEST(AnnotationTransform, AxesCoords) {
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{10, 20, 100, 200};
    Extent2D fig{200, 300};
    // (0, 0) in axes fraction → bottom-left of axes rect
    auto p0 = toDisplay(0.0f, 0.0f, CoordSystem::Axes, rect, fig, vp);
    EXPECT_NEAR(p0.x, 10.0f, 0.5f);
    EXPECT_NEAR(p0.y, 220.0f, 0.5f);  // 20 + 200
    // (1, 1) → top-right
    auto p1 = toDisplay(1.0f, 1.0f, CoordSystem::Axes, rect, fig, vp);
    EXPECT_NEAR(p1.x, 110.0f, 0.5f);  // 10 + 100
    EXPECT_NEAR(p1.y, 20.0f, 0.5f);
    // (0.5, 0.5) → center
    auto pc = toDisplay(0.5f, 0.5f, CoordSystem::Axes, rect, fig, vp);
    EXPECT_NEAR(pc.x, 60.0f, 0.5f);   // 10 + 50
    EXPECT_NEAR(pc.y, 120.0f, 0.5f);  // 20 + 100
}

TEST(AnnotationTransform, FigureCoords) {
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{10, 20, 100, 200};
    Extent2D fig{200, 300};
    // (0, 0) in figure fraction → bottom-left of figure
    auto p0 = toDisplay(0.0f, 0.0f, CoordSystem::Figure, rect, fig, vp);
    EXPECT_NEAR(p0.x, 0.0f, 0.5f);
    EXPECT_NEAR(p0.y, 300.0f, 0.5f);
    // (1, 1) → top-right
    auto p1 = toDisplay(1.0f, 1.0f, CoordSystem::Figure, rect, fig, vp);
    EXPECT_NEAR(p1.x, 200.0f, 0.5f);
    EXPECT_NEAR(p1.y, 0.0f, 0.5f);
}

TEST(AnnotationTransform, DisplayCoords) {
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    auto p = toDisplay(42.0f, 58.0f, CoordSystem::Display, rect, fig, vp);
    EXPECT_NEAR(p.x, 42.0f, 0.5f);
    EXPECT_NEAR(p.y, 58.0f, 0.5f);
}

TEST(AnnotationTransform, OffsetPoints) {
    // Data (0.5, 0.5) with offset (72, 72) points at 72 DPI
    // → 72pt = 72px right, 72px up
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    float dpi = 72.0f;
    auto p = toDisplay(0.5f, 0.5f, CoordSystem::OffsetPoints, rect, fig, vp,
                       dpi, 72.0f, 72.0f);
    // Base: (50, 50). Offset: +72px x, -72px y (Y-up → Y-down)
    EXPECT_NEAR(p.x, 122.0f, 0.5f);
    EXPECT_NEAR(p.y, -22.0f, 0.5f);
}

TEST(AnnotationTransform, OffsetPointsDpi100) {
    // At 100 DPI, 72pt = 100px
    Viewport vp{0, 1, 0, 1, 0, 1};
    Rect2D rect{0, 0, 100, 100};
    Extent2D fig{100, 100};
    float dpi = 100.0f;
    auto p = toDisplay(0.0f, 0.0f, CoordSystem::OffsetPoints, rect, fig, vp,
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
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
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
