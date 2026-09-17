// tests/test_collections.cpp — §14 paths, patches, collections, hatches
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Collections.hpp>
#include <volcano/plot/Plot.hpp>

using namespace volcano;
using namespace volcano::plot;

namespace {

constexpr test::Pixel Red{255, 0, 0, 255};
constexpr test::Pixel Green{0, 255, 0, 255};
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

// ═══ Path ═══════════════════════════════════════════════════════════════════

TEST(PathTest, FlattenLinesAndClose) {
    Path p;
    p.moveTo({0, 0}); p.lineTo({1, 0}); p.lineTo({1, 1}); p.close();
    auto subs = p.toPolylines();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_TRUE(subs[0].closed);
    EXPECT_EQ(subs[0].points.size(), 3u);
}

TEST(PathTest, FlattenCubic) {
    Path p;
    p.moveTo({0, 0});
    p.curve4({0, 1}, {1, 1}, {1, 0});
    auto subs = p.toPolylines(8);
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].points.size(), 9u); // start + 8 subdivisions
    // Midpoint of the cubic lies above the chord.
    EXPECT_GT(subs[0].points[4].y, 0.5f);
}

TEST(PathTest, FlattenQuadratic) {
    Path p;
    p.moveTo({0, 0});
    p.curve3({0.5f, 1.0f}, {1.0f, 0.0f});
    auto subs = p.toPolylines(4);
    EXPECT_EQ(subs[0].points.size(), 5u);
    EXPECT_NEAR(subs[0].points[2].x, 0.5f, 1e-4f);
}

TEST(PathTest, Bounds) {
    Path p = Path::rectangle(2, 3, 4, 5);
    auto [lo, hi] = p.bounds();
    EXPECT_EQ(lo.x, 2); EXPECT_EQ(lo.y, 3);
    EXPECT_EQ(hi.x, 6); EXPECT_EQ(hi.y, 8);
}

TEST(PathTest, ContainsPoint) {
    Path p = Path::rectangle(0, 0, 1, 1);
    EXPECT_TRUE(p.containsPoint({0.5f, 0.5f}));
    EXPECT_FALSE(p.containsPoint({1.5f, 0.5f}));
}

TEST(PathTest, Transformed) {
    Path p = Path::rectangle(0, 0, 1, 1);
    Affine2D m = Affine2D::translate(10, 20);
    auto q = p.transformed(m);
    auto [lo, hi] = q.bounds();
    EXPECT_EQ(lo.x, 10); EXPECT_EQ(hi.y, 21);
}

TEST(PathTest, UnitShapes) {
    EXPECT_EQ(Path::unitRegularPolygon(5).vertices.size(), 6u); // +close vert
    EXPECT_EQ(Path::unitStar(5).vertices.size(), 11u);
    EXPECT_EQ(Path::unitAsterisk(4).vertices.size(), 8u);
    EXPECT_TRUE(Path::unitWedge(0, 90).containsPoint({0.5f, 0.5f}));
    EXPECT_FALSE(Path::unitWedge(0, 90).containsPoint({-0.5f, -0.5f}));
}

// ═══ earClip / clipping ═══════════════════════════════════════════════════

TEST(PathUtil, EarClipTriangle) {
    std::array<Point2D, 3> tri{{{0, 0}, {4, 0}, {0, 4}}};
    auto t = earClip(tri);
    ASSERT_EQ(t.size(), 3u);
}

TEST(PathUtil, EarClipConcave) {
    // Arrow/chevron shape — reflex vertex at (1, 1).
    std::vector<Point2D> poly{{0, 0}, {4, 0}, {4, 4}, {1, 1}, {0, 4}};
    auto t = earClip(poly);
    ASSERT_EQ(t.size(), 9u); // 3 triangles
}

TEST(PathUtil, ClipSegmentToSquare) {
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    auto segs = clipSegmentToPolygon({-2, 2}, {6, 2}, sq);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_NEAR(segs[0].first.x, 0.0f, 1e-4f);
    EXPECT_NEAR(segs[0].second.x, 4.0f, 1e-4f);
}

TEST(PathUtil, ClipSegmentMissesPolygon) {
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    EXPECT_TRUE(clipSegmentToPolygon({-2, 6}, {6, 6}, sq).empty());
}

// ═══ Hatch ════════════════════════════════════════════════════════════════

TEST(PathUtil, HatchProducesLines) {
    std::vector<Point2D> sq{{0, 0}, {40, 0}, {40, 40}, {0, 40}};
    auto t = hatchTriangles(sq, "/", 6.0f);
    EXPECT_GT(t.size(), 0u);
    auto denser = hatchTriangles(sq, "//", 6.0f);
    EXPECT_GT(denser.size(), t.size());
    auto cross = hatchTriangles(sq, "x", 6.0f);
    EXPECT_GT(cross.size(), t.size()); // 'x' = both diagonals
}

