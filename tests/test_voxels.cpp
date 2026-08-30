// tests/test_voxels.cpp — tests for VoxelsPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/VoxelsPlot.hpp>
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
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(VoxelsRegression, SingleVoxelRenders) {
    Fig3D cf(256);
    // 1x1x1 grid with one filled voxel.
    std::vector<uint8_t> filled = {1};
    VoxelsConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 1, 1, 1, cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Single voxel should render";
}

TEST(VoxelsRegression, EmptyVoxelsRendersNothing) {
    Fig3D cf(256);
    // 3x3x3 grid with no filled voxels.
    std::vector<uint8_t> filled(27, 0);
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 3, 3, 3);
    plot->setCamera(Camera3D{{5, 5, 5}, {1.5f, 1.5f, 1.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty voxels should render nothing";
}

TEST(VoxelsRegression, CubeOfVoxels) {
    Fig3D cf(256);
    // 2x2x2 cube of filled voxels.
    std::vector<uint8_t> filled(8, 1);
    VoxelsConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 2, 2, 2, cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {1, 1, 1}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "2x2x2 cube should render";
}

TEST(VoxelsRegression, CustomColor) {
    Fig3D cf(256);
    std::vector<uint8_t> filled = {1};
    VoxelsConfig cfg;
    cfg.color = Color::red();
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 1, 1, 1, cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
        }
    EXPECT_GT(redCount, 30u) << "Red voxel should render red pixels";
}

TEST(VoxelsRegression, PerVoxelColors) {
    Fig3D cf(256);
    // 2x1x1 with different colors.
    std::vector<uint8_t> filled = {1, 1};
    VoxelsConfig cfg;
    cfg.colors = {Color::red(), Color::blue()};
    cfg.drawEdges = false;
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 2, 1, 1, cfg);
    plot->setCamera(Camera3D{{5, 3, 3}, {1, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0, blueCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
            if (p.b > 100 && p.b > p.r + 20 && p.b > p.g + 20) ++blueCount;
        }
    EXPECT_GT(redCount, 5u) << "Red voxel should render";
    EXPECT_GT(blueCount, 5u) << "Blue voxel should render";
}

TEST(VoxelsRegression, WithEdges) {
    Fig3D cf(256);
    std::vector<uint8_t> filled = {1};
    VoxelsConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    cfg.drawEdges = true;
    cfg.edgeColor = Color::black();
    cfg.edgeWidth = 1.0f;
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 1, 1, 1, cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Voxel with edges should render";
}

TEST(VoxelsRegression, WithoutEdges) {
    Fig3D cf(256);
    std::vector<uint8_t> filled = {1};
    VoxelsConfig cfg;
    cfg.color = Color::blue();
    cfg.drawEdges = false;
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 1, 1, 1, cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Voxel without edges should render";
}

TEST(VoxelsRegression, DifferentCameraAngles) {
    std::vector<uint8_t> filled(8, 1);

    Fig3D cf1(256);
    VoxelsConfig cfg1;
    cfg1.color = Color::blue();
    auto p1 = std::make_unique<VoxelsPlot>(filled, 2, 2, 2, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {1, 1, 1}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    VoxelsConfig cfg2;
    cfg2.color = Color::blue();
    auto p2 = std::make_unique<VoxelsPlot>(filled, 2, 2, 2, cfg2);
    p2->setCamera(Camera3D{{-5, 5, 5}, {1, 1, 1}, {0, 0, 1}});
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

TEST(VoxelsRegression, Autoscale) {
    Fig3D cf(256);
    // 3x2x1 grid with some filled voxels.
    std::vector<uint8_t> filled(6, 0);
    filled[0] = 1;  // (0,0,0)
    filled[5] = 1;  // (2,1,0)
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 3, 2, 1);
    plot->setCamera(Camera3D{{5, 5, 5}, {1.5f, 1, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, 0.0f);
    EXPECT_GE(av.x.max, 3.0f);
    EXPECT_LE(av.y.min, 0.0f);
    EXPECT_GE(av.y.max, 2.0f);
}

TEST(VoxelsRegression, SparseVoxels) {
    Fig3D cf(256);
    // 4x4x4 grid with a few scattered voxels.
    std::vector<uint8_t> filled(64, 0);
    filled[0] = 1;       // (0,0,0)
    filled[21] = 1;      // (3,1,1)
    filled[42] = 1;      // (2,2,2)
    filled[63] = 1;      // (3,3,3)
    VoxelsConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 4, 4, 4, cfg);
    plot->setCamera(Camera3D{{7, 7, 7}, {2, 2, 2}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "Sparse voxels should render";
}

TEST(VoxelsRegression, HollowShell) {
    Fig3D cf(256);
    // 3x3x3 hollow shell: filled on the boundary, empty inside.
    std::vector<uint8_t> filled(27, 0);
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 3; ++z) {
                if (x == 0 || x == 2 || y == 0 || y == 2 || z == 0 || z == 2) {
                    filled[x * 9 + y * 3 + z] = 1;
                }
            }
    VoxelsConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    auto plot = std::make_unique<VoxelsPlot>(std::move(filled), 3, 3, 3, cfg);
    plot->setCamera(Camera3D{{6, 6, 6}, {1.5f, 1.5f, 1.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Hollow shell should render";
}
