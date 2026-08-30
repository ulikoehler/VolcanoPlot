// tests/test_gpu_autoscale.cpp — tests for GPU-side parallel min/max autoscale
//
// Verifies that the GPU compute reduce produces the same viewport the CPU
// autoscale would, by rendering scatter/line plots with NO manual viewport
// (so autoscale runs) and checking that points land at the expected pixel
// positions for the data-derived viewport.
//
// Strategy:
//   * Place points with a known data-space bounding box.
//   * Do NOT call setViewport() — let autoscaleGpu() compute it.
//   * The expected viewport = data bbox + 5% padding (matching Axes::finalizeAutoscale).
//   * Convert data points to pixels using that expected viewport + the actual
//     axes rect (set by Figure::layout during renderFrame) and assert the
//     rendered pixels are at those positions.
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

/// Minimal self-contained crafted-figure helper for GPU autoscale tests.
struct AutoscaleFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit AutoscaleFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

/// Expected viewport after Axes::finalizeAutoscale: 5% padding around bbox.
Viewport expectedViewport(float minX, float maxX, float minY, float maxY) {
    Viewport v;
    v.x = {minX, maxX};
    v.y = {minY, maxY};
    v.z = {0, 1};
    float padx = v.x.span() * 0.05f;
    float pady = v.y.span() * 0.05f;
    v.x.min -= padx; v.x.max += padx;
    v.y.min -= pady; v.y.max += pady;
    return v;
}

/// Convert data coords to pixel coords for a given viewport + axes rect,
/// matching the renderer's Y-up math convention (data y maps to H*(1-y)).
std::pair<float, float> dataToPixel(const Viewport& v, const Rect2D& rect,
                                    float dx, float dy) {
    float nx = (dx - v.x.min) / v.x.span();
    float ny = (dy - v.y.min) / v.y.span();
    float px = rect.x + nx * rect.width;
    float py = rect.y + (1.0f - ny) * rect.height;
    return {px, py};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// GPU autoscale: scatter
// ═══════════════════════════════════════════════════════════════════════════

TEST(GpuAutoscaleRegression, ScatterViewportMatchesDataBbox) {
    // Craft: scatter with bbox x=[10,20], y=[100,200]. No manual viewport.
    // The GPU reduce should compute this bbox; the renderer then maps the
    // data point at (15, 150) (the bbox center) to the axes-rect center.
    AutoscaleFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(255, 0, 0);
    s.marker = MarkerStyle::Circle;
    s.size = 10.0f;
    s.points.push_back({10.0f, 100.0f});
    s.points.push_back({20.0f, 200.0f});
    s.points.push_back({15.0f, 150.0f});  // center
    cf.axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    auto img = cf.render();

    // The viewport should match the data bbox + 5% padding.
    auto vp = expectedViewport(10.0f, 20.0f, 100.0f, 200.0f);
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, vp.x.min, 0.01f);
    EXPECT_NEAR(av.x.max, vp.x.max, 0.01f);
    EXPECT_NEAR(av.y.min, vp.y.min, 0.01f);
    EXPECT_NEAR(av.y.max, vp.y.max, 0.01f);

    // The center point (15, 150) should render at the axes-rect center.
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 15.0f, 150.0f);
    auto c = img.centroid(Pixel::red(), 40);
    EXPECT_GT(c.count, 0u) << "No red pixels found";
    EXPECT_NEAR(c.x, cx, 8.0) << "Red centroid not at expected center x";
    EXPECT_NEAR(c.y, cy, 8.0) << "Red centroid not at expected center y";
}

