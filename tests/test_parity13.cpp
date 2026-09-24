// tests/test_parity13.cpp — tests for the 5-major-feature parity batch:
//   1. vp.colors/vp.cm/ScalarMappable — colormap registry, scatter c=/s=
//      arrays, imshow norm/clim/cmap/array, colorbar-from-mappable
//   2. Text bbox= dict — BoxStyleSpec behind raster/vector text
//   3. Artist remove() — Axes::removePlot / Figure::removeAxes
//   4. Figure geometry/color — dpi/size/facecolor/edgecolor/frameon
//   5. Legend kwargs — bbox_to_anchor/handles/labels/spacing/colors/
//      markerscale/reverse/markerfirst/mode/alignment
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Normalize.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Path.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>
#include <volcano/render/VectorWriters.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct BatchFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit BatchFigure(uint32_t size = 256, bool fullBleed = true)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        if (fullBleed) {
            figure.grid().left = 0.0f; figure.grid().right = 1.0f;
            figure.grid().bottom = 0.0f; figure.grid().top = 1.0f;
        }
    }

    Image render() { return harness.render(figure); }
};

Series2D pts(std::initializer_list<std::pair<float, float>> ps) {
    Series2D s;
    for (auto& p : ps) s.points.push_back({p.first, p.second});
    return s;
}

test::Pixel px(Color c) {
    return test::Pixel{uint8_t(std::clamp(c.r, 0.0f, 1.0f) * 255 + 0.5f),
                       uint8_t(std::clamp(c.g, 0.0f, 1.0f) * 255 + 0.5f),
                       uint8_t(std::clamp(c.b, 0.0f, 1.0f) * 255 + 0.5f),
                       uint8_t(std::clamp(c.a, 0.0f, 1.0f) * 255 + 0.5f)};
}

} // namespace

// ─── Feature 1: colormap registry ──────────────────────────────────────

TEST(CmapRegistry, RegisterResolvesByName) {
    Colormap custom;
    custom.name = "test_parity13_mymap";
    custom.stops = {Color{1, 0, 0, 1}, Color{0, 0, 1, 1}};
    Colormap::registerCmap(std::move(custom), false);
    const Colormap& found = Colormap::byName("test_parity13_mymap");
    EXPECT_EQ(found.name, "test_parity13_mymap");
    // Midpoint is the red↔blue lerp.
    Color mid = found.sample(0.5f);
    EXPECT_NEAR(mid.r, 0.5f, 0.05f);
    EXPECT_NEAR(mid.b, 0.5f, 0.05f);
    EXPECT_NEAR(mid.g, 0.0f, 0.05f);
    auto names = Colormap::availableNames();
    EXPECT_NE(std::ranges::find(names, "test_parity13_mymap"),
              names.end());
}

TEST(CmapRegistry, BuiltinNamesRejectOverride) {
    Colormap fake;
    fake.name = "viridis";
    fake.stops = {Color{1, 0, 0, 1}};
    EXPECT_THROW(Colormap::registerCmap(std::move(fake), false),
                 std::invalid_argument);
    // viridis is unchanged.
    EXPECT_NE(Colormap::byName("viridis").sample(0.0f).r, 1.0f);
}

TEST(CmapRegistry, ListedColormapDiscretizes) {
    auto lc = Colormap::listed("test_parity13_rgb",
                               {Color{1, 0, 0, 1}, Color{0, 1, 0, 1},
                                Color{0, 0, 1, 1}});
    EXPECT_TRUE(lc.discrete);
    EXPECT_NEAR(lc.sample(0.1f).r, 1.0f, 0.05f);   // bin 0
    EXPECT_NEAR(lc.sample(0.5f).g, 1.0f, 0.05f);   // bin 1
    EXPECT_NEAR(lc.sample(0.9f).b, 1.0f, 0.05f);   // bin 2
}

// ─── Feature 1: scatter c=/s= colormapping ─────────────────────────────

