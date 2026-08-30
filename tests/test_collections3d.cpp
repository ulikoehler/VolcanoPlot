// tests/test_collections3d.cpp — tests for Line3DCollection and Poly3DCollection
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Collections3D.hpp>
#include <volcano/plot/Transform.hpp>

#include <gtest/gtest.h>

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

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Line3DCollection
// ═══════════════════════════════════════════════════════════════════════════

TEST(Line3DCollectionRegression, BasicSegmentsRender) {
    Fig3D cf(256);
    // Two line segments in 3D.
    std::vector<std::pair<Point3D, Point3D>> segs = {
        {{0, 0, 0}, {1, 0, 0}},
        {{0, 0, 0}, {0, 1, 0}},
        {{0, 0, 0}, {0, 0, 1}},
    };
    Line3DCollectionConfig cfg;
    cfg.color = Color::blue();
    cfg.lineWidth = 2.0f;
    auto plot = std::make_unique<Line3DCollection>(std::move(segs), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D line segments should render";
}

TEST(Line3DCollectionRegression, EmptySegmentsRenderNothing) {
    Fig3D cf(256);
    std::vector<std::pair<Point3D, Point3D>> segs;
    auto plot = std::make_unique<Line3DCollection>(std::move(segs));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty line collection should render nothing";
}

TEST(Line3DCollectionRegression, FlatArrayConstructor) {
    Fig3D cf(256);
    // Two segments via flat array: [x0,y0,z0, x1,y1,z1, ...]
    std::vector<float> segs = {
        0, 0, 0,  1, 0, 0,
        0, 0, 0,  0, 1, 0,
    };
    Line3DCollectionConfig cfg;
    cfg.color = Color::red();
    auto plot = std::make_unique<Line3DCollection>(std::move(segs), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Flat array constructor should work";
}

TEST(Line3DCollectionRegression, CustomColor) {
    Fig3D cf(256);
    std::vector<std::pair<Point3D, Point3D>> segs = {
        {{0, 0, 0}, {1, 1, 0}},
    };
    Line3DCollectionConfig cfg;
    cfg.color = Color::red();
    cfg.lineWidth = 3.0f;
    auto plot = std::make_unique<Line3DCollection>(std::move(segs), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red line collection should render red pixels";
}

TEST(Line3DCollectionRegression, DifferentCameraAngles) {
    std::vector<std::pair<Point3D, Point3D>> segs = {
        {{0, 0, 0}, {1, 0, 0}},
        {{0, 0, 0}, {0, 1, 0}},
        {{0, 0, 0}, {0, 0, 1}},
    };

    Fig3D cf1(256);
    Line3DCollectionConfig cfg1;
    cfg1.color = Color::blue();
    auto p1 = std::make_unique<Line3DCollection>(segs, cfg1);
    p1->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Line3DCollectionConfig cfg2;
    cfg2.color = Color::blue();
    auto p2 = std::make_unique<Line3DCollection>(segs, cfg2);
    p2->setCamera(Camera3D{{-4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
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

TEST(Line3DCollectionRegression, Autoscale) {
    Fig3D cf(256);
    std::vector<std::pair<Point3D, Point3D>> segs = {
        {{-1, 0, 0}, {2, 1, 0}},
        {{0, -1, 1}, {1, 2, 2}},
    };
    auto plot = std::make_unique<Line3DCollection>(std::move(segs));
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -1.0f);
    EXPECT_GE(av.x.max, 2.0f);
    EXPECT_LE(av.y.min, -1.0f);
    EXPECT_GE(av.y.max, 2.0f);
    EXPECT_LE(av.z.min, 0.0f);
    EXPECT_GE(av.z.max, 2.0f);
}

TEST(Line3DCollectionRegression, ManySegments) {
    Fig3D cf(256);
    // A grid of line segments.
    std::vector<std::pair<Point3D, Point3D>> segs;
    for (int i = 0; i <= 3; ++i) {
        segs.push_back({{float(i), 0, 0}, {float(i), 3, 0}});
        segs.push_back({{0, float(i), 0}, {3, float(i), 0}});
    }
    Line3DCollectionConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Line3DCollection>(std::move(segs), cfg);
    plot->setCamera(Camera3D{{6, 6, 6}, {1.5f, 1.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Many segments should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Poly3DCollection
// ═══════════════════════════════════════════════════════════════════════════

TEST(Poly3DCollectionRegression, SingleTriangleRenders) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColor = Color::blue();
    cfg.drawEdges = false;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "Single 3D triangle should render";
}

TEST(Poly3DCollectionRegression, EmptyPolysRenderNothing) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty poly collection should render nothing";
}

TEST(Poly3DCollectionRegression, QuadPolygon) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColor = Color::fromRgba8(31, 119, 180, 255);
    cfg.drawEdges = false;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Quad polygon should render";
}

TEST(Poly3DCollectionRegression, CustomColor) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColor = Color::red();
    cfg.drawEdges = false;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
        }
    EXPECT_GT(redCount, 20u) << "Red polygon should render red pixels";
}

TEST(Poly3DCollectionRegression, PerPolygonColors) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},   // red
        {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}},    // blue
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColors = {Color::red(), Color::blue()};
    cfg.drawEdges = false;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0, blueCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
            if (p.b > 100 && p.b > p.r + 20 && p.b > p.g + 20) ++blueCount;
        }
    EXPECT_GT(redCount, 5u) << "Red polygon should render";
    EXPECT_GT(blueCount, 5u) << "Blue polygon should render";
}

TEST(Poly3DCollectionRegression, WithEdges) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColor = Color::fromRgba8(31, 119, 180, 200);
    cfg.drawEdges = true;
    cfg.edgeColor = Color::black();
    cfg.edgeWidth = 1.0f;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Polygon with edges should render";
}

