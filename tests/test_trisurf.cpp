// tests/test_trisurf.cpp — tests for TrisurfPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/TrisurfPlot.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct Fig3D {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit Fig3D(uint32_t size = 256)
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

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

// Generate scattered points on a sine surface.
struct SurfPts { std::vector<float> x, y, z; };
SurfPts makeSineSurface(int n, float spread = 3.0f) {
    SurfPts s;
    for (int i = 0; i < n; ++i) {
        float angle = static_cast<float>(i) / n * 2.0f * static_cast<float>(M_PI);
        float r = spread * static_cast<float>(i) / n;
        float x = r * std::cos(angle);
        float y = r * std::sin(angle);
        float z = std::sin(std::sqrt(x*x + y*y));
        s.x.push_back(x);
        s.y.push_back(y);
        s.z.push_back(z);
    }
    return s;
}

// Generate a grid of points on a dome.
SurfPts makeDomeSurface(int nx, int ny) {
    SurfPts s;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            float x = -1.5f + 3.0f * i / (nx - 1);
            float y = -1.5f + 3.0f * j / (ny - 1);
            float r2 = x*x + y*y;
            float z = r2 < 2.0f ? std::sqrt(2.0f - r2) : 0.0f;
            s.x.push_back(x);
            s.y.push_back(y);
            s.z.push_back(z);
        }
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(TrisurfRegression, BasicTrisurfRenders) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<TrisurfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Trisurf should render filled triangles";
}

TEST(TrisurfRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    auto plot = std::make_unique<TrisurfPlot>(std::move(x), std::move(y), std::move(z));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty trisurf should render nothing";
}

TEST(TrisurfRegression, ThreePointsOneTriangle) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1, 0}, y = {0, 0, 1}, z = {0, 1, 0.5f};
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<TrisurfPlot>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Three-point trisurf should render one triangle";
}

TEST(TrisurfRegression, DomeSurfaceRenders) {
    Fig3D cf(256);
    auto s = makeDomeSurface(8, 8);
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<TrisurfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0, 0, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Dome trisurf should render";
}

TEST(TrisurfRegression, CustomColormap) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::plasma();
    auto plot = std::make_unique<TrisurfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Custom colormap trisurf should render";
}

TEST(TrisurfRegression, DifferentColormapsProduceDifferentColors) {
    auto s = makeSineSurface(30);

    Fig3D cf1(256);
    TrisurfConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    auto plot1 = std::make_unique<TrisurfPlot>(s.x, s.y, s.z, cfg1);
    plot1->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(plot1));
    auto imgViridis = cf1.render();

    Fig3D cf2(256);
    TrisurfConfig cfg2;
    cfg2.cmap = &colormaps::plasma();
    auto plot2 = std::make_unique<TrisurfPlot>(s.x, s.y, s.z, cfg2);
    plot2->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(plot2));
    auto imgPlasma = cf2.render();

    Pixel pix1 = imgViridis.get(128, 128);
    Pixel pix2 = imgPlasma.get(128, 128);
    EXPECT_FALSE(pix1.approx(pix2, 30))
        << "Different colormaps should produce different colors";
}

TEST(TrisurfRegression, WithEdges) {
    Fig3D cf(256);
    auto s = makeSineSurface(20);
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.drawEdges = true;
    cfg.edgeColor = Color::black();
    cfg.edgeWidth = 1.0f;
    auto plot = std::make_unique<TrisurfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Trisurf with edges should render";
}

TEST(TrisurfRegression, ExplicitValueRange) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {-1, 1};
    auto plot = std::make_unique<TrisurfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Explicit value range trisurf should render";
}

TEST(TrisurfRegression, ExplicitTriangles) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1, 0, 1};
    std::vector<float> y = {0, 0, 1, 1};
    std::vector<float> z = {0, 1, 0.5f, 1.5f};
    std::vector<Triangle> tris = {{0, 1, 2}, {1, 3, 2}};
    TrisurfConfig cfg;
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<TrisurfPlot>(std::move(x), std::move(y), std::move(z),
                                               std::move(tris), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "Explicit triangles trisurf should render";
}

TEST(TrisurfRegression, Autoscale) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    auto plot = std::make_unique<TrisurfPlot>(std::move(s.x), std::move(s.y), std::move(s.z));
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    // z range: sin(r) is in [-1, 1]
    EXPECT_LE(av.z.min, 0.0f);
    EXPECT_GE(av.z.max, 0.0f);
}

TEST(TrisurfRegression, DifferentCameraAngles) {
    auto s = makeDomeSurface(8, 8);

    Fig3D cf1(256);
    TrisurfConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    auto p1 = std::make_unique<TrisurfPlot>(s.x, s.y, s.z, cfg1);
    p1->setCamera(Camera3D{{4, 4, 4}, {0, 0, 0.5f}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    TrisurfConfig cfg2;
    cfg2.cmap = &colormaps::viridis();
    auto p2 = std::make_unique<TrisurfPlot>(s.x, s.y, s.z, cfg2);
    p2->setCamera(Camera3D{{-4, 4, 4}, {0, 0, 0.5f}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    bool anyDiff = false;
    for (uint32_t y = 64; y < 192; ++y)
        for (uint32_t x = 64; x < 192; ++x)
            if (!img1.get(x, y).approx(img2.get(x, y), 30)) {
                anyDiff = true;
                break;
            }
    EXPECT_TRUE(anyDiff) << "Different camera angles should produce different images";
}
