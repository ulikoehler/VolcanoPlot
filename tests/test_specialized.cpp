// tests/test_specialized.cpp — §15: squarify/treemap, table, sankey, xkcd, radar
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Collections.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Specialized.hpp>
#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/Style.hpp>

#include <cmath>
#include <numeric>

using namespace volcano;
using namespace volcano::plot;

namespace {

constexpr test::Pixel Red{255, 0, 0, 255};
constexpr test::Pixel Blue{0, 0, 255, 255};
constexpr test::Pixel White{255, 255, 255, 255};

struct Fx {
    test::PlotTestHarness h{128, 128};
    Figure fig;
    Axes* ax = fig.addAxes();
    Fx() {
        ax->setStyle(test::flatTestStyle());
        fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
        fig.layout(Extent2D{128, 128});
        ax->setViewport({{0, 1}, {0, 1}});
    }
    test::Image render() { return h.render(fig); }
};

} // namespace

// ═══ squarify ═════════════════════════════════════════════════════════════

TEST(Squarify, AreasPreserved) {
    std::array<float, 3> sizes{6.f, 4.f, 3.f};
    auto rects = squarify(sizes, 0, 0, 1, 1);
    ASSERT_EQ(rects.size(), 3u);
    // Total area ≈ 1 (unit square), proportions ≈ sizes.
    float total = 0;
    for (auto& r : rects) total += r.w * r.h;
    EXPECT_NEAR(total, 1.0f, 1e-3f);
    EXPECT_NEAR(rects[0].w * rects[0].h / (rects[2].w * rects[2].h),
                6.0f / 3.0f, 0.05f);
}

TEST(Squarify, NoOverlapAndInside) {
    std::vector<float> sizes{10, 6, 4, 3, 2, 1};
    auto rects = squarify(sizes, 0, 0, 2, 1);
    ASSERT_EQ(rects.size(), sizes.size());
    for (auto& r : rects) {
        EXPECT_GE(r.x, -1e-4f); EXPECT_GE(r.y, -1e-4f);
        EXPECT_LE(r.x + r.w, 2.0001f); EXPECT_LE(r.y + r.h, 1.0001f);
    }
    // Pairwise disjoint (allow touching edges).
    for (size_t i = 0; i < rects.size(); ++i)
        for (size_t j = i + 1; j < rects.size(); ++j) {
            float ix = std::min(rects[i].x + rects[i].w, rects[j].x + rects[j].w)
                     - std::max(rects[i].x, rects[j].x);
            float iy = std::min(rects[i].y + rects[i].h, rects[j].y + rects[j].h)
                     - std::max(rects[i].y, rects[j].y);
            EXPECT_LE(std::min(ix, iy), 1e-4f);
        }
}

TEST(Squarify, ReasonableAspect) {
    // Equal sizes in a square should give near-square rectangles.
    std::array<float, 4> sizes{1, 1, 1, 1};
    auto rects = squarify(sizes, 0, 0, 1, 1);
    for (auto& r : rects) {
        float aspect = std::max(r.w / r.h, r.h / r.w);
        EXPECT_LT(aspect, 2.5f);
    }
}

// ═══ treemap ══════════════════════════════════════════════════════════════

TEST(Treemap, AddsPatchesAndLabels) {
    Fx fx;
    std::vector<float> sizes{5, 3, 2};
    treemap(*fx.ax, sizes, {"a", "b", "c"},
            {Color::red(), Color::blue(), Color::green()});
    // 3 PatchCollections + text annotations.
    EXPECT_EQ(fx.ax->plots().size(), 3u);
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 1000, 40);
    EXPECT_PIXEL_COUNT(img, Blue, 200, 40);
}

// ═══ table ════════════════════════════════════════════════════════════════

TEST(TablePlot, RendersCellsAndText) {
    Fx fx;
    // mpl loc='bottom' hangs the table off the bottom spine — give the
    // figure a bottom margin so the cells land on canvas.
    fx.fig.subplotsAdjust(0, 0.45f, 1, 1, 0, 0);
    auto& tbl = fx.ax->table({{"1", "2"}, {"3", "4"}});
    tbl.cellColors = {{Color::red(), Color::red()},
                      {Color::red(), Color::red()}};
    tbl.edgeColor = Color::blue();
    auto img = fx.render();
    // Red cells + blue borders below the axes bottom edge.
    uint32_t y0 = uint32_t(fx.ax->rect.y) + fx.ax->rect.height;
    EXPECT_PIXEL_COUNT(img, Red, 1000, 40);
    EXPECT_PIXEL_COUNT(img, Blue, 50, 40);
    EXPECT_GT(img.countIf([y0](uint32_t, uint32_t y, auto p) {
                  return y >= y0 && y < y0 + 20 &&
                         p.r > 200 && p.g < 60 && p.b < 60;
              }),
              200u);
}