// ═══ Patch renders ════════════════════════════════════════════════════════

TEST(PatchRender, RectangleFills) {
    Fx fx;
    auto& p = fx.ax->addPatch(patch::Rectangle(0.25f, 0.25f, 0.5f, 0.5f));
    p.style.face = Color::red(); p.style.edge.a = 0;
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 500, 40);
}

TEST(PatchRender, CircleRendersRound) {
    Fx fx;
    auto& p = fx.ax->addPatch(patch::Circle({0.5f, 0.5f}, 0.3f));
    p.style.face = Color::blue(); p.style.edge.a = 0;
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 400, 40);
    // Corners of the bbox are empty (it's round).
    EXPECT_PIXEL_AT(img, 64 - 38, 64 - 38, White, 40);
}

TEST(PatchRender, PolygonConcave) {
    Fx fx;
    auto& p = fx.ax->addPatch(patch::Polygon(
        {{0.1f, 0.1f}, {0.9f, 0.1f}, {0.9f, 0.9f}, {0.5f, 0.4f}, {0.1f, 0.9f}}));
    p.style.face = Color::green(); p.style.edge.a = 0;
    auto img = fx.render();
    // Notch at (0.5, 0.4) is concave → pixel there should be background.
    // data(0.5,0.4) → px (64, 128*(1-0.4)=76.8)
    EXPECT_PIXEL_COUNT(img, Green, 2000, 40);
}

TEST(PatchRender, EdgeStrokeDraws) {
    Fx fx;
    auto& p = fx.ax->addPatch(patch::Rectangle(0.25f, 0.25f, 0.5f, 0.5f));
    p.style.face.a = 0;
    p.style.edge = Color::red();
    p.style.lineWidth = 4.0f;
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 60, 40);
}

TEST(PatchRender, HatchDrawsEdgeColorInside) {
    Fx fx;
    auto& p = fx.ax->addPatch(patch::Rectangle(0.2f, 0.2f, 0.6f, 0.6f));
    p.style.face = Color::white();
    p.style.edge = Color::blue();
    p.style.lineWidth = 0;      // no border
    p.style.hatch = "///";
    p.style.hatchSpacing = 8.0f;
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 30, 40);
}

TEST(PatchRender, WedgeRenders) {
    Fx fx;
    auto& p = fx.ax->addPatch(patch::Wedge({0.5f, 0.5f}, 0.35f, 0, 90));
    p.style.face = Color::red(); p.style.edge.a = 0;
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 200, 40);
}

TEST(PatchRender, FancyArrowRenders) {
    Fx fx;
    auto& p = fx.ax->addPatch(
        patch::FancyArrowPatch({0.2f, 0.2f}, {0.8f, 0.8f}, 0.15f, 0.15f));
    p.style.face = Color::blue(); p.style.edge.a = 0;
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 30, 40);
}

// ═══ Collection renders ═══════════════════════════════════════════════════

TEST(CollectionRender, LineCollectionDrawsSegments) {
    Fx fx;
    auto lc = std::make_unique<LineCollection>(std::vector<std::vector<Point2D>>{
        {{0.1f, 0.5f}, {0.9f, 0.5f}},
        {{0.5f, 0.1f}, {0.5f, 0.9f}},
    });
    auto* lcp = static_cast<LineCollection*>(fx.ax->addPlot(std::move(lc)));
    lcp->edgeColors = {Color::red(), Color::blue()};
    lcp->lineWidths = {4.0f, 4.0f};
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 60, 40);
    EXPECT_PIXEL_COUNT(img, Blue, 60, 40);
}

TEST(CollectionRender, PolyCollectionFillsTwo) {
    Fx fx;
    auto pc = std::make_unique<PolyCollection>(std::vector<std::vector<Point2D>>{
        {{0.05f, 0.05f}, {0.4f, 0.05f}, {0.4f, 0.4f}, {0.05f, 0.4f}},
        {{0.6f, 0.6f}, {0.95f, 0.6f}, {0.95f, 0.95f}, {0.6f, 0.95f}},
    });
    auto* pcp = static_cast<PolyCollection*>(fx.ax->addPlot(std::move(pc)));
    pcp->faceColors = {Color::red(), Color::green()};
    pcp->edgeColors = {Color{0,0,0,0}, Color{0,0,0,0}};
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 300, 40);
    EXPECT_PIXEL_COUNT(img, Green, 300, 40);
}

