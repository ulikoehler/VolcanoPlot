// tests/test_basic.cpp — basic smoke tests
#include <gtest/gtest.h>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Plot.hpp>

TEST(Types, ColorFromRgba8) {
    using namespace volcano::plot;
    auto c = Color::fromRgba8(255, 0, 0, 255);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(Colormap, ViridisSample) {
    using namespace volcano::plot;
    auto& cm = colormaps::viridis();
    auto c0 = cm.sample(0.0f);
    auto c1 = cm.sample(1.0f);
    EXPECT_NEAR(c0.r, 68/255.0f, 0.01f);
    EXPECT_NEAR(c1.b, 37/255.0f, 0.01f);
}

TEST(Style, GgplotHasGrayBackground) {
    using namespace volcano::plot;
    auto s = styles::ggplotStyle();
    EXPECT_NEAR(s.faceColor.r, 238/255.0f, 0.01f);
}

TEST(Transform, ToNdc) {
    using namespace volcano::plot;
    Transform2D t;
    t.view = {{0, 10}, {0, 10}};
    auto p = t.toNdc({5, 5});
    EXPECT_NEAR(p.x, 0.0f, 1e-5f);
    EXPECT_NEAR(p.y, 0.0f, 1e-5f);
}

TEST(Figure, AddAxes) {
    using namespace volcano::plot;
    Figure f(2, 2);
    auto* a = f.addAxes(0, 0);
    EXPECT_NE(a, nullptr);
    f.layout(Extent2D{800, 600});
    EXPECT_GT(a->rect.width, 0u);
}

TEST(Colormap, AvailableNames) {
    using namespace volcano::plot;
    auto names = Colormap::availableNames();
    EXPECT_GE(names.size(), 8u);
}
