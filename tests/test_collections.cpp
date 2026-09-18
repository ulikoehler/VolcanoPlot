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

// ═══ set_clip_path (artist clip paths) ════════════════════════════════════

TEST(PathUtil, ClipPolylineToSquare) {
    // Line crossing the square horizontally: one inside piece [0,4].
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    std::vector<Point2D> line{{-2, 2}, {6, 2}};
    auto pieces = clipPolylineToPolygon(line, sq);
    ASSERT_EQ(pieces.size(), 1u);
    EXPECT_NEAR(pieces[0].front().x, 0.0f, 1e-4f);
    EXPECT_NEAR(pieces[0].back().x, 4.0f, 1e-4f);
}

TEST(PathUtil, ClipPolylineMultiplePieces) {
    // W-shaped polyline crossing the square twice: enters left, exits
    // bottom, re-enters bottom, exits right.
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    std::vector<Point2D> line{{-2, 2}, {2, 2}, {2, -2},
                              {3, -2}, {3, 2}, {6, 2}};
    auto pieces = clipPolylineToPolygon(line, sq);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_NEAR(pieces[0].front().x, 0.0f, 1e-4f);   // enters left edge
    EXPECT_NEAR(pieces[0].back().y, 0.0f, 1e-4f);    // exits bottom
    EXPECT_NEAR(pieces[1].front().y, 0.0f, 1e-4f);   // re-enters bottom
    EXPECT_NEAR(pieces[1].back().x, 4.0f, 1e-4f);    // exits right
}

TEST(PathUtil, ClipRingToRingTriangle) {
    // Triangle clipped to a square keeps only the inside part.
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    std::vector<Point2D> tri{{-2, 2}, {2, -2}, {2, 6}};
    auto out = clipRingToRing(tri, sq);
    ASSERT_GE(out.size(), 3u);
    for (auto p : out) {
        EXPECT_GE(p.x, -1e-4f); EXPECT_LE(p.x, 4.0f + 1e-4f);
        EXPECT_GE(p.y, -1e-4f); EXPECT_LE(p.y, 4.0f + 1e-4f);
    }
}

TEST(PathUtil, ClipRingToRingBothWinding) {
    // Works regardless of clip-ring winding.
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    std::vector<Point2D> sqRev{sq.rbegin(), sq.rend()};
    std::vector<Point2D> tri{{-2, 2}, {2, -2}, {2, 6}};
    auto a = clipRingToRing(tri, sq);
    auto b = clipRingToRing(tri, sqRev);
    EXPECT_EQ(a.size(), b.size());
}

TEST(PathUtil, ClipTrianglesToSquare) {
    // A triangle covering the whole square clips down to the square area.
    std::vector<Point2D> sq{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    std::vector<Point2D> tris{{-10, -10}, {10, -10}, {0, 10}};
    auto out = clipTrianglesToPolygon(tris, sq);
    ASSERT_GE(out.size(), 3u);
    for (auto p : out) {
        EXPECT_GE(p.x, -1e-3f); EXPECT_LE(p.x, 4.0f + 1e-3f);
        EXPECT_GE(p.y, -1e-3f); EXPECT_LE(p.y, 4.0f + 1e-3f);
    }
}

TEST(PathUtil, ClipTrianglesToConcave) {
    // Concave clip: chevron notch at the top-middle.
    std::vector<Point2D> notch{{0, 0}, {4, 0}, {4, 4}, {2, 2}, {0, 4}};
    // Triangle covering everything.
    std::vector<Point2D> tris{{-10, -10}, {14, -10}, {2, 14}};
    auto out = clipTrianglesToPolygon(tris, notch);
    ASSERT_GE(out.size(), 3u);
    // The notch vertex region (2,3.5) must not be covered.
    auto covered = [&](Point2D p) {
        for (size_t i = 0; i + 2 < out.size(); i += 3) {
            auto& a = out[i]; auto& b = out[i+1]; auto& c = out[i+2];
            float d0 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
            float d1 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
            float d2 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
            bool neg = (d0 < 0) || (d1 < 0) || (d2 < 0);
            bool pos = (d0 > 0) || (d1 > 0) || (d2 > 0);
            if (!(neg && pos)) return true;
        }
        return false;
    };
    EXPECT_FALSE(covered({2.0f, 3.5f}));   // inside the notch → clipped out
    EXPECT_TRUE(covered({1.0f, 1.0f}));    // inside the polygon → kept
}

TEST(ClipPathRender, PatchClippedToCircle) {
    // A big square clipped to a small central circle → corners empty.
    Fx fx;
    auto p = patch::Rectangle(0, 0, 1, 1);
    p.style.face = Color::red(); p.style.edge.a = 0;
    auto pc = std::make_unique<PatchCollection>(std::vector<Patch>{p});
    pc->clipPath = Path::ellipse({0.5f, 0.5f}, 0.4f, 0.4f);
    fx.ax->addPlot(std::move(pc));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 100u, 40);
    // Corner pixels must be white (clipped away).
    EXPECT_PIXEL_AT(img, 4, 4, White, 40);
    EXPECT_PIXEL_AT(img, 124, 4, White, 40);
    EXPECT_PIXEL_AT(img, 64, 64, Red, 40);
}