TEST(CollectionRender, CircleCollectionInstances) {
    Fx fx;
    auto cc = std::make_unique<CircleCollection>(
        std::vector<float>{0.15f, 0.15f},
        std::vector<Point2D>{{0.25f, 0.5f}, {0.75f, 0.5f}});
    auto* ccp = static_cast<CircleCollection*>(fx.ax->addPlot(std::move(cc)));
    ccp->faceColors = {Color::red()};
    ccp->edgeColors = {Color{0, 0, 0, 0}};
    auto img = fx.render();
    // Two circles: left-center and right-center.
    EXPECT_PIXEL_COUNT(img, Red, 200, 40);
    // Center between them should be background.
    EXPECT_PIXEL_AT(img, 64, 64, White, 40);
}

TEST(CollectionRender, RegularPolyCollection) {
    Fx fx;
    auto rp = std::make_unique<RegularPolyCollection>(
        5, std::vector<float>{0.2f}, std::vector<Point2D>{{0.5f, 0.5f}});
    auto* rpp = static_cast<RegularPolyCollection*>(fx.ax->addPlot(std::move(rp)));
    rpp->faceColors = {Color::blue()};
    rpp->edgeColors = {Color{0, 0, 0, 0}};
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 200, 40);
}

TEST(CollectionRender, AsteriskIsStrokeOnly) {
    Fx fx;
    auto ap = std::make_unique<AsteriskPolygonCollection>(
        6, std::vector<float>{0.25f}, std::vector<Point2D>{{0.5f, 0.5f}});
    auto* app = static_cast<AsteriskPolygonCollection*>(fx.ax->addPlot(std::move(ap)));
    app->faceColors = {Color::red()};
    app->edgeColors = {Color::blue()};
    app->lineWidths = {3.0f};
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 20, 40);
    EXPECT_PIXEL_COUNT(img, Red, 0, 40); // never filled
}

TEST(CollectionRender, QuadMeshCellColors) {
    Fx fx;
    // 1×2 mesh: left red, right blue.
    auto qm = std::make_unique<QuadMesh>(
        1, 2,
        std::vector<Point2D>{{0, 0}, {0.5f, 0}, {1, 0},
                             {0, 1}, {0.5f, 1}, {1, 1}},
        std::vector<Color>{Color::red(), Color::blue()});
    fx.ax->addPlot(std::move(qm));
    auto img = fx.render();
    EXPECT_PIXEL_AT(img, 32, 64, Red, 40);
    EXPECT_PIXEL_AT(img, 96, 64, Blue, 40);
}

TEST(CollectionRender, TriMeshFillsTriangles) {
    Fx fx;
    auto tm = std::make_unique<TriMeshCollection>(
        std::vector<Point2D>{{0.1f, 0.1f}, {0.9f, 0.1f}, {0.5f, 0.9f}},
        std::vector<std::array<uint32_t, 3>>{{0, 1, 2}});
    auto* tmp = static_cast<TriMeshCollection*>(fx.ax->addPlot(std::move(tm)));
    tmp->faceColors = {Color::red()};
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 1000, 40);
}

TEST(CollectionRender, OffsetTransformShiftsItems) {
    Fx fx;
    // offsets are display px when offsetTransform = transDisplay.
    auto cc = std::make_unique<CircleCollection>(
        std::vector<float>{0.1f},
        std::vector<Point2D>{{64, 64}});
    auto* ccp = static_cast<CircleCollection*>(fx.ax->addPlot(std::move(cc)));
    ccp->faceColors = {Color::red()};
    ccp->edgeColors = {Color{0, 0, 0, 0}};
    ccp->offsetTransform = transDisplay();
    auto img = fx.render();
    // Circle centered at px (64, 64): center pixel is red.
    EXPECT_PIXEL_AT(img, 64, 64, Red, 40);
}

TEST(CollectionRender, PathCollectionPerItemSize) {
    Fx fx;
    PathCollection pc(Path::unitRegularPolygon(4),
                      {{0.25f, 0.5f}, {0.75f, 0.5f}});
    auto* raw = static_cast<PathCollection*>(
        fx.ax->addPlot(std::make_unique<PathCollection>(std::move(pc))));
    raw->sizes = {{0.05f, 0.05f}, {0.2f, 0.2f}};
    raw->faceColors = {Color::red()};
    raw->edgeColors = {Color{0, 0, 0, 0}};
    auto img = fx.render();
    // Larger right diamond covers more pixels than left.
    auto leftCount = img.countColorInRegion(Red, 0, 32, 64, 96, 40);
    auto rightCount = img.countColorInRegion(Red, 64, 32, 128, 96, 40);
    EXPECT_GT(leftCount, 20);
    EXPECT_GT(rightCount, leftCount * 3);
}