TEST(TablePlot, TopLoc) {
    Fx fx;
    // loc='top' hangs the table off the top spine — leave a top margin.
    fx.fig.subplotsAdjust(0, 0, 1, 0.5f, 0, 0);
    auto& tbl = fx.ax->table({{"x"}}, "top");
    tbl.cellColors = {{Color::red()}};
    tbl.edgeColor.a = 0;
    tbl.textColor = Color{0, 0, 0, 0}; // hide glyph
    auto img = fx.render();
    // Axes top at ~(1-0.5)*128=64 → cell spans y≈36..64.
    EXPECT_PIXEL_AT(img, 64, 50, Red, 60);         // near top
    EXPECT_PIXEL_AT(img, 64, 120, White, 60);      // bottom untouched
}

// ═══ Sankey ═══════════════════════════════════════════════════════════════

TEST(Sankey, FinishAddsPatchesAndLabels) {
    Fx fx;
    Sankey sk(*fx.ax);
    sk.add({1.0f, 0.5f, -1.5f}, {"in1", "in2", "out"});
    sk.finish();
    // Trunk + 3 ribbons = 4 patch collections.
    EXPECT_EQ(fx.ax->plots().size(), 4u);
    auto img = fx.render();
    // Ribbons are blue-ish (default color) — check non-white interior.
    auto colored = img.countIf([](uint32_t, uint32_t, test::Pixel p) {
        return p.b > 100 && p.r < 150;
    });
    EXPECT_GT(colored, 200);
}

TEST(Sankey, BalancedFlowsFit) {
    Fx fx;
    Sankey sk(*fx.ax);
    sk.add({2.0f, -2.0f}, {"a", "b"});
    sk.finish();
    auto img = fx.render();
    auto colored = img.countIf([](uint32_t, uint32_t, test::Pixel p) {
        return p.r < 200 && p.b > 80;
    });
    EXPECT_GT(colored, 100);
}

// ═══ xkcd sketch ══════════════════════════════════════════════════════════

TEST(Sketch, WobbleIsDeterministic) {
    std::vector<Point2D> line{{0, 0}, {10, 0}, {20, 0}};
    auto a = sketchPolyline(line, 1.0f);
    auto b = sketchPolyline(line, 1.0f);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].x, b[i].x);
        EXPECT_EQ(a[i].y, b[i].y);
    }
}

TEST(Sketch, WobbleOffsetsPerpendicular) {
    // Horizontal line — wobble should displace in y, not along the chord.
    std::vector<Point2D> line{{0, 0}, {100, 0}};
    auto out = sketchPolyline(line, 2.0f);
    ASSERT_GT(out.size(), 2u);
    float maxDy = 0;
    for (auto& p : out) maxDy = std::max(maxDy, std::abs(p.y));
    EXPECT_GT(maxDy, 0.1f);
    EXPECT_LT(maxDy, 8.0f); // bounded by scale
}

TEST(Sketch, ZeroScalePassThrough) {
    std::vector<Point2D> line{{0, 0}, {5, 5}};
    auto out = sketchPolyline(line, 0.0f);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[1].x, 5.0f);
}

TEST(Sketch, XkcdStyleSetsSketchScale) {
    auto s = styles::xkcdStyle();
    EXPECT_GT(s.sketchScale, 0.0f);
}

// ═══ radar (polar) ════════════════════════════════════════════════════════

TEST(Radar, PolarFillRendersInside) {
    Fx fx;
    fx.ax->setProjection("polar");
    // Radar polygon: closed pentagon in (theta, r) data space.
    std::vector<Point2D> ring;
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i <= 5; ++i) {
        float t = float(i) * 2 * kPi / 5 - kPi / 2;
        ring.push_back({t, 0.8f});
    }
    auto pc = std::make_unique<PolyCollection>(std::vector<std::vector<Point2D>>{ring});
    auto* pcp = static_cast<PolyCollection*>(fx.ax->addPlot(std::move(pc)));
    pcp->faceColors = {Color::red()};
    pcp->edgeColors = {Color{0, 0, 0, 0}};
    fx.ax->setViewport({{-1, 1}, {-1, 1}}); // polar plane bounds
    auto img = fx.render();
    // Pentagon center should be filled.
    EXPECT_PIXEL_AT(img, 64, 64, Red, 40);
    // Corners outside the polygon are background.
    EXPECT_PIXEL_AT(img, 2, 2, White, 40);
}

// ═══ wordcloud ═════════════════════════════════════════════════════════════