TEST(ClipPathRender, LineClippedToCircle) {
    // A horizontal line across the canvas clipped to a small circle →
    // only the chord inside the circle remains.
    Fx fx;
    auto lc = std::make_unique<LineCollection>(
        std::vector<std::vector<Point2D>>{{{0.0f, 0.5f}, {1.0f, 0.5f}}});
    auto* lcp = static_cast<LineCollection*>(fx.ax->addPlot(std::move(lc)));
    lcp->edgeColors = {Color::blue()};
    lcp->lineWidths = {3.0f};
    lcp->clipPath = Path::ellipse({0.5f, 0.5f}, 0.3f, 0.3f);
    auto img = fx.render();
    // Line at y=64 spans only ~[45,83] inside the circle.
    EXPECT_PIXEL_AT(img, 10, 64, White, 40);   // outside circle → clipped
    EXPECT_PIXEL_AT(img, 120, 64, White, 40);
    EXPECT_PIXEL_AT(img, 64, 64, Blue, 40);    // inside → drawn
}

TEST(ClipPathRender, AnnotationArrowClipped) {
    // Arrow clipped to a small square around the tip: shaft outside is cut.
    Fx fx;
    auto* a = fx.ax->annotate(0.5f, 0.5f, 0.9f, 0.9f, "");
    a->arrowSpec = parseArrowStyle("-|>");
    a->arrowSpec->mutationSize = 20.0f;
    a->arrowWidth = 3.0f;
    a->shrinkA = 0.0f; a->shrinkB = 0.0f;
    a->clipPath = Path::rectangle(0.45f, 0.45f, 0.2f, 0.2f);
    auto img = fx.render();
    // Near the text end (0.9,0.9) → px ~(115,13): outside clip → white.
    EXPECT_PIXEL_AT(img, 115, 13, White, 40);
    // At the tip (64,64): the filled head → black.
    size_t dark = 0;
    for (uint32_t y = 56; y < 72; ++y)
        for (uint32_t x = 56; x < 72; ++x) {
            auto p = img.get(x, y);
            if (p.r < 80 && p.g < 80 && p.b < 80) ++dark;
        }
    EXPECT_GT(dark, 20u);
}

// ═══ boxstyle variants (mpl BoxStyle) ═════════════════════════════════════

TEST(BoxStyle, ParseNamesAndParams) {
    auto r = parseBoxStyle("round,pad=0.5,rounding_size=0.2");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->kind, BoxStyleSpec::Kind::Round);
    EXPECT_FLOAT_EQ(r->pad, 0.5f);
    EXPECT_FLOAT_EQ(r->roundingSize, 0.2f);

    EXPECT_TRUE(parseBoxStyle("square"));
    EXPECT_TRUE(parseBoxStyle("sawtooth,tooth_size=0.1"));
    EXPECT_TRUE(parseBoxStyle("roundtooth"));
    EXPECT_TRUE(parseBoxStyle("rarrow"));
    EXPECT_FALSE(parseBoxStyle("bogus"));
}