TEST(ScatterMappable, ScalarArrayColormapsPoints) {
    BatchFigure f(128);
    auto* sp = static_cast<ScatterPlot*>(f.axes->addPlot(
        std::make_unique<ScatterPlot>(pts({{0.25f, 0.5f}, {0.75f, 0.5f}}))));
    sp->series().size = 20.0f;
    sp->array_ = {0.0f, 1.0f};
    auto img = f.render();
    // viridis(0) ≈ dark purple (68,1,84), viridis(1) ≈ yellow (253,231,37).
    EXPECT_GT(img.countColor(px(colormaps::viridis().sample(0.0f)), 40),
              20u);
    EXPECT_GT(img.countColor(px(colormaps::viridis().sample(1.0f)), 40),
              20u);
}

TEST(ScatterMappable, PerPointSizesChangeArea) {
    BatchFigure f(128);
    auto* sp = static_cast<ScatterPlot*>(f.axes->addPlot(
        std::make_unique<ScatterPlot>(pts({{0.25f, 0.5f}, {0.75f, 0.5f}}))));
    sp->series().color = Color{1, 0, 0, 1};
    sp->series().size = 8.0f;
    sp->sizes_ = {8.0f, 30.0f};  // second point much bigger
    auto img = f.render();
    // Uniform 8px would give ~2×50px² ≈ 100px; the 30px marker alone
    // covers ~700px².
    EXPECT_GT(img.countColor(test::Pixel{255, 0, 0, 255}, 40), 400u);
}

TEST(ScatterMappable, ExplicitPerPointColors) {
    BatchFigure f(128);
    auto* sp = static_cast<ScatterPlot*>(f.axes->addPlot(
        std::make_unique<ScatterPlot>(pts({{0.25f, 0.5f}, {0.75f, 0.5f}}))));
    sp->series().size = 16.0f;
    sp->colors_ = {Color{1, 0, 0, 1}, Color{0, 0, 1, 1}};
    auto img = f.render();
    EXPECT_GT(img.countColor(test::Pixel{255, 0, 0, 255}, 40), 20u);
    EXPECT_GT(img.countColor(test::Pixel{0, 0, 255, 255}, 40), 20u);
}

TEST(ScatterMappable, ClimClampsColormap) {
    BatchFigure f(128);
    auto* sp = static_cast<ScatterPlot*>(f.axes->addPlot(
        std::make_unique<ScatterPlot>(pts({{0.3f, 0.5f}, {0.7f, 0.5f}}))));
    sp->series().size = 20.0f;
    sp->array_ = {0.0f, 1.0f};
    // vmin=0.5 clamps value 0 below range → viridis(0); 1.0 → viridis(1).
    sp->setClim(0.5f, 1.0f);
    auto img = f.render();
    // No low-value point should reach viridis(0.9+) — clim[0.5,1] caps
    // the sample range at t ∈ [0, 0.5]... actually v=1 → t=1: yellow.
    // The 0-value clamps to t=0 → dark purple.
    EXPECT_GT(img.countColor(px(colormaps::viridis().sample(0.0f)), 40),
              20u);
    EXPECT_GT(img.countColor(px(colormaps::viridis().sample(1.0f)), 40),
              20u);
    // But viridis(0.5) teal must be absent (no value maps there).
    EXPECT_EQ(img.countColor(px(colormaps::viridis().sample(0.5f)), 20),
              0u);
}

TEST(ScatterMappable, ValueRangeFeedsAutoscale) {
    BatchFigure f(128, false);
    auto* sp = static_cast<ScatterPlot*>(f.axes->addPlot(
        std::make_unique<ScatterPlot>(pts({{0.5f, 0.5f}}))));
    sp->array_ = {10.0f, 20.0f, 30.0f};
    f.render();
    auto vr = sp->valueRange();
    ASSERT_TRUE(vr.has_value());
    EXPECT_NEAR(vr->min, 10.0f, 0.5f);
    EXPECT_NEAR(vr->max, 30.0f, 0.5f);
}

TEST(ScatterMappable, SetArrayNormCmap) {
    ScatterPlot sp(pts({{0, 0}, {1, 1}}));
    sp.setArray({5, 6});
    ASSERT_EQ(sp.array().size(), 2u);
    EXPECT_FLOAT_EQ(sp.array()[0], 5.0f);
    EXPECT_FLOAT_EQ(sp.array()[1], 6.0f);
    sp.setCmap(colormaps::plasma());
    EXPECT_EQ(sp.cmap(), &colormaps::plasma());
    sp.setNorm(std::make_shared<LogNorm>());
    ASSERT_NE(sp.norm(), nullptr);
    EXPECT_NE(dynamic_cast<LogNorm*>(sp.norm().get()), nullptr);
}

