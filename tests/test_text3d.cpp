// tests/test_text3d.cpp — tests for Text3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Text3D.hpp>
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

TEST(Text3DRegression, SingleTextRenders) {
    Fig3D cf(256);
    auto plot = std::make_unique<Text3D>(0.0f, 0.0f, 0.0f, "Hello",
                                          Color::black(), 16.0f);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "3D text should render pixels";
}

TEST(Text3DRegression, EmptyTextRendersNothing) {
    Fig3D cf(256);
    std::vector<Text3DItem> items;
    auto plot = std::make_unique<Text3D>(std::move(items));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty text should render nothing";
}

TEST(Text3DRegression, MultipleTexts) {
    Fig3D cf(256);
    std::vector<Text3DItem> items;
    Text3DItem a; a.x = 0; a.y = 0; a.z = 0; a.text = "A"; a.color = Color::black(); a.fontSize = 16;
    Text3DItem b; b.x = 1; b.y = 0; b.z = 0; b.text = "B"; b.color = Color::black(); b.fontSize = 16;
    Text3DItem c; c.x = 0; c.y = 1; c.z = 0; c.text = "C"; c.color = Color::black(); c.fontSize = 16;
    items.push_back(std::move(a));
    items.push_back(std::move(b));
    items.push_back(std::move(c));
    auto plot = std::make_unique<Text3D>(std::move(items));
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Multiple 3D texts should render";
}

TEST(Text3DRegression, CustomColor) {
    Fig3D cf(256);
    auto plot = std::make_unique<Text3D>(0.0f, 0.0f, 0.0f, "Hello",
                                          Color::red(), 16.0f);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
        }
    EXPECT_GT(redCount, 3u) << "Red 3D text should render red pixels";
}

TEST(Text3DRegression, DifferentCameraAngles) {
    // Use an off-center position so different camera angles produce
    // different screen positions.
    Fig3D cf1(256);
    auto p1 = std::make_unique<Text3D>(1.0f, 0.0f, 0.0f, "Hello",
                                        Color::black(), 16.0f);
    p1->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    auto p2 = std::make_unique<Text3D>(1.0f, 0.0f, 0.0f, "Hello",
                                        Color::black(), 16.0f);
    p2->setCamera(Camera3D{{-5, 5, 5}, {0.5f, 0, 0}, {0, 0, 1}});
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

TEST(Text3DRegression, Autoscale) {
    Fig3D cf(256);
    std::vector<Text3DItem> items;
    Text3DItem a; a.x = -1; a.y = 0; a.z = 0; a.text = "A"; a.fontSize = 16;
    Text3DItem b; b.x = 2; b.y = 3; b.z = 1; b.text = "B"; b.fontSize = 16;
    items.push_back(a);
    items.push_back(b);
    auto plot = std::make_unique<Text3D>(std::move(items));
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 1.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -1.0f);
    EXPECT_GE(av.x.max, 2.0f);
    EXPECT_LE(av.y.min, 0.0f);
    EXPECT_GE(av.y.max, 3.0f);
    EXPECT_LE(av.z.min, 0.0f);
    EXPECT_GE(av.z.max, 1.0f);
}

TEST(Text3DRegression, LongTextRendersMore) {
    Fig3D cf1(256);
    auto p1 = std::make_unique<Text3D>(0.0f, 0.0f, 0.0f, "Hi",
                                        Color::black(), 16.0f);
    p1->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    auto p2 = std::make_unique<Text3D>(0.0f, 0.0f, 0.0f, "Hello World",
                                        Color::black(), 16.0f);
    p2->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    size_t count1 = countPixels(img1, isNotWhite);
    size_t count2 = countPixels(img2, isNotWhite);
    EXPECT_GT(count2, count1) << "Longer text should render more pixels";
}

TEST(Text3DRegression, TextAtDifferentPositions) {
    Fig3D cf(256);
    std::vector<Text3DItem> items;
    // Place text at two well-separated positions.
    Text3DItem a; a.x = -2; a.y = -2; a.z = 0; a.text = "Left"; a.color = Color::black(); a.fontSize = 16;
    Text3DItem b; b.x = 2; b.y = 2; b.z = 0; b.text = "Right"; b.color = Color::black(); b.fontSize = 16;
    items.push_back(a);
    items.push_back(b);
    auto plot = std::make_unique<Text3D>(std::move(items));
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Text at different positions should render";
}

TEST(Text3DRegression, EmptyStringRendersNothing) {
    Fig3D cf(256);
    auto plot = std::make_unique<Text3D>(0.0f, 0.0f, 0.0f, "",
                                          Color::black(), 16.0f);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty string should render nothing";
}