TEST(Poly3DCollectionRegression, FacesOnly) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColor = Color::blue();
    cfg.drawFaces = true;
    cfg.drawEdges = false;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Faces-only polygon should render";
}

TEST(Poly3DCollectionRegression, EdgesOnly) {
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
    };

    Fig3D cf(256);
    Poly3DCollectionConfig cfg;
    cfg.drawFaces = false;
    cfg.drawEdges = true;
    cfg.edgeColor = Color::blue();
    cfg.edgeWidth = 2.0f;
    auto plot = std::make_unique<Poly3DCollection>(polys, cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Edges-only polygon should render";

    // Edges only should render fewer pixels than faces.
    Fig3D cf2(256);
    Poly3DCollectionConfig cfg2;
    cfg2.faceColor = Color::blue();
    cfg2.drawFaces = true;
    cfg2.drawEdges = false;
    auto p2 = std::make_unique<Poly3DCollection>(polys, cfg2);
    p2->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();
    size_t faceCount = countPixels(img2, isNotWhite);
    EXPECT_LT(filledCount, faceCount) << "Edges only should render fewer pixels than faces";
}

TEST(Poly3DCollectionRegression, DifferentCameraAngles) {
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    };

    Fig3D cf1(256);
    Poly3DCollectionConfig cfg1;
    cfg1.faceColor = Color::blue();
    cfg1.drawEdges = false;
    auto p1 = std::make_unique<Poly3DCollection>(polys, cfg1);
    p1->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Poly3DCollectionConfig cfg2;
    cfg2.faceColor = Color::blue();
    cfg2.drawEdges = false;
    auto p2 = std::make_unique<Poly3DCollection>(polys, cfg2);
    p2->setCamera(Camera3D{{-4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
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

TEST(Poly3DCollectionRegression, Autoscale) {
    Fig3D cf(256);
    std::vector<std::vector<Point3D>> polys = {
        {{-1, 0, 0}, {2, 0, 0}, {2, 3, 0}, {-1, 3, 0}},
        {{0, 0, -1}, {1, 0, -1}, {1, 1, 2}, {0, 1, 2}},
    };
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys));
    plot->setCamera(Camera3D{{6, 6, 6}, {0.5f, 1.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -1.0f);
    EXPECT_GE(av.x.max, 2.0f);
    EXPECT_LE(av.y.min, 0.0f);
    EXPECT_GE(av.y.max, 3.0f);
    EXPECT_LE(av.z.min, -1.0f);
    EXPECT_GE(av.z.max, 2.0f);
}

TEST(Poly3DCollectionRegression, MultipleTriangles) {
    Fig3D cf(256);
    // Two triangles forming a square.
    std::vector<std::vector<Point3D>> polys = {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}},
        {{0, 0, 0}, {1, 1, 0}, {0, 1, 0}},
    };
    Poly3DCollectionConfig cfg;
    cfg.faceColor = Color::fromRgba8(31, 119, 180, 255);
    cfg.drawEdges = false;
    auto plot = std::make_unique<Poly3DCollection>(std::move(polys), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Multiple triangles should render";
}