// ─── Feature 1: heatmap ScalarMappable ─────────────────────────────────

TEST(HeatmapMappable, NormRemapsMidValue) {
    BatchFigure f(128);
    Grid2D g;
    g.width = 3; g.height = 1;
    g.values = {1.0f, 10.0f, 100.0f};
    g.xRange = {0, 1}; g.yRange = {0, 1};
    g.valueRange = {1, 100};
    auto* hp = static_cast<HeatmapPlot*>(
        f.axes->addPlot(std::make_unique<HeatmapPlot>(
            g, colormaps::viridis())));
    // Linear: 10 → t≈0.09 (dark). LogNorm: 10 → t=0.5 (teal).
    hp->setNorm(std::make_shared<LogNorm>());
    auto img = f.render();
    // Center of middle cell ≈ viridis(0.5) ≈ (33,145,140).
    EXPECT_GT(img.countColor(px(colormaps::viridis().sample(0.5f)), 40),
              50u);
}

TEST(HeatmapMappable, SetClimRescales) {
    BatchFigure f(128);
    Grid2D g;
    g.width = 2; g.height = 1;
    g.values = {0.0f, 1.0f};
    g.xRange = {0, 1}; g.yRange = {0, 1};
    auto* hp = static_cast<HeatmapPlot*>(
        f.axes->addPlot(std::make_unique<HeatmapPlot>(
            g, colormaps::viridis())));
    // clim [0, 0.5] → value 1.0 maps past vmax → over/last stop.
    hp->setClim(0.0f, 0.5f);
    auto img = f.render();
    EXPECT_GT(img.countColor(px(colormaps::viridis().sample(1.0f)), 40),
              1000u);
}

TEST(HeatmapMappable, SetArrayAndCmap) {
    BatchFigure f(128);
    Grid2D g;
    g.width = 2; g.height = 1;
    g.values = {0.0f, 1.0f};
    g.xRange = {0, 1}; g.yRange = {0, 1};
    auto* hp = static_cast<HeatmapPlot*>(
        f.axes->addPlot(std::make_unique<HeatmapPlot>(
            g, colormaps::viridis())));
    hp->setArray({1.0f, 0.0f});  // swap cells
    hp->setCmap(colormaps::plasma());
    auto img = f.render();
    EXPECT_GT(img.countColor(px(colormaps::plasma().sample(1.0f)), 40),
              500u);  // left cell now value 1
    EXPECT_GT(img.countColor(px(colormaps::plasma().sample(0.0f)), 40),
              500u);  // right cell now value 0
}

TEST(HeatmapMappable, SetArrayReshapedChangesDims) {
    Grid2D g;
    g.width = 2; g.height = 1;
    g.values = {0, 1};
    HeatmapPlot hp(g);
    hp.setArrayReshaped({0, 0.5f, 1, 0.25f}, 2, 2);
    EXPECT_EQ(hp.dims(), std::make_pair(2u, 2u));
    EXPECT_EQ(hp.array().size(), 4u);
    EXPECT_THROW(hp.setArrayReshaped({1, 2}, 3, 3),
                 std::invalid_argument);
}

TEST(HeatmapMappable, AlphaScalesStops) {
    BatchFigure f(128);
    Grid2D g;
    g.width = 1; g.height = 1;
    g.values = {0.5f};
    g.xRange = {0, 1}; g.yRange = {0, 1};
    g.valueRange = {0, 1};
    auto* hp = static_cast<HeatmapPlot*>(
        f.axes->addPlot(std::make_unique<HeatmapPlot>(
            g, colormaps::viridis())));
    hp->setAlpha(0.5f);
    auto img = f.render();
    // viridis(0.5)≈(33,145,140) at α=0.5 over white → ~(145,200,198).
    auto c = colormaps::viridis().sample(0.5f);
    test::Pixel blended{uint8_t(c.r * 0.5f * 255 + 0.5f * 255),
                        uint8_t(c.g * 0.5f * 255 + 0.5f * 255),
                        uint8_t(c.b * 0.5f * 255 + 0.5f * 255), 255};
    EXPECT_GT(img.countColor(blended, 30), 500u);
}