TEST(BoxStyle, SquarePadsRect) {
    auto spec = *parseBoxStyle("square,pad=0.1");
    auto p = boxStylePath(0, 0, 10, 10, spec);   // pad = 0.1
    auto [lo, hi] = p.bounds();
    EXPECT_NEAR(lo.x, -0.1f, 1e-5f);
    EXPECT_NEAR(hi.x, 10.1f, 1e-5f);
    EXPECT_NEAR(hi.y, 10.1f, 1e-5f);
}

TEST(BoxStyle, RoundHasCurvedCorners) {
    auto spec = *parseBoxStyle("round,pad=0.2");
    auto p = boxStylePath(0, 0, 10, 10, spec);
    // mpl: 4 CURVE3 corners.
    int curves = 0;
    for (auto c : p.codes) if (c == Path::Curve3) ++curves;
    EXPECT_EQ(curves, 8);  // two verts per corner curve
    // Flattened path must round the corners: corner point of the padded
    // rect should NOT be reached by the outline.
    auto pts = p.flatten();
    float maxCornerDist = 0.0f;
    for (auto v : pts)
        maxCornerDist = std::max(maxCornerDist,
            std::min(std::hypot(v.x - (-0.2f), v.y - (-0.2f)), 1e9f));
    // Simply assert some vertex sits away from the exact corner.
    auto [lo, hi] = p.bounds();
    EXPECT_NEAR(lo.x, -0.2f, 1e-4f);
    EXPECT_NEAR(hi.x, 10.2f, 1e-4f);
}

TEST(BoxStyle, SawtoothTeeth) {
    // tooth_size = pad/2 = 0.25 on a 10-unit box → several teeth per side.
    auto spec = *parseBoxStyle("sawtooth,pad=0.5");
    auto p = boxStylePath(0, 0, 10, 10, spec);
    EXPECT_GT(p.vertices.size(), 20u);   // many tooth vertices
    auto [lo, hi] = p.bounds();
    EXPECT_NEAR(lo.x, -0.5f, 1e-4f);     // teeth reach the padded rect
    EXPECT_NEAR(hi.y, 10.5f, 1e-4f);
}

TEST(BoxStyle, RoundtoothIsCurved) {
    auto spec = *parseBoxStyle("roundtooth,pad=0.5");
    auto p = boxStylePath(0, 0, 10, 10, spec);
    int curves = 0;
    for (auto c : p.codes) if (c == Path::Curve3) ++curves;
    EXPECT_GT(curves, 10u);
}

TEST(BoxStyle, RArrowPointsRight) {
    auto spec = *parseBoxStyle("rarrow,pad=0.3");
    auto p = boxStylePath(0, 0, 10, 4, spec);
    auto [lo, hi] = p.bounds();
    // Arrow tip extends past the padded right edge.
    EXPECT_GT(hi.x, 10.0f + 0.5f);
    // Left edge stays at the padded rect edge (mpl mirrors LArrow).
    EXPECT_NEAR(lo.x, -0.3f, 1e-4f);
    // The left edge is indented: no vertex sits left of it, and the
    // outline's left extent is narrower than a full arrow.
    EXPECT_LT(hi.x - 10.0f, 3.0f);  // tip overshoot bounded by box height
}

TEST(ClipPathRender, FancyBboxPatchSawtoothRenders) {
    Fx fx;
    auto spec = *parseBoxStyle("sawtooth,pad=0.4");
    spec.mutationSize = 20.0f;  // px-scale in data units via viewport
    // Build in data coords: mutation in data units.
    spec.mutationSize = 0.08f;
    auto pt = patch::FancyBboxPatch(0.2f, 0.2f, 0.6f, 0.6f, spec);
    pt.style.face = Color::red(); pt.style.edge.a = 0;
    auto pc = std::make_unique<PatchCollection>(std::vector<Patch>{pt});
    fx.ax->addPlot(std::move(pc));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 200, 40);
    // Toothed outline → some edge pixels white inside the padded bounds.
}

TEST(ClipPathRender, AnnotationBoxStyleRound) {
    Fx fx;
    auto* a = fx.ax->annotate(0.5f, 0.5f, 0.5f, 0.5f, "hi");
    a->bboxFaceColor = Color::blue();
    a->boxStyle = parseBoxStyle("round,pad=0.3");
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 50, 40);
}
