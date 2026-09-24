// tests/test_parity12.cpp — tests for the 5-major-feature parity batch:
//   1. patheffects — Stroke/LineShadow/PatchShadow/Normal on artists
//   2. Axis placement — set_ticks_position/set_label_position/tick_top
//   3. Artist transform= — transAxes/transFigure on artists + autoscale
//   4. savefig kwargs — dpi/transparent/facecolor/tight (raster+vector)
//   5. Colorbar object — mappable/ticks/ticklabels/minorticks
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Transform.hpp>
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

constexpr test::Pixel Red{255, 0, 0, 255};
constexpr test::Pixel Black{0, 0, 0, 255};

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

std::unique_ptr<LinePlot> redLine() {
    Series2D s;
    s.points = {{0.1f, 0.5f}, {0.9f, 0.5f}};
    s.color = Color{1, 0, 0, 1};
    s.lineWidth = 2.0f;
    return std::make_unique<LinePlot>(std::move(s));
}

std::string readFile(const std::string& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct TmpFile {
    std::filesystem::path path;
    explicit TmpFile(std::string name)
        : path(std::filesystem::temp_directory_path() / name) {}
    ~TmpFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

std::vector<uint8_t> readBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}

} // namespace

// ─── PathEffect helpers ─────────────────────────────────────────────────────

TEST(PathEffect, OffsetPxConvertsPointsAndInvertsY) {
    PathEffect e;
    e.offset = {7.2f, 3.6f};           // mpl points, y up
    auto px = e.offsetPx(144.0f);      // 2px per pt at 144 dpi
    EXPECT_FLOAT_EQ(px.x, 14.4f);
    EXPECT_FLOAT_EQ(px.y, -7.2f);      // display y is down
}

TEST(PathEffect, ShadowForDarkensBaseAndSetsAlpha) {
    PathEffect e;                       // mpl rho=0.3, alpha=0.3
    auto c = e.shadowFor(Color{1.0f, 0.5f, 0.0f, 1.0f});
    EXPECT_FLOAT_EQ(c.r, 0.3f);
    EXPECT_FLOAT_EQ(c.g, 0.15f);
    EXPECT_FLOAT_EQ(c.a, 0.3f);
    e.shadowColor = Color::blue();
    e.shadowAlpha = 0.9f;
    auto c2 = e.shadowFor(Color::red());
    EXPECT_FLOAT_EQ(c2.b, 1.0f);
    EXPECT_FLOAT_EQ(c2.a, 0.9f);
}

TEST(PathEffect, StrokeWidthFallsBackToArtist) {
    PathEffect e;
    EXPECT_FLOAT_EQ(e.strokeWidthPx(4.0f, 100.0f), 4.0f);
    e.lineWidth = 7.2f;                 // 7.2pt at 72dpi = 7.2px
    EXPECT_FLOAT_EQ(e.strokeWidthPx(4.0f, 72.0f), 7.2f);
}

// ─── Path effects: raster ───────────────────────────────────────────────────

TEST(PathEffect, StrokeWidensLine) {
    BatchFigure noFx, fx;
    noFx.axes->addPlot(redLine());
    auto img0 = noFx.render();

    auto lp = redLine();
    PathEffect stroke;
    stroke.kind = PathEffect::Kind::Stroke;
    stroke.foreground = Color{0, 0, 0, 1};
    stroke.lineWidth = 8.0f;
    stroke.thenNormal = true;   // mpl withStroke
    lp->pathEffects = {stroke};
    fx.axes->addPlot(std::move(lp));
    auto img1 = fx.render();

    EXPECT_GT(img1.countColor(Black, 30), img0.countColor(Black, 30));
    // withStroke still draws the artist normally on top.
    EXPECT_GT(img1.countColor(Red, 30), 0u);
}

TEST(PathEffect, LineShadowOffsetsCopy) {
    BatchFigure fx;
    auto lp = redLine();
    PathEffect sh;
    sh.kind = PathEffect::Kind::LineShadow;
    sh.shadowColor = Color{0, 0, 0, 1};
    sh.shadowAlpha = 1.0f;
    sh.offset = {0.0f, 14.4f};          // +14.4pt up → ~20px up
    lp->pathEffects = {sh};
    fx.axes->addPlot(std::move(lp));
    auto img = fx.render();

    auto bb = img.boundingBox(Black, 30);
    ASSERT_TRUE(bb.found);
    // Shadow sits above (mpl +y is up) the line at y=0.5 → row 128.
    EXPECT_LT(int(bb.y0 + bb.y1) / 2, 128 - 10);
}

