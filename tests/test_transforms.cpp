// tests/test_transforms.cpp — §13 transform hierarchy
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Transform.hpp>

using namespace volcano;
using namespace volcano::plot;

namespace {

constexpr float kEps = 1e-4f;

void expectPt(Point2D p, float x, float y) {
    EXPECT_NEAR(p.x, x, kEps);
    EXPECT_NEAR(p.y, y, kEps);
}

/// A non-affine transform for generic-composite coverage.
class Parabola final : public Transform {
public:
    Point2D apply(Point2D p) const override {
        return {p.x, p.y * p.y};
    }
    TransformPtr clone() const override {
        return std::make_shared<Parabola>(*this);
    }
};

} // namespace

// ═══ Affine2D ═══════════════════════════════════════════════════════════════

TEST(Affine2DTransform, Identity) {
    expectPt(Affine2D::identity().apply({3, -7}), 3, -7);
}

TEST(Affine2DTransform, ScaleTranslate) {
    auto t = Affine2D::scale(2, 3).concat(Affine2D::translate(5, 7));
    // translate first, then scale: (1,1) → (6,8) → (12,24)
    expectPt(t.apply({1, 1}), 12, 24);
}

TEST(Affine2DTransform, Rotate90) {
    expectPt(Affine2D::rotateDeg(90).apply({1, 0}), 0, 1);
    expectPt(Affine2D::rotateDeg(180).apply({1, 0}), -1, 0);
}

TEST(Affine2DTransform, RotateAround) {
    // Rotate (2,1) 90° around (1,1): relative (1,0) → (0,1) → (1,2).
    expectPt(Affine2D::rotateAround(1, 1, 90).apply({2, 1}), 1, 2);
}

TEST(Affine2DTransform, Skew) {
    // 45° x-shear: (0,1) → (tan(45°),1) = (1,1)
    expectPt(Affine2D::skewDeg(45, 0).apply({0, 1}), 1, 1);
}

TEST(Affine2DTransform, InverseRoundtrip) {
    auto m = Affine2D::translate(4, 9)
                 .concat(Affine2D::rotateDeg(30))
                 .concat(Affine2D::scale(2, 0.5f));
    auto inv = m.inverted();
    ASSERT_NE(inv, nullptr);
    Point2D p{3.3f, -1.7f};
    expectPt(inv->apply(m.apply(p)), p.x, p.y);
}

TEST(Affine2DTransform, SingularNotInvertible) {
    EXPECT_EQ(Affine2D::scale(0, 1).inverted(), nullptr);
}

// ═══ transDisplay / composition ═══════════════════════════════════════════

TEST(TransformTree, TransDisplayIdentity) {
    auto t = transDisplay();
    expectPt(t->apply({42, 17}), 42, 17);
    EXPECT_TRUE(t->isAffine());
    expectPt(t->inverted()->apply({5, 6}), 5, 6);
}

TEST(TransformTree, AffineCompositeCollapses) {
    Affine2D a = Affine2D::scale(2);
    auto c = a.then(std::make_shared<Affine2D>(Affine2D::translate(10, 20)));
    EXPECT_TRUE(c->isAffine());
    // scale then translate: (1,1) → (2,2) → (12,22)
    expectPt(c->apply({1, 1}), 12, 22);
    // Type is the affine composite.
    EXPECT_NE(dynamic_cast<CompositeAffine2D*>(c.get()), nullptr);
}

TEST(TransformTree, GenericComposite) {
    Parabola nl;
    auto c = nl.then(std::make_shared<Affine2D>(Affine2D::scale(2)));
    EXPECT_FALSE(c->isAffine());
    // parabola then scale: (2,3) → (2,9) → (4,18)
    expectPt(c->apply({2, 3}), 4, 18);
}

TEST(TransformTree, CompositeInverseRoundtrip) {
    auto m = std::make_shared<Affine2D>(
        Affine2D::translate(1, 2).concat(Affine2D::scale(3)));
    auto c = m->then(std::make_shared<Affine2D>(Affine2D::rotateDeg(45)));
    auto inv = c->inverted();
    ASSERT_NE(inv, nullptr);
    Point2D p{0.5f, 2.5f};
    expectPt(inv->apply(c->apply(p)), p.x, p.y);
}

// ═══ Blended transforms ═══════════════════════════════════════════════════

TEST(TransformTree, BlendedAffine) {
    auto b = blendedTransformFactory(
        std::make_shared<Affine2D>(Affine2D::scale(10)),
        std::make_shared<Affine2D>(Affine2D::scale(100)));
    EXPECT_TRUE(b->isAffine());
    EXPECT_NE(dynamic_cast<BlendedAffine2D*>(b.get()), nullptr);
    // x scaled by 10, y scaled by 100.
    expectPt(b->apply({2, 3}), 20, 300);
}

TEST(TransformTree, BlendedGeneric) {
    auto b = blendedTransformFactory(
        std::make_shared<Affine2D>(Affine2D::scale(10)),
        std::make_shared<Parabola>());
    EXPECT_FALSE(b->isAffine());
    expectPt(b->apply({2, 3}), 20, 9);
}

// ═══ offset_copy ═══════════════════════════════════════════════════════════

