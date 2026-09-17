// tests/test_vector.cpp — vector backend (SVG/PDF/EPS/PGF) export tests
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>
#include <volcano/plot/plots/StemPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/HistPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/PiePlot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/Collections.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace volcano;
using namespace volcano::plot;
using namespace volcano::test;

namespace {

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
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

/// Minimal figure: one axes, one line plot.
std::unique_ptr<LinePlot> makeLine() {
    Series2D s;
    s.points = {{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.2f}};
    s.color = Color::fromRgba8(255, 0, 0, 255);
    s.lineWidth = 2.0f;
    return std::make_unique<LinePlot>(std::move(s));
}

std::unique_ptr<ScatterPlot> makeScatter() {
    Series2D s;
    s.points = {{0.25f, 0.25f}, {0.5f, 0.5f}, {0.75f, 0.75f}};
    s.color = Color::fromRgba8(0, 0, 255, 255);
    s.size = 8.0f;
    return std::make_unique<ScatterPlot>(std::move(s));
}

} // namespace

// ─── SVG ────────────────────────────────────────────────────────────────────

TEST(VectorSvg, DocumentStructure) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_TRUE(doc.starts_with("<?xml"));
    EXPECT_NE(doc.find("<svg"), std::string::npos);
    EXPECT_NE(doc.find("viewBox=\"0 0 256 256\""), std::string::npos);
    EXPECT_NE(doc.find("</svg>"), std::string::npos);
}

TEST(VectorSvg, LineEmitsNativePath) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec_line.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // A stroked red path — the line geometry, not a bitmap.
    EXPECT_NE(doc.find("<path"), std::string::npos);
    EXPECT_NE(doc.find("stroke=\"#ff0000\""), std::string::npos);
    EXPECT_EQ(doc.find("<image"), std::string::npos);
}

TEST(VectorSvg, DashedLineEmitsDashArray) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    auto lp = makeLine();
    lp->series().lineStyle = LineStyle::Dashed;
    ax->addPlot(std::move(lp));
    TmpFile f("volcano_vec_dash.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("stroke-dasharray"), std::string::npos);
}

TEST(VectorSvg, ScatterEmitsMarkerPaths) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeScatter());
    TmpFile f("volcano_vec_scatter.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // Three blue circle markers → three filled paths.
    size_t n = 0, pos = 0;
    while ((pos = doc.find("fill=\"#0000ff\"", pos)) != std::string::npos) {
        ++n; ++pos;
    }
    EXPECT_GE(n, 3u);
}

TEST(VectorSvg, RasterizedLayerEmbedsImage) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    auto lp = makeLine();
    auto* lpPtr = lp.get();
    ax->addPlot(std::move(lp));
    lpPtr->rasterized = true;
    TmpFile f("volcano_vec_raster.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find("<image"), std::string::npos);
    EXPECT_NE(doc.find("data:image/png;base64,"), std::string::npos);
}

TEST(VectorSvg, ZOrderPreservesDrawOrder) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    auto red = makeLine();  // zorder 0
    ax->addPlot(std::move(red));
    auto blue = makeScatter();  // zorder 10 → drawn after
    blue->zorder = 10.0f;
    ax->addPlot(std::move(blue));
    TmpFile f("volcano_vec_z.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    auto redPos = doc.find("stroke=\"#ff0000\"");
    auto bluePos = doc.find("fill=\"#0000ff\"");
    ASSERT_NE(redPos, std::string::npos);
    ASSERT_NE(bluePos, std::string::npos);
    EXPECT_LT(redPos, bluePos);  // lower zorder emitted first
}

TEST(VectorSvg, ClippedToAxesRect) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec_clip.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("clip-path"), std::string::npos);
}

TEST(VectorSvg, MetadataComment) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    encode::SaveOptions opts;
    opts.metadata["Title"] = "Vector Test";
    TmpFile f("volcano_vec_meta.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path, opts));
    EXPECT_NE(readText(f.path).find("Vector Test"), std::string::npos);
}

// ─── PDF ────────────────────────────────────────────────────────────────────

TEST(VectorPdf, DocumentStructure) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec.pdf");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_TRUE(doc.starts_with("%PDF-"));
    EXPECT_NE(doc.find("%%EOF"), std::string::npos);
    EXPECT_NE(doc.find("/MediaBox"), std::string::npos);
    EXPECT_NE(doc.find("stream"), std::string::npos);
    EXPECT_NE(doc.find("trailer"), std::string::npos);
}

TEST(VectorPdf, RasterizedEmbedsXObject) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    auto lp = makeLine();
    auto* lpPtr = lp.get();
    ax->addPlot(std::move(lp));
    lpPtr->rasterized = true;
    TmpFile f("volcano_vec_raster.pdf");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find("/Subtype /Image"), std::string::npos);
    EXPECT_NE(doc.find("/SMask"), std::string::npos);
}

// ─── EPS ────────────────────────────────────────────────────────────────────

TEST(VectorEps, DocumentStructure) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec.eps");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_TRUE(doc.starts_with("%!PS-Adobe"));
    EXPECT_NE(doc.find("%%BoundingBox:"), std::string::npos);
    EXPECT_NE(doc.find("stroke"), std::string::npos);
    EXPECT_NE(doc.find("showpage"), std::string::npos);
}

// ─── PGF ────────────────────────────────────────────────────────────────────

TEST(VectorPgf, DocumentStructure) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec.pgf");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find("\\pgfpathmoveto"), std::string::npos);
    EXPECT_NE(doc.find("\\pgfusepath{stroke}"), std::string::npos);
}