TEST(PathEffect, PatchShadowFillsOffsetCopy) {
    BatchFigure fx;
    Series2D s;
    s.points = {{0.5f, 0.5f}};
    s.color = Color{1, 0, 0, 1};
    s.size = 20.0f;
    auto sp = std::make_unique<ScatterPlot>(std::move(s));
    PathEffect sh;
    sh.kind = PathEffect::Kind::PatchShadow;
    sh.shadowColor = Color{0, 0, 0, 1};
    sh.shadowAlpha = 1.0f;
    sh.offset = {7.2f, -7.2f};          // right + down in display px
    sh.thenNormal = true;
    sp->pathEffects = {sh};
    fx.axes->addPlot(std::move(sp));
    auto img = fx.render();
    // Shadow pixels sit to the lower-right of center.
    auto bb = img.boundingBox(Black, 30);
    ASSERT_TRUE(bb.found);
    EXPECT_GT(int(bb.x0 + bb.x1) / 2, 128);
    EXPECT_GT(int(bb.y0 + bb.y1) / 2, 128);
}

// ─── Path effects: vector ───────────────────────────────────────────────────

TEST(PathEffect, VectorEmitsExtraPaths) {
    auto svgFor = [](bool withFx) {
        PlotTestHarness harness(256, 256);
        Figure fig;
        auto* ax = fig.addAxes();
        auto lp = redLine();
        if (withFx) {
            PathEffect sh;
            sh.kind = PathEffect::Kind::Stroke;
            sh.foreground = Color{0, 0, 0, 1};
            sh.lineWidth = 6.0f;
            sh.thenNormal = true;
            lp->pathEffects = {sh};
        }
        ax->addPlot(std::move(lp));
        TmpFile f("volcano_pfx.svg");
        EXPECT_TRUE(harness.renderer().savefig(fig, f.path));
        return readFile(f.path);
    };
    auto count = [](const std::string& doc) {
        size_t n = 0, pos = 0;
        while ((pos = doc.find("<path", pos)) != std::string::npos) {
            ++n; ++pos;
        }
        return n;
    };
    EXPECT_GT(count(svgFor(true)), count(svgFor(false)));
}

// ─── Axis placement ─────────────────────────────────────────────────────────

TEST(AxisPlacement, TicksPositionMapping) {
    BatchFigure fx;
    fx.axes->setXTicksPosition("top");
    EXPECT_EQ(fx.axes->xTicksPosition(), "top");
    fx.axes->setXTicksPosition("bottom");
    EXPECT_EQ(fx.axes->xTicksPosition(), "bottom");
    // mpl: 'both' leaves labels alone → near labels + both marks =
    // 'default' when labels are near, 'unknown' when far.
    fx.axes->setXTicksPosition("top");
    fx.axes->setXTicksPosition("both");
    EXPECT_EQ(fx.axes->xTicksPosition(), "unknown");
    fx.axes->setXTicksPosition("default");
    EXPECT_EQ(fx.axes->xTicksPosition(), "default");
    fx.axes->setXTicksPosition("none");
    EXPECT_EQ(fx.axes->xTicksPosition(), "unknown");
    EXPECT_THROW(fx.axes->setXTicksPosition("left"),
                 std::invalid_argument);
    EXPECT_THROW(fx.axes->setYTicksPosition("top"),
                 std::invalid_argument);
}

TEST(AxisPlacement, TickTopPreservesDisabledLabels) {
    BatchFigure fx;
    auto& f = fx.axes->xFurniture();
    f.labelsNear = false; f.labelsFar = false;   // labels off both sides
    fx.axes->xaxisTickTop();
    EXPECT_TRUE(fx.axes->xFurniture().marksFar);
    EXPECT_FALSE(fx.axes->xFurniture().labelsFar);
    // With labels on, tick_top moves them.
    fx.axes->setXTicksPosition("bottom");
    fx.axes->xaxisTickTop();
    EXPECT_TRUE(fx.axes->xFurniture().labelsFar);
}

