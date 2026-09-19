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

#include <cstdlib>
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

// ─── deterministic geometry ─────────────────────────────────────────────────

namespace {

/// Extract the numbers from the `d` attribute of the first <path>
/// element containing `needle`.
std::vector<float> pathNums(const std::string& doc,
                            const std::string& needle) {
    auto pos = doc.find(needle);
    if (pos == std::string::npos) return {};
    auto el = doc.rfind("<path", pos);
    auto ds = doc.find("d=\"", el);
    auto de = doc.find('\"', ds + 3);
    std::vector<float> out;
    const char* p = doc.c_str() + ds + 3;
    const char* end = doc.c_str() + de;
    while (p < end) {
        while (p < end && !std::isdigit(*p) && *p != '-' && *p != '.')
            ++p;
        if (p >= end) break;
        char* next = nullptr;
        out.push_back(std::strtof(p, &next));
        p = next;
    }
    return out;
}

/// All <path> number-lists whose element contains `needle`.
std::vector<std::vector<float>> allPathNums(const std::string& doc,
                                            const std::string& needle) {
    std::vector<std::vector<float>> out;
    size_t pos = 0;
    while ((pos = doc.find("<path", pos)) != std::string::npos) {
        auto end = doc.find("/>", pos);
        if (end == std::string::npos) break;
        auto el = doc.substr(pos, end - pos);
        if (el.find(needle) != std::string::npos)
            out.push_back(pathNums(doc.substr(pos), "d=\""));
        pos = end + 2;
    }
    return out;
}

} // namespace

TEST(VectorSvgDeterministic, LinePathMatchesDataTransform) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec_golden_line.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto nums = pathNums(readText(f.path), "stroke=\"#ff0000\"");
    // Three points → M x y L x y L x y = 6 numbers.
    ASSERT_EQ(nums.size(), 6u);
    const std::vector<Point2D> pts = {{0, 0}, {0.5f, 1.0f}, {1, 0.2f}};
    for (size_t i = 0; i < pts.size(); ++i) {
        auto fr = ax->dataToFraction(pts[i]);
        float ex = ax->rect.x + fr.x * float(ax->rect.width);
        float ey = ax->rect.y + (1.0f - fr.y) * float(ax->rect.height);
        EXPECT_NEAR(nums[i * 2], ex, 0.5f) << "point " << i << " x";
        EXPECT_NEAR(nums[i * 2 + 1], ey, 0.5f) << "point " << i << " y";
    }
}

TEST(VectorSvgDeterministic, GeometryStableAcrossRuns) {
    auto emit = [] {
        PlotTestHarness harness(256, 256);
        Figure fig;
        auto* ax = fig.addAxes();
        ax->addPlot(makeLine());
        ax->addPlot(makeScatter());
        TmpFile f("volcano_vec_golden_rep.svg");
        EXPECT_TRUE(harness.renderer().savefig(fig, f.path));
        return readText(f.path);
    };
    EXPECT_EQ(emit(), emit());  // byte-identical output
}

TEST(VectorSvgDeterministic, SizeBarEmitsNativeGeometry) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    ax->setViewport({0, 10, 0, 10, 0, 1});
    SizeBar sb;
    sb.size = 2.0f;
    sb.label = "2 units";
    sb.loc = "lower right";
    ax->addSizeBar(sb);
    TmpFile f("volcano_vec_sizebar.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // Label emitted as text.
    EXPECT_NE(doc.find(">2 units</text>"), std::string::npos);
    // Bar: a filled black quad whose width matches the data transform.
    float f0 = ax->dataToFraction({0, 0}).x;
    float f1 = ax->dataToFraction({2, 0}).x;
    float barW = std::fabs(f1 - f0) * float(ax->rect.width);
    bool found = false;
    for (auto& nums : allPathNums(doc, "fill=\"#000000\"")) {
        if (nums.size() < 8) continue;
        float w = std::fabs(nums[2] - nums[0]);
        float h = std::fabs(nums[5] - nums[1]);
        if (std::fabs(w - barW) < 1.0f && h < 4.0f) found = true;
    }
    EXPECT_TRUE(found) << "no filled bar quad of width " << barW;
}

// ─── vector/raster parity ───────────────────────────────────────────────────

TEST(VectorSvgParity, SecondaryYAxisEmitsRightLabels) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setViewport({0, 1, 0, 1});
    ax->secondaryYaxis([](float y) { return y * 2.0f; },
                      [](float y) { return y * 0.5f; }, "dbl");
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec_sec.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // Secondary labels sit right of the axes rect.
    float edge = ax->rect.x + float(ax->rect.width);
    bool found = false;
    size_t pos = 0;
    while ((pos = doc.find("<text x=\"", pos)) != std::string::npos) {
        float tx = std::strtof(doc.c_str() + pos + 9, nullptr);
        if (tx > edge + 2.0f) { found = true; break; }
        ++pos;
    }
    EXPECT_TRUE(found) << "no tick label right of the axes";
}

TEST(VectorSvgParity, SecondaryXAxisEmitsTopLabels) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setViewport({0, 1, 0, 1});
    ax->secondaryXaxis([](float x) { return x * 100.0f; },
                      [](float x) { return x * 0.01f; }, "pct");
    ax->addPlot(makeLine());
    TmpFile f("volcano_vec_secx.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    // Secondary x labels sit above the axes rect.
    bool found = false;
    size_t pos = 0;
    while ((pos = doc.find("<text x=\"", pos)) != std::string::npos) {
        auto ys = doc.find("y=\"", pos);
        float ty = std::strtof(doc.c_str() + ys + 3, nullptr);
        if (ty < float(ax->rect.y) - 2.0f) { found = true; break; }
        ++pos;
    }
    EXPECT_TRUE(found) << "no tick label above the axes";
}

TEST(VectorSvgParity, RotatedTickLabelsEmitTransform) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    ax->style().xAxis.tickFont.rotation = 0.6f;
    TmpFile f("volcano_vec_rot.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    EXPECT_NE(readText(f.path).find("rotate("), std::string::npos);
}

TEST(VectorSvgParity, TableEmitsNativeCells) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    ax->table({{"a", "b"}, {"1", "2"}});
    TmpFile f("volcano_vec_table.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find(">a</text>"), std::string::npos);
    EXPECT_NE(doc.find(">2</text>"), std::string::npos);
    EXPECT_EQ(doc.find("<image"), std::string::npos)
        << "table should emit native vector cells";
}

TEST(VectorSvgParity, AnchoredTextEmitsFrameAndText) {
    PlotTestHarness harness(256, 256);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->addPlot(makeLine());
    ax->addAnchoredText("note", "lower right");
    TmpFile f("volcano_vec_anchored.svg");
    ASSERT_TRUE(harness.renderer().savefig(fig, f.path));
    auto doc = readText(f.path);
    EXPECT_NE(doc.find(">note</text>"), std::string::npos);
    // Frame: a filled rect + stroked border around the text.
    EXPECT_NE(doc.find("fill-opacity=\"0.8"), std::string::npos);
}