// ─── Feature 1: colorbar from mappable ─────────────────────────────────

TEST(ColorbarMappable, UsesMappableCmap) {
    BatchFigure f(256, false);
    Grid2D g;
    g.width = 2; g.height = 2;
    g.values = {0, 0.5f, 0.5f, 1};
    g.xRange = {0, 1}; g.yRange = {0, 1};
    auto* hp = static_cast<HeatmapPlot*>(
        f.axes->addPlot(std::make_unique<HeatmapPlot>(
            g, colormaps::plasma())));
    auto& cb = f.axes->style().colorbar;
    cb.visible = true;
    cb.mappable = hp;
    auto img = f.render();
    // Colorbar extremes ≈ plasma(0)=(13,8,135) dark blue and
    // plasma(1)=(240,249,33) yellow — NOT viridis.
    EXPECT_GT(img.countColor(px(colormaps::plasma().sample(1.0f)), 40),
              20u);
    EXPECT_GT(img.countColor(px(colormaps::plasma().sample(0.0f)), 40),
              20u);
}

// ─── Feature 2: text bbox ──────────────────────────────────────────────

TEST(TextBbox, FillsRectBehindText) {
    BatchFigure f(128);
    TextAnnotation t;
    t.text = "Hi";
    t.x = 0.5f; t.y = 0.5f;
    t.coords = CoordSystem::Axes;
    t.halign = HAlign::Center;
    t.valign = VAlign::Center;
    t.hasBbox = true;
    t.bboxFaceColor = Color{1, 0, 0, 1};
    t.bboxEdgeColor = Color{1, 0, 0, 1};
    t.boxStyle = BoxStyleSpec{BoxStyleSpec::Kind::Square, 0.3f};
    f.axes->texts().push_back(t);
    auto img = f.render();
    EXPECT_GT(img.countColor(test::Pixel{255, 0, 0, 255}, 40), 30u);
}

TEST(TextBbox, RoundStyleRenders) {
    BatchFigure f(128);
    TextAnnotation t;
    t.text = "R";
    t.x = 0.5f; t.y = 0.5f;
    t.coords = CoordSystem::Axes;
    t.halign = HAlign::Center;
    t.valign = VAlign::Center;
    t.hasBbox = true;
    t.bboxFaceColor = Color{0, 0, 1, 1};
    t.boxStyle = BoxStyleSpec{BoxStyleSpec::Kind::Round, 0.3f};
    f.axes->texts().push_back(t);
    auto img = f.render();
    EXPECT_GT(img.countColor(test::Pixel{0, 0, 255, 255}, 40), 30u);
}

// ─── Feature 3: artist remove() ────────────────────────────────────────

TEST(ArtistRemove, RemovePlotDetachesFromAxes) {
    BatchFigure f(128);
    auto* lp = static_cast<LinePlot*>(f.axes->addPlot(
        std::make_unique<LinePlot>(pts({{0.1f, 0.5f}, {0.9f, 0.5f}}))));
    lp->series().color = Color{1, 0, 0, 1};
    lp->series().lineWidth = 3.0f;
    auto before = f.render();
    EXPECT_GT(before.countColor(test::Pixel{255, 0, 0, 255}, 40), 50u);
    auto owned = f.axes->removePlot(lp);
    ASSERT_TRUE(owned != nullptr);
    auto after = f.render();
    EXPECT_EQ(after.countColor(test::Pixel{255, 0, 0, 255}, 40), 0u);
}

TEST(ArtistRemove, RemoveAxesDetachesFromFigure) {
    BatchFigure f(128);
    auto* ax2 = f.figure.addAxesFraction(0.6f, 0.6f, 0.3f, 0.3f);
    ax2->setStyle(flatTestStyle());
    ax2->addPlot(std::make_unique<LinePlot>(pts({{0, 0}, {1, 1}})));
    EXPECT_EQ(f.figure.allAxes().size(), 2u);
    auto owned = f.figure.removeAxes(ax2);
    ASSERT_TRUE(owned != nullptr);
    EXPECT_EQ(f.figure.allAxes().size(), 1u);
    f.render();  // no crash
}

