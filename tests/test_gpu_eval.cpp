// tests/test_gpu_eval.cpp — GPU function evaluation (EvalRenderer +
// FunctionPlot) and GPU KDE (KdeEvalRenderer + KDEPlot).
#include <gtest/gtest.h>
#include <volcano/backend/HeadlessBackend.hpp>
#include <volcano/render/primitives/EvalRenderer.hpp>
#include <volcano/render/primitives/KdeEvalRenderer.hpp>
#include <volcano/plot/plots/FunctionPlot.hpp>
#include <volcano/plot/plots/KDEPlot.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Plot.hpp>
#include "PlotTestHarness.hpp"

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct GpuFixture {
    backend::BackendDesc desc;
    std::unique_ptr<backend::IBackend> backend;
    GpuFixture() {
        desc.width = 64; desc.height = 64;
        desc.samples = vk::SampleCountFlagBits::e1;
        backend = backend::createHeadlessBackend(desc);
    }
    core::Buffer hostStorage(uint32_t count) {
        core::BufferDesc d{};
        d.size = vk::DeviceSize(count) * sizeof(float) * 2;
        d.usage = core::BufferUsage::Storage;
        d.hostVisible = true;
        d.hostCached = true;
        return core::Buffer(backend->context().allocator.handle(), d);
    }
};

/// Read back `count` vec2 samples after a compute write.
std::vector<Point2D> readback(core::Buffer& buf, uint32_t count) {
    buf.invalidate();
    auto* p = static_cast<const float*>(buf.mappedData());
    std::vector<Point2D> out(count);
    for (uint32_t i = 0; i < count; ++i) out[i] = {p[2*i], p[2*i+1]};
    return out;
}

struct TFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;
    explicit TFig(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
    }
    Image render() { return harness.render(figure); }
};

size_t darkPixels(const Image& img) {
    return img.countIf([](uint32_t, uint32_t, Pixel p) {
        return int(p.r) + int(p.g) + int(p.b) < 300;
    });
}

} // namespace

// ─── EvalRenderer (direct compute verification) ───────────────────────────

TEST(EvalRenderer, BareExpressionEvaluates) {
    GpuFixture fx;
    auto& ctx = fx.backend->context();
    render::primitives::EvalRenderer eval;
    eval.init(ctx.device.handle(), ctx.allocator.handle(),
              ctx.device.computeQueue(), ctx.computePool.handle());
    if (!eval.ready()) GTEST_SKIP() << "no compute queue";
    ASSERT_TRUE(eval.compile("sin(x)"));
    auto buf = fx.hostStorage(64);
    eval.eval(buf.handle(), 0.0, 1.0, 64);
    auto pts = readback(buf, 64);
    for (uint32_t i = 0; i < 64; ++i) {
        float x = i / 63.0f;
        EXPECT_NEAR(pts[i].x, x, 1e-4f);
        EXPECT_NEAR(pts[i].y, std::sin(x), 1e-4f) << "i=" << i;
    }
}

TEST(EvalRenderer, StatementBodyEvaluates) {
    GpuFixture fx;
    auto& ctx = fx.backend->context();
    render::primitives::EvalRenderer eval;
    eval.init(ctx.device.handle(), ctx.allocator.handle(),
              ctx.device.computeQueue(), ctx.computePool.handle());
    if (!eval.ready()) GTEST_SKIP() << "no compute queue";
    ASSERT_TRUE(eval.compile("y = x * x;"));
    auto buf = fx.hostStorage(32);
    eval.eval(buf.handle(), -1.0, 1.0, 32);
    auto pts = readback(buf, 32);
    for (uint32_t i = 0; i < 32; ++i)
        EXPECT_NEAR(pts[i].y, pts[i].x * pts[i].x, 1e-4f) << "i=" << i;
}

TEST(EvalRenderer, InvalidBodyFailsCompile) {
    GpuFixture fx;
    auto& ctx = fx.backend->context();
    render::primitives::EvalRenderer eval;
    eval.init(ctx.device.handle(), ctx.allocator.handle(),
              ctx.device.computeQueue(), ctx.computePool.handle());
    if (!eval.ready()) GTEST_SKIP() << "no compute queue";
    EXPECT_FALSE(eval.compile("y = }}"));
    EXPECT_FALSE(eval.compiled());
}

// ─── FunctionPlot (rendered via GPU eval or CPU fallback) ─────────────────

TEST(FunctionPlot, GpuEvalRendersCurve) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, -1.2f, 1.2f, 0, 1});
    cf.axes->addPlot( std::make_unique<FunctionPlot>(
        "sin(x * 6.2831853)", Range{0, 1}, 256, Color::black(), 1.5f));
    auto img = cf.render();
    // sin(x·2π) over [0,1] oscillates once — dark pixels across width.
    EXPECT_GT(darkPixels(img), 60u);
}

TEST(FunctionPlot, CpuFallbackStillRenders) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, -1.2f, 1.2f, 0, 1});
    // Invalid GLSL → compile fails → CPU fallback (sin) still draws.
    cf.axes->addPlot( std::make_unique<FunctionPlot>(
        "y = }}", Range{0, 1}, 128, Color::black(), 1.5f));
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 40u);
}

TEST(FunctionPlot, ManualXlimTriggersReevaluation) {
    TFig cf(256);
    auto* plot = cf.axes->addPlot( std::make_unique<FunctionPlot>(
        "sin(x * 6.2831853)", Range{0, 1}, 256, Color::black(), 1.5f));
    (void)plot;
    cf.axes->setViewport({0, 1, -1.2f, 1.2f, 0, 1});
    cf.axes->setXlim(0.0f, 2.0f);   // manual range → resample over [0,2]
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 60u);
}

// ─── KDEPlot ──────────────────────────────────────────────────────────────

TEST(KdePlot, DensityHeatmapRenders) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    std::vector<Point2D> samples;
    // Cluster samples around (0.5, 0.5) → peak density at center.
    for (int i = 0; i < 200; ++i) {
        float a = i * 0.1f;
        samples.push_back({0.5f + 0.15f * std::cos(a) * (i % 7) / 7.0f,
                           0.5f + 0.15f * std::sin(a) * (i % 5) / 5.0f});
    }
    cf.axes->addPlot(
        std::make_unique<KDEPlot>(samples, 32, 32, 0.1f));
    auto img = cf.render();
    // Center should differ from edges (density peak vs background).
    auto center = img.averageRegion(96, 96, 160, 160);
    auto corner = img.averageRegion(0, 0, 40, 40);
    EXPECT_NE(center.r, corner.r);
}

TEST(KdePlot, EmptySamplesNoCrash) {
    TFig cf(128);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    cf.axes->addPlot(
        std::make_unique<KDEPlot>(std::vector<Point2D>{}, 16, 16, 0.1f));
    EXPECT_NO_THROW(cf.render());
}