TEST(AxisPlacement, LabelPositionIndependent) {
    BatchFigure fx;
    EXPECT_EQ(fx.axes->xLabelPosition(), "bottom");
    EXPECT_EQ(fx.axes->yLabelPosition(), "left");
    fx.axes->setXLabelPosition("top");
    fx.axes->setYLabelPosition("right");
    EXPECT_EQ(fx.axes->xLabelPosition(), "top");
    EXPECT_EQ(fx.axes->yLabelPosition(), "right");
    // Tick positions unaffected.
    EXPECT_EQ(fx.axes->xTicksPosition(), "bottom");
    EXPECT_THROW(fx.axes->setXLabelPosition("left"),
                 std::invalid_argument);
}

TEST(AxisPlacement, TopTickLabelsRender) {
    // Non-fullbleed so the top margin exists.
    BatchFigure fx(256, false);
    fx.axes->style().xAxis.visible = true;
    fx.axes->setXTicksPosition("top");
    auto img = fx.render();
    // Tick labels in the top margin → dark pixels above the axes rect.
    uint32_t topH = uint32_t(fx.axes->rect.y);
    EXPECT_GT(img.countColorInRegion(Black, 0, 0, 256, topH, 60), 0u);
}

// ─── Artist transforms ──────────────────────────────────────────────────────

TEST(ArtistTransform, AffineConcatOrder) {
    // concat(o): *this applied after o (mpl dot).
    auto t = Affine2D::scale(2.0f).concat(Affine2D::translate(1.0f, 0.0f));
    auto p = t.apply({1.0f, 1.0f});
    EXPECT_FLOAT_EQ(p.x, 4.0f);  // (1+1)*2
    EXPECT_FLOAT_EQ(p.y, 2.0f);
}

TEST(ArtistTransform, TransAxesMapsFractionToPixels) {
    BatchFigure fx;
    fx.figure.layout(Extent2D{256, 256});
    auto ta = fx.axes->transAxes();
    auto p = ta->apply({0.5f, 0.5f});
    const auto& r = fx.axes->rect;
    EXPECT_NEAR(p.x, r.x + r.width * 0.5f, 1.5f);
    EXPECT_NEAR(p.y, r.y + r.height * 0.5f, 1.5f);
}

TEST(ArtistTransform, AxesTransformSkipsDataLim) {
    BatchFigure fx;
    fx.axes->addPlot(redLine());
    auto lp = redLine();
    lp->transform = fx.axes->transAxes();   // axes coords, not data
    fx.axes->addPlot(std::move(lp));
    fx.axes->relim();
    // dataLim from the untransformed line only.
    auto lim = fx.axes->dataLim();
    EXPECT_NEAR(lim.x.min, 0.1f, 1e-5f);
    EXPECT_NEAR(lim.x.max, 0.9f, 1e-5f);
    auto feeds = fx.axes->plots()[1]->feedsData(*fx.axes);
    EXPECT_FALSE(feeds.first);
    EXPECT_FALSE(feeds.second);
}

TEST(ArtistTransform, LineInAxesCoordsRenders) {
    BatchFigure fx;
    fx.axes->setXlim(0, 1);
    fx.axes->setYlim(0, 1);
    auto lp = redLine();                     // spans x=[.1,.9] axes-frac
    lp->transform = fx.axes->transAxes();
    fx.axes->addPlot(std::move(lp));
    auto img = fx.render();
    auto bb = img.boundingBox(Red, 30);
    ASSERT_TRUE(bb.found);
    // Line at axes-y 0.5 → pixel row ~128 (Y-up → middle).
    EXPECT_NEAR(int(bb.y0 + bb.y1) / 2, 128, 8);
}

TEST(ArtistTransform, TextTransformOverridesCoordSystem) {
    BatchFigure fx;
    fx.axes->setXlim(0, 1);
    fx.axes->setYlim(0, 1);
    auto* t = fx.axes->text(0.5f, 0.5f, "T", CoordSystem::Data);
    t->transform = fx.axes->transAxes();
    t->color = Color{0, 0, 0, 1};
    t->fontSize = 36.0f;
    auto img = fx.render();
    // "T" drawn at axes center regardless of viewport.
    auto bb = img.boundingBox(Black, 30);
    ASSERT_TRUE(bb.found);
    EXPECT_NEAR(int(bb.x0 + bb.x1) / 2, 128, 30);
    EXPECT_NEAR(int(bb.y0 + bb.y1) / 2, 128, 30);
}

// ─── savefig ────────────────────────────────────────────────────────────────
// `.raw` output is a plain RGBA8 dump — readable without a PNG decoder.