TEST(VectorPgf, RasterizedWritesPngSidecar) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    auto lp = makeLine();
    auto* lpPtr = lp.get();
    ax->addPlot(std::move(lp));
    lpPtr->rasterized = true;
    TmpFile f("volcano_vec_raster.pgf");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find("\\pgfdeclareimage"), std::string::npos);
    // Sidecar PNG written next to the .pgf.
    auto sidecar = f.path.parent_path() / "volcano_vec_raster-img0.png";
    EXPECT_TRUE(std::filesystem::exists(sidecar));
    std::error_code ec;
    std::filesystem::remove(sidecar, ec);
}

// ─── driver-level tests (no GPU raster fallback needed) ────────────────────

TEST(VectorDriver, CollectionsEmitNativeGeometry) {
    // PolyCollection emits a polygon for each poly.
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    auto pc = std::make_unique<PolyCollection>(
        std::vector<std::vector<Point2D>>{
            {{0.1f, 0.1f}, {0.9f, 0.1f}, {0.5f, 0.9f}}});
    auto* pcPtr = pc.get();
    ax->addPlot(std::move(pc));
    // addPlot consumes the prop cycle — set colors after adding.
    pcPtr->faceColors = {Color::fromRgba8(0, 255, 0, 255)};
    pcPtr->edgeColors = {};
    TmpFile f("volcano_vec_poly.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("fill=\"#00ff00\""), std::string::npos);
}

TEST(VectorDriver, BarPlotEmitsRects) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    BarData bd;
    bd.heights = {1.0f, 2.0f, 3.0f};
    ax->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    TmpFile f("volcano_vec_bar.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // Three bars → at least 3 filled paths.
    size_t n = 0, pos = 0;
    while ((pos = doc.find("<path", pos)) != std::string::npos) {
        ++n; ++pos;
    }
    EXPECT_GE(n, 3u);
}

TEST(VectorDriver, StepPlotEmitsStaircase) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(std::make_unique<StepPlot>(
        std::vector<float>{0.0f, 0.5f, 1.0f},
        std::vector<float>{0.0f, 1.0f, 0.5f}, StepWhere::Post,
        Color::fromRgba8(255, 0, 0, 255), 2.0f));
    TmpFile f("volcano_vec_step.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("stroke=\"#ff0000\""), std::string::npos);
}

TEST(VectorDriver, StemPlotEmitsStemsAndMarkers) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(std::make_unique<StemPlot>(
        std::vector<float>{0.2f, 0.5f, 0.8f},
        std::vector<float>{0.5f, 1.0f, 0.6f}));
    TmpFile f("volcano_vec_stem.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // 3 stems + baseline + 3 marker paths.
    size_t n = 0, pos = 0;
    while ((pos = doc.find("<path", pos)) != std::string::npos) {
        ++n; ++pos;
    }
    EXPECT_GE(n, 7u);
}

TEST(VectorDriver, FillBetweenEmitsPolygon) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(std::make_unique<FillBetweenPlot>(
        std::vector<float>{0.0f, 0.5f, 1.0f},
        std::vector<float>{0.8f, 1.0f, 0.9f},
        std::vector<float>{0.1f, 0.2f, 0.15f},
        Color::fromRgba8(0, 128, 0, 128)));
    TmpFile f("volcano_vec_fill.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("fill=\"#008000\""), std::string::npos);
}

TEST(VectorDriver, HistPlotEmitsBars) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(std::make_unique<HistPlot>(
        std::vector<float>{0.1f, 0.2f, 0.3f, 0.7f, 0.8f, 0.9f}));
    TmpFile f("volcano_vec_hist.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find("<path"), std::string::npos);
    EXPECT_EQ(doc.find("<image"), std::string::npos);
}

TEST(VectorDriver, ErrorbarEmitsBarsAndMarkers) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ErrorbarConfig ec;
    ec.yerr = {0.1f, 0.1f};
    ax->addPlot(std::make_unique<ErrorbarPlot>(
        std::vector<float>{0.3f, 0.7f}, std::vector<float>{0.5f, 0.6f}, ec));
    TmpFile f("volcano_vec_err.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("<path"), std::string::npos);
}

TEST(VectorDriver, PieEmitsWedges) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    PieData pd;
    pd.values = {1.0f, 1.0f, 2.0f};
    pd.colors = {Color::fromRgba8(255, 0, 0, 255),
                 Color::fromRgba8(0, 255, 0, 255),
                 Color::fromRgba8(0, 0, 255, 255)};
    ax->addPlot(std::make_unique<PiePlot>(std::move(pd)));
    TmpFile f("volcano_vec_pie.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find("fill=\"#ff0000\""), std::string::npos);
    EXPECT_NE(doc.find("fill=\"#00ff00\""), std::string::npos);
    EXPECT_NE(doc.find("fill=\"#0000ff\""), std::string::npos);
}

TEST(VectorDriver, ContourEmitsSegments) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    Grid2D grid;
    grid.width = grid.height = 8;
    grid.xRange = {0.0f, 1.0f};
    grid.yRange = {0.0f, 1.0f};
    for (uint32_t j = 0; j < grid.height; ++j)
        for (uint32_t i = 0; i < grid.width; ++i) {
            float x = float(i) / (grid.width - 1);
            float y = float(j) / (grid.height - 1);
            grid.values.push_back(x * x + y * y);  // concentric contours
        }
    ContourConfig cc;
    cc.levels = {0.5f, 1.0f};
    ax->addPlot(std::make_unique<ContourPlot>(std::move(grid), cc));
    TmpFile f("volcano_vec_contour.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("<path"), std::string::npos);
}