TEST(GpuAutoscaleRegression, ScatterLargeDatasetReduceCorrect) {
    // Stress the multi-pass reduce with > 256 points (needs > 1 workgroup).
    // Data spans x=[0,1000], y=[0,100]; verify the autoscaled viewport maps
    // the data extremes to near the axes-rect edges.
    AutoscaleFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(0, 200, 0);
    s.marker = MarkerStyle::Circle;
    s.size = 4.0f;
    for (int i = 0; i < 1000; ++i) {
        float x = float(i);
        float y = (i % 2 == 0) ? 0.0f : 100.0f;
        s.points.push_back({x, y});
    }
    s.points.push_back({500.0f, 50.0f});  // center
    cf.axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    auto img = cf.render();

    // Viewport should match data bbox + 5% padding.
    // Data x ranges [0, 999] (i=0..999), y ranges [0, 100].
    auto vp = expectedViewport(0.0f, 999.0f, 0.0f, 100.0f);
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, vp.x.min, 1.0f);
    EXPECT_NEAR(av.x.max, vp.x.max, 1.0f);
    EXPECT_NEAR(av.y.min, vp.y.min, 1.0f);
    EXPECT_NEAR(av.y.max, vp.y.max, 1.0f);

    // Center point (500, 50) should be at the axes-rect center.
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 500.0f, 50.0f);
    auto c = img.centroid(Pixel::green(), 60);
    EXPECT_GT(c.count, 0u) << "No green pixels found";
    // The center point is small (size=4) and there are many edge points,
    // so the centroid won't be exactly at the center. Instead, check that
    // there are green pixels near the expected center.
    bool hasGreenNearCenter = false;
    for (int dy = -8; dy <= 8 && !hasGreenNearCenter; ++dy)
        for (int dx = -8; dx <= 8; ++dx)
            if (img.get(static_cast<uint32_t>(cx + dx),
                        static_cast<uint32_t>(cy + dy)).approx(Pixel::green(), 60))
                hasGreenNearCenter = true;
    EXPECT_TRUE(hasGreenNearCenter) << "No green pixel near expected center";

    // Green pixels should span most of the axes rect width (x extremes).
    auto bb = img.boundingBox(Pixel::green(), 60);
    EXPECT_TRUE(bb.found) << "No green pixels found";
    auto [edgeX0, _0] = dataToPixel(vp, cf.axes->rect, 0.0f, 50.0f);
    auto [edgeX1, _1] = dataToPixel(vp, cf.axes->rect, 999.0f, 50.0f);
    EXPECT_NEAR(float(bb.x0), edgeX0, 15.0f) << "Green bbox left edge mismatch";
    EXPECT_NEAR(float(bb.x1), edgeX1, 15.0f) << "Green bbox right edge mismatch";
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU autoscale: line
// ═══════════════════════════════════════════════════════════════════════════

TEST(GpuAutoscaleRegression, LineViewportMatchesDataBbox) {
    // A line from (0,0) to (10,10). GPU reduce should produce bbox [0,10]^2.
    // The line should span from bottom-left to top-right of the axes rect.
    AutoscaleFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(0, 0, 255);
    s.lineWidth = 2.0f;
    s.points.push_back({0.0f, 0.0f});
    s.points.push_back({10.0f, 10.0f});
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto img = cf.render();

    // Viewport should match data bbox + 5% padding.
    auto vp = expectedViewport(0.0f, 10.0f, 0.0f, 10.0f);
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, vp.x.min, 0.01f);
    EXPECT_NEAR(av.x.max, vp.x.max, 0.01f);
    EXPECT_NEAR(av.y.min, vp.y.min, 0.01f);
    EXPECT_NEAR(av.y.max, vp.y.max, 0.01f);

    // Endpoints should map to expected pixel positions (using actual rect).
    auto [x0, y0] = dataToPixel(vp, cf.axes->rect, 0.0f, 0.0f);
    auto [x1, y1] = dataToPixel(vp, cf.axes->rect, 10.0f, 10.0f);

    // Blue pixels should form a diagonal spanning the axes rect.
    auto bb = img.boundingBox(Pixel::blue(), 60);
    EXPECT_TRUE(bb.found) << "No blue pixels found";
    EXPECT_NEAR(float(bb.x0), x0, 15.0f) << "Line left edge mismatch";
    EXPECT_NEAR(float(bb.x1), x1, 15.0f) << "Line right edge mismatch";
    EXPECT_NEAR(float(bb.y0), y1, 15.0f) << "Line top edge mismatch";
    EXPECT_NEAR(float(bb.y1), y0, 15.0f) << "Line bottom edge mismatch";
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU autoscale: manual viewport is respected (no GPU reduce)
// ═══════════════════════════════════════════════════════════════════════════

TEST(GpuAutoscaleRegression, ManualViewportOverridesAutoscale) {
    // When setViewport is called, autoscaleGpu must be a no-op.
    AutoscaleFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(255, 0, 0);
    s.marker = MarkerStyle::Circle;
    s.size = 10.0f;
    s.points.push_back({50.0f, 50.0f});
    cf.axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    cf.axes->setViewport({0, 100, 0, 100});
    auto img = cf.render();

    // Viewport should remain [0,100]^2 (not overwritten by autoscale).
    const auto& av = cf.axes->viewport();
    EXPECT_FLOAT_EQ(av.x.min, 0.0f);
    EXPECT_FLOAT_EQ(av.x.max, 100.0f);
    EXPECT_FLOAT_EQ(av.y.min, 0.0f);
    EXPECT_FLOAT_EQ(av.y.max, 100.0f);

    // Point at (50,50) maps to the axes-rect center.
    auto [cx, cy] = dataToPixel({0, 100, 0, 100}, cf.axes->rect, 50.0f, 50.0f);
    auto c = img.centroid(Pixel::red(), 40);
    EXPECT_GT(c.count, 0u) << "No red pixels found";
    EXPECT_NEAR(c.x, cx, 8.0) << "Manual viewport: point not at expected center x";
    EXPECT_NEAR(c.y, cy, 8.0) << "Manual viewport: point not at expected center y";
}