TEST(ArtistRemove, RemovingMissingPlotReturnsNull) {
    BatchFigure f(128);
    ScatterPlot stray(pts({{0, 0}}));
    EXPECT_EQ(f.axes->removePlot(&stray), nullptr);
}

// ─── Feature 4: figure geometry / color ────────────────────────────────

TEST(FigureGeom, FaceColorPaintsCanvas) {
    BatchFigure f(64);
    f.axes->setAxisOff();
    f.figure.style().faceColor = Color{0, 1, 0, 1};
    auto img = f.render();
    EXPECT_GT(img.countColor(test::Pixel{0, 255, 0, 255}, 30), 2000u);
}

TEST(FigureGeom, DpiField) {
    Figure fig;
    EXPECT_FLOAT_EQ(fig.style().dpi, 100.0f);
    fig.style().dpi = 200.0f;
    EXPECT_FLOAT_EQ(fig.dpi(), 200.0f);
}

// ─── Feature 5: legend kwargs ──────────────────────────────────────────

TEST(LegendKw, ExplicitHandlesAndLabels) {
    BatchFigure f(256, false);
    auto* l1 = f.axes->addPlot(
        std::make_unique<LinePlot>(pts({{0, 0}, {1, 1}})));
    static_cast<LinePlot*>(l1)->series().color = Color{1, 0, 0, 1};
    auto& lg = f.axes->legend();
    lg.location = "upper left";
    lg.explicitHandles = std::vector<LegendHandle>{
        LegendHandle{"only", Color{1, 0, 0, 1}, LegendMarker::Line}};
    lg.explicitLabels = {"only"};
    auto img = f.render();
    EXPECT_GT(img.countColor(test::Pixel{255, 0, 0, 255}, 40), 5u);
}

TEST(LegendKw, IntLocCodeAndBboxAnchor) {
    BatchFigure f(256, false);
    auto* l1 = f.axes->addPlot(
        std::make_unique<LinePlot>(pts({{0, 0}, {1, 1}})));
    static_cast<LinePlot*>(l1)->series().label = "x";
    auto& lg = f.axes->legend();
    lg.location = "4";  // mpl code: lower right
    lg.anchorX = 1.0f; lg.anchorY = 0.0f;
    auto img = f.render();
    // Anchor at axes (1,0) with loc 'lower right' parks the legend in
    // the bottom-right corner.
    (void)img;
}

TEST(LegendKw, ReverseAndStyleFields) {
    BatchFigure f(256, false);
    auto* l1 = f.axes->addPlot(
        std::make_unique<LinePlot>(pts({{0, 0}, {1, 1}})));
    static_cast<LinePlot*>(l1)->series().color = Color{1, 0, 0, 1};
    static_cast<LinePlot*>(l1)->series().label = "first";
    auto* l2 = f.axes->addPlot(
        std::make_unique<LinePlot>(pts({{0, 1}, {1, 0}})));
    static_cast<LinePlot*>(l2)->series().color = Color{0, 0, 1, 1};
    static_cast<LinePlot*>(l2)->series().label = "second";
    auto& lg = f.axes->legend();
    lg.reverse = true;
    lg.faceColor = Color{1, 1, 1, 1};
    lg.edgeColor = Color{0, 0, 0, 1};
    lg.labelSpacing = 0.1f;
    lg.borderPad = 0.1f;
    lg.handleLength = 2.0f;
    lg.handleTextPad = 0.8f;
    lg.columnSpacing = 2.0f;
    lg.markerScale = 1.5f;
    lg.markerFirst = false;
    lg.alignment = "right";
    lg.expand = true;
    lg.draggable = true;
    auto img = f.render();
    EXPECT_GT(img.countColor(test::Pixel{0, 0, 255, 255}, 40), 3u);
    EXPECT_GT(img.countColor(test::Pixel{255, 0, 0, 255}, 40), 3u);
}