TEST(TransformTree, OffsetCopyPoints) {
    // At 72dpi, 1pt = 1px. +x right, +y up (Y-down display → y shrinks).
    auto t = offsetCopy(*transDisplay(), 72.0f, 10.0f, 5.0f, "points");
    expectPt(t->apply({0, 0}), 10, -5);
}

TEST(TransformTree, OffsetCopyInches) {
    auto t = offsetCopy(*transDisplay(), 100.0f, 1.0f, 0.5f, "inches");
    expectPt(t->apply({0, 0}), 100, -50);
}

TEST(TransformTree, OffsetCopyPixels) {
    auto t = offsetCopy(*transDisplay(), 300.0f, 7.0f, -3.0f, "pixels");
    expectPt(t->apply({2, 2}), 9, 5);
}

TEST(TransformTree, OffsetCopyOnBound) {
    Figure fig;
    fig.layout(Extent2D{200, 100});
    // Offset transFigure by 12pt at 72dpi = 12px.
    auto t = offsetCopy(*fig.transFigure(), 72.0f, 12.0f, 0.0f, "points");
    expectPt(t->apply({0.5f, 0.5f}), 112, 50);
}

// ═══ Bound transforms (transAxes / transFigure / transData) ════════════════

TEST(TransformTree, TransAxesFractionToPixels) {
    Figure fig;
    auto* ax = fig.addAxes();
    fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
    fig.layout(Extent2D{200, 100});
    auto t = ax->transAxes();
    // (0,0) = bottom-left of axes → bottom-left pixel (y = rect bottom).
    expectPt(t->apply({0, 0}), ax->rect.x, ax->rect.y + ax->rect.height);
    expectPt(t->apply({1, 1}), ax->rect.x + ax->rect.width, ax->rect.y);
    expectPt(t->apply({0.5f, 0.5f}), 100, 50);
}

TEST(TransformTree, TransAxesTracksRelayout) {
    Figure fig;
    auto* ax = fig.addAxes();
    fig.layout(Extent2D{200, 100});
    auto t = ax->transAxes();
    Point2D before = t->apply({1, 1});
    fig.layout(Extent2D{400, 200}); // relayout doubles the canvas
    Point2D after = t->apply({1, 1});
    EXPECT_GT(after.x, before.x);
    // And it lands on the (new) rect's top-right corner.
    expectPt(after, ax->rect.x + ax->rect.width, ax->rect.y);
}

TEST(TransformTree, TransFigureFractionToPixels) {
    Figure fig;
    fig.layout(Extent2D{200, 100});
    auto t = fig.transFigure();
    expectPt(t->apply({0, 0}), 0, 100);  // bottom-left
    expectPt(t->apply({1, 1}), 200, 0);  // top-right
}

TEST(TransformTree, TransDataRoundtrip) {
    Figure fig;
    auto* ax = fig.addAxes();
    fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
    fig.layout(Extent2D{200, 100});
    ax->setViewport({{0, 10}, {0, 5}});
    auto t = ax->transData();
    // data (5, 2.5) → fraction (0.5, 0.5) → pixel (100, 50)
    expectPt(t->apply({5, 2.5f}), 100, 50);
    auto inv = t->inverted();
    ASSERT_NE(inv, nullptr);
    Point2D p = inv->apply({100, 50});
    EXPECT_NEAR(p.x, 5, kEps);
    EXPECT_NEAR(p.y, 2.5f, kEps);
}

TEST(TransformTree, TransDataInverseViaBoundAxes) {
    Figure fig;
    auto* ax = fig.addAxes();
    fig.layout(Extent2D{200, 100});
    ax->setViewport({{0, 1}, {0, 1}});
    auto inv = ax->transData()->inverted();
    Point2D d = inv->apply({100, 50});
    EXPECT_NEAR(d.x, 0.5f, 0.05f);
    EXPECT_NEAR(d.y, 0.5f, 0.05f);
}

TEST(TransformTree, BlendedDataWithAxes) {
    // The classic mpl pattern: x in data coords, y in axes fraction.
    Figure fig;
    auto* ax = fig.addAxes();
    fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
    fig.layout(Extent2D{200, 100});
    ax->setViewport({{0, 10}, {0, 10}});
    auto t = blendedTransformFactory(ax->transData(), ax->transAxes());
    // x = data 5 → px 100; y = axes fraction 0.25 → px 75.
    expectPt(t->apply({5, 0.25f}), 100, 75);
}

TEST(TransformTree, CloneKeepsLiveBinding) {
    Figure fig;
    auto* ax = fig.addAxes();
    fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
    fig.layout(Extent2D{200, 100});
    auto t = ax->transAxes()->clone();
    fig.layout(Extent2D{400, 200});
    expectPt(t->apply({0.5f, 0.5f}), 200, 100);
}

TEST(TransformTree, BatchApply) {
    Affine2D m = Affine2D::translate(1, 1);
    std::vector<Point2D> in{{0, 0}, {1, 1}, {2, 3}};
    auto out = m.apply(std::span<const Point2D>(in));
    ASSERT_EQ(out.size(), 3u);
    expectPt(out[2], 3, 4);
}