TEST(SavefigKw, FacecolorPaintsBackground) {
    PlotTestHarness harness(128, 128);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(redLine());
    encode::SaveOptions opts;
    opts.facecolor = std::array{0.0f, 1.0f, 0.0f, 1.0f};
    TmpFile f("volcano_fc.raw");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path, opts));
    auto px = readBytes(f.path);
    ASSERT_EQ(px.size(), 128u * 128u * 4u);
    // Corner pixel = green figure facecolor.
    EXPECT_EQ(px[0], 0); EXPECT_EQ(px[1], 255);
    EXPECT_EQ(px[2], 0); EXPECT_EQ(px[3], 255);
}

TEST(SavefigKw, TransparentClearsAlpha) {
    PlotTestHarness harness(128, 128);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(redLine());
    encode::SaveOptions opts;
    opts.transparent = true;
    TmpFile f("volcano_tr.raw");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path, opts));
    auto px = readBytes(f.path);
    ASSERT_EQ(px.size(), 128u * 128u * 4u);
    EXPECT_EQ(px[3], 0);   // corner alpha
    // mpl transparent=True clears the axes patch too — a pixel inside
    // the axes region but off the line must also be alpha 0.
    const size_t inner = (30u * 128u + 60u) * 4u;
    EXPECT_EQ(px[inner + 3], 0);
    // The red line itself must still draw with alpha (blend "over"
    // semantics — alpha accumulates over the transparent clear).
    bool foundOpaque = false;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i + 3] > 200 && px[i] > 200 && px[i + 1] < 60)
            foundOpaque = true;
    EXPECT_TRUE(foundOpaque);
}

TEST(SavefigKw, TightCropsToContent) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(redLine());
    encode::SaveOptions opts;
    opts.tight = true;
    opts.padInches = 0.0f;
    TmpFile f("volcano_tight.raw");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path, opts));
    auto px = readBytes(f.path);
    // Cropped output must be strictly smaller than the canvas dump.
    EXPECT_LT(px.size(), 256u * 256u * 4u);
    EXPECT_GT(px.size(), 0u);
    EXPECT_EQ(px.size() % 4u, 0u);
}

TEST(SavefigKw, VectorTightShrinksViewBox) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(redLine());
    encode::SaveOptions opts;
    opts.tight = true;
    TmpFile f("volcano_tight.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path, opts));
    auto doc = readFile(f.path);
    auto pos = doc.find("viewBox=\"0 0 ");
    ASSERT_NE(pos, std::string::npos);
    float w = 0, h = 0;
    sscanf(doc.c_str() + pos, "viewBox=\"0 0 %f %f", &w, &h);
    EXPECT_LT(w, 256.0f);
    EXPECT_LT(h, 256.0f);
}

TEST(SavefigKw, DpiRescalesOutput) {
    PlotTestHarness harness(128, 128);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(redLine());
    encode::SaveOptions opts;
    opts.dpi = 200.0f;
    TmpFile f("volcano_dpi.raw");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path, opts));
    auto px = readBytes(f.path);
    EXPECT_EQ(px.size(), 256u * 256u * 4u);   // 128px @ dpi 200
}

// ─── Colorbar ───────────────────────────────────────────────────────────────

TEST(ColorbarApi, MappableSelectsValueRange) {
    BatchFigure fx;
    Grid2D grid;
    grid.width = 2; grid.height = 2;
    grid.values = {10.0f, 20.0f, 30.0f, 40.0f};
    auto& hm = fx.axes->imshow(std::move(grid));
    auto& cb = fx.axes->style().colorbar;
    cb.visible = true;
    cb.mappable = &hm;
    // The renderer resolves the range from the mappable; smoke-render.
    auto img = fx.render();
    EXPECT_GT(img.countIf([](uint32_t, uint32_t, Pixel p) {
        return p.a > 0 && !(p.r > 230 && p.g > 230 && p.b > 230);
    }), 100u);
}

TEST(ColorbarApi, CustomTicksAndLabelsStored) {
    BatchFigure fx;
    auto& cb = fx.axes->style().colorbar;
    cb.ticks = {0.0f, 0.5f, 1.0f};
    cb.tickLabels = {"lo", "mid", "hi"};
    cb.minorTicksOn = true;
    ASSERT_EQ(cb.ticks.size(), 3u);
    EXPECT_EQ(cb.tickLabels[1], "mid");
    EXPECT_TRUE(cb.minorTicksOn);
}