TEST(WordCloud, RendersWordsSizedByWeight) {
    Fx fx;
    auto& wc = fx.ax->wordcloud({{"alpha", 10.0}, {"beta", 5.0},
                                 {"gamma", 3.0}, {"delta", 1.0}});
    wc.maxFontScale = 2.5f;
    auto img = fx.render();
    // Any non-white pixels = words rendered.
    size_t painted = img.countIf([](uint32_t, uint32_t, test::Pixel p) {
        return int(p.r) + int(p.g) + int(p.b) < 720;
    });
    EXPECT_GT(painted, 100u);
}

TEST(WordCloud, EmptyWordsNoCrash) {
    Fx fx;
    fx.ax->wordcloud({});
    EXPECT_NO_THROW(fx.render());
}

TEST(WordCloud, DeterministicLayout) {
    // Two identical word clouds must produce identical frames.
    Fx fx1, fx2;
    fx1.ax->wordcloud({{"a", 5.0}, {"b", 3.0}, {"c", 1.0}});
    fx2.ax->wordcloud({{"a", 5.0}, {"b", 3.0}, {"c", 1.0}});
    auto i1 = fx1.render();
    auto i2 = fx2.render();
    ASSERT_EQ(i1.raw().size(), i2.raw().size());
    EXPECT_TRUE(std::ranges::equal(i1.raw(), i2.raw()));
}

// ═══ network ═══════════════════════════════════════════════════════════════

TEST(Network, CircularLayoutPositionsOnCircle) {
    Fx fx;
    NetworkPlot::Options opts;
    opts.layout = NetworkPlot::Layout::Circular;
    auto* net = static_cast<NetworkPlot*>(fx.ax->addPlot(
        std::make_unique<NetworkPlot>(5u,
            std::vector<std::pair<uint32_t,uint32_t>>{{0,1},{1,2},{2,3}},
            opts)));
    // Force layout: render triggers prepare()→computeLayout().
    auto img = fx.render();
    const auto& pos = net->positions();
    ASSERT_EQ(pos.size(), 5u);
    for (const auto& p : pos) {
        float dx = p.x - 0.5f, dy = p.y - 0.5f;
        EXPECT_NEAR(std::sqrt(dx*dx + dy*dy), 0.45f, 1e-4f);
    }
}

TEST(Network, SpringLayoutSeparatesNodes) {
    Fx fx;
    // Two disconnected pairs — repulsion should spread all four nodes.
    auto* net = static_cast<NetworkPlot*>(fx.ax->addPlot(
        std::make_unique<NetworkPlot>(4u,
            std::vector<std::pair<uint32_t,uint32_t>>{{0,1},{2,3}})));
    auto img = fx.render();
    const auto& pos = net->positions();
    ASSERT_EQ(pos.size(), 4u);
    // After spring layout, at least some pairs are far apart.
    float maxD = 0;
    for (auto& a : pos) for (auto& b : pos) {
        float d = std::hypot(a.x - b.x, a.y - b.y);
        maxD = std::max(maxD, d);
    }
    EXPECT_GT(maxD, 0.3f);
}

TEST(Network, RendersEdgesAndNodes) {
    Fx fx;
    NetworkPlot::Options opts;
    opts.layout = NetworkPlot::Layout::Circular;
    opts.nodeColor = Color::red();
    opts.edgeColor = Color::blue();
    opts.nodeSize = 12.0f;
    fx.ax->addPlot(std::make_unique<NetworkPlot>(4u,
        std::vector<std::pair<uint32_t,uint32_t>>{{0,1},{1,2},{2,3},{3,0}},
        opts));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 30, 40);   // 4 node markers
    EXPECT_PIXEL_COUNT(img, Blue, 30, 40);  // edge segments
}

TEST(Network, GivenLayoutUsesProvidedPositions) {
    Fx fx;
    NetworkPlot::Options opts;
    opts.layout = NetworkPlot::Layout::Given;
    opts.positions = {{0.2f, 0.2f}, {0.8f, 0.8f}};
    opts.nodeColor = Color::red();
    opts.nodeSize = 14.0f;
    fx.ax->addPlot(std::make_unique<NetworkPlot>(2u,
        std::vector<std::pair<uint32_t,uint32_t>>{{0,1}}, opts));
    auto img = fx.render();
    // Node at data (0.2,0.2) → pixel ≈ (26, 102) (Y-flip).
    EXPECT_PIXEL_COUNT(img, Red, 30, 40);
    EXPECT_GT(img.countColorInRegion(Red, 15, 92, 40, 115, 40), 5u);
    EXPECT_GT(img.countColorInRegion(Red, 90, 15, 115, 40, 40), 5u);
}
