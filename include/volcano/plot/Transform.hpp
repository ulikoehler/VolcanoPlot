// volcano/plot/Transform.hpp — data↔screen coordinate transforms
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Scale.hpp"
#include "volcano/plot/Projection.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace volcano::plot {

class Axes;
class Figure;
class Transform;
using TransformPtr = std::shared_ptr<Transform>;

// ═══ Transform hierarchy (§13) ════════════════════════════════════════════
//
// matplotlib-style transform tree: every Transform maps an input point to
// display pixels (Y-down, top-left origin). Bound transforms (transData,
// transAxes, transFigure) read the axes rect / figure extent at apply() time
// so they stay valid across relayout.

/// Abstract transform node (matplotlib `Transform` / `TransformNode`).
class Transform {
public:
    virtual ~Transform() = default;

    /// Map one input point to display pixels.
    [[nodiscard]] virtual Point2D apply(Point2D p) const = 0;
    /// True when the transform is a 2D affine (matrix-composable).
    [[nodiscard]] virtual bool isAffine() const noexcept { return false; }
    /// Inverse transform, or nullptr when not invertible.
    [[nodiscard]] virtual TransformPtr inverted() const { return nullptr; }
    /// Deep copy (bound transforms keep their live axes/figure binding).
    [[nodiscard]] virtual TransformPtr clone() const = 0;

    /// mpl `transform.contains_branch(transData)`: true when this
    /// transform (or a branch of it) is the data transform of `axes`.
    /// Artists use it to decide whether their data feeds autoscale.
    [[nodiscard]] virtual bool containsDataBranch(const Axes&) const {
        return false;
    }
    /// Blended transforms expose their per-axis branches (mpl
    /// BlendedGenericTransform.x_transform / y_transform). nullptr for
    /// non-blended transforms.
    [[nodiscard]] virtual const Transform* xBranch() const {
        return nullptr;
    }
    [[nodiscard]] virtual const Transform* yBranch() const {
        return nullptr;
    }

    [[nodiscard]] Point2D operator()(Point2D p) const { return apply(p); }
    /// Batch-transform a list of points.
    [[nodiscard]] std::vector<Point2D> apply(
        std::span<const Point2D> pts) const;
    /// Compose: `then(b)` applies `b` after `*this` (mpl `a + b`).
    /// Returns a CompositeAffine2D when both sides are affine.
    [[nodiscard]] std::shared_ptr<Transform> then(
        std::shared_ptr<Transform> after) const;
};

/// 2D affine transform (matplotlib `Affine2D`).
/// Matrix layout follows matplotlib: [[a c e], [b d f], [0 0 1]].
class Affine2D : public Transform {
public:
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // NOLINT — mpl field names

    Affine2D() = default; // identity
    Affine2D(float a_, float b_, float c_, float d_, float e_, float f_)
        : a(a_), b(b_), c(c_), d(d_), e(e_), f(f_) {}

    static Affine2D identity() { return {}; }
    static Affine2D scale(float s) { return scale(s, s); }
    static Affine2D scale(float sx, float sy) { return {sx, 0, 0, sy, 0, 0}; }
    static Affine2D translate(float tx, float ty) { return {1, 0, 0, 1, tx, ty}; }
    static Affine2D rotate(float rad);
    static Affine2D rotateDeg(float deg) { return rotate(deg * 0.017453292519943295f); }
    static Affine2D rotateAround(float x, float y, float deg) {
        return translate(x, y).concat(rotateDeg(deg)).concat(translate(-x, -y));
    }
    static Affine2D skew(float xShear, float yShear);   // radians
    static Affine2D skewDeg(float xDeg, float yDeg);

    /// `concat(o)` = `*this` applied after `o` (mpl `self.dot(o)`).
    [[nodiscard]] Affine2D concat(const Affine2D& o) const noexcept;

    using Transform::apply; // keep the batch overload visible
    [[nodiscard]] Point2D apply(Point2D p) const override {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
    [[nodiscard]] bool isAffine() const noexcept override { return true; }
    [[nodiscard]] TransformPtr inverted() const override;
    [[nodiscard]] TransformPtr clone() const override {
        return std::make_shared<Affine2D>(*this);
    }
};

/// Identity transform — matplotlib `transDisplay` (input already in pixels).
class IdentityTransform final : public Transform {
public:
    [[nodiscard]] Point2D apply(Point2D p) const override { return p; }
    [[nodiscard]] bool isAffine() const noexcept override { return true; }
    [[nodiscard]] TransformPtr inverted() const override;
    [[nodiscard]] TransformPtr clone() const override {
        return std::make_shared<IdentityTransform>();
    }
};

/// The shared `transDisplay` instance.
[[nodiscard]] TransformPtr transDisplay();

/// CompositeGenericTransform — `first` then `second`.
class CompositeGenericTransform : public Transform {
public:
    CompositeGenericTransform(TransformPtr first, TransformPtr second)
        : first_(std::move(first)), second_(std::move(second)) {}
    [[nodiscard]] Point2D apply(Point2D p) const override {
        return second_->apply(first_->apply(p));
    }
    [[nodiscard]] bool isAffine() const noexcept override {
        return first_->isAffine() && second_->isAffine();
    }
    [[nodiscard]] TransformPtr inverted() const override;
    [[nodiscard]] TransformPtr clone() const override {
        return std::make_shared<CompositeGenericTransform>(*this);
    }
    [[nodiscard]] bool containsDataBranch(const Axes& axes) const override {
        return first_->containsDataBranch(axes) ||
               second_->containsDataBranch(axes);
    }
    [[nodiscard]] const Transform& first() const { return *first_; }
    [[nodiscard]] const Transform& second() const { return *second_; }

private:
    TransformPtr first_, second_;
};

/// CompositeAffine2D — the affine-collapsed composite (still an Affine2D).
class CompositeAffine2D final : public Affine2D {
public:
    CompositeAffine2D(const Affine2D& first, const Affine2D& second)
        : Affine2D(second.concat(first)) {}
};

/// BlendedGenericTransform — x from `xTransform`, y from `yTransform`
/// (matplotlib BlendedGenericTransform, e.g. blended transData+transAxes).
class BlendedGenericTransform : public Transform {
public:
    BlendedGenericTransform(TransformPtr x, TransformPtr y)
        : x_(std::move(x)), y_(std::move(y)) {}
    [[nodiscard]] Point2D apply(Point2D p) const override {
        return {x_->apply(p).x, y_->apply(p).y};
    }
    [[nodiscard]] bool isAffine() const noexcept override {
        return x_->isAffine() && y_->isAffine();
    }
    [[nodiscard]] TransformPtr inverted() const override;
    [[nodiscard]] TransformPtr clone() const override {
        return std::make_shared<BlendedGenericTransform>(*this);
    }
    [[nodiscard]] bool containsDataBranch(const Axes& axes) const override {
        return x_->containsDataBranch(axes) ||
               y_->containsDataBranch(axes);
    }
    [[nodiscard]] const Transform* xBranch() const override {
        return x_.get();
    }
    [[nodiscard]] const Transform* yBranch() const override {
        return y_.get();
    }
    [[nodiscard]] const Transform& xTransform() const { return *x_; }
    [[nodiscard]] const Transform& yTransform() const { return *y_; }

private:
    TransformPtr x_, y_;
};

/// BlendedAffine2D — blend of two affines; itself affine.
class BlendedAffine2D final : public Affine2D {
public:
    BlendedAffine2D(const Affine2D& x, const Affine2D& y)
        : Affine2D(x.a, x.b, x.c, y.d, x.e, y.f) {}
};

/// mpl `blended_transform_factory`: affine blend when both inputs are
/// affine, generic blend otherwise.
[[nodiscard]] TransformPtr blendedTransformFactory(TransformPtr x,
                                                   TransformPtr y);

/// mpl `ScaledTranslation` — a *pure translation* by (xt, yt) after the
/// offset itself has been mapped through `scale_trans` (e.g. the
/// dpi-scale transform for point/inch offsets).
///   out = p + scale_trans((xt, yt))
class ScaledTranslation final : public Affine2D {
public:
    ScaledTranslation(float xt, float yt, const Transform& scaleT) {
        const Point2D t = scaleT.apply({xt, yt});
        a = 1; b = 0; c = 0; d = 1; e = t.x; f = t.y;
    }
};

/// mpl `AffineDeltaTransform` — transforms *displacements* between
/// points: the child's linear part with a zeroed offset column.
///   apply(p) = base(p) - base(0)
class AffineDeltaTransform final : public Transform {
public:
    explicit AffineDeltaTransform(TransformPtr base)
        : base_(std::move(base)) {}
    [[nodiscard]] Point2D apply(Point2D p) const override {
        const Point2D q = base_->apply(p), o = base_->apply({0, 0});
        return {q.x - o.x, q.y - o.y};
    }
    [[nodiscard]] bool isAffine() const noexcept override {
        return base_->isAffine();
    }
    /// The probed linear part (exact when the base is affine).
    [[nodiscard]] Affine2D deltaMatrix() const {
        const Point2D o = base_->apply({0, 0}),
                      x = base_->apply({1, 0}),
                      y = base_->apply({0, 1});
        return Affine2D{x.x - o.x, x.y - o.y,
                        y.x - o.x, y.y - o.y, 0, 0};
    }
    [[nodiscard]] TransformPtr inverted() const override {
        if (!isAffine()) return nullptr;
        return deltaMatrix().inverted();
    }
    [[nodiscard]] TransformPtr clone() const override {
        return std::make_shared<AffineDeltaTransform>(base_->clone());
    }
    [[nodiscard]] const Transform& base() const noexcept { return *base_; }

private:
    TransformPtr base_;
};

/// mpl `BboxTransform` — affine map from `boxin` bounds (x0,y0,w0,h0)
/// to `boxout` bounds. Snapshot semantics (bounds are read once).
class BboxTransform : public Affine2D {
public:
    BboxTransform(float inX, float inY, float inW, float inH,
                  float outX, float outY, float outW, float outH) {
        const float sx = outW / inW, sy = outH / inH;
        a = sx; b = 0; c = 0; d = sy;
        e = outX - inX * sx;
        f = outY - inY * sy;
    }
};

/// mpl `BboxTransformTo` — unit box → boxout.
class BboxTransformTo final : public BboxTransform {
public:
    explicit BboxTransformTo(float outX, float outY,
                             float outW, float outH)
        : BboxTransform(0, 0, 1, 1, outX, outY, outW, outH) {}
};

/// mpl `BboxTransformFrom` — boxin → unit box.
class BboxTransformFrom final : public BboxTransform {
public:
    explicit BboxTransformFrom(float inX, float inY,
                               float inW, float inH)
        : BboxTransform(inX, inY, inW, inH, 0, 0, 1, 1) {}
};

/// mpl `composite_transform_factory`: identity passthrough, affine
/// collapse when both sides are affine, generic composite otherwise.
[[nodiscard]] TransformPtr compositeTransformFactory(
    TransformPtr a, TransformPtr b);

/// mpl `offset_copy`: transform offset by (dx, dy) in `units`
/// ("points", "pixels"/"dots", "inches") at `dpi`.
[[nodiscard]] TransformPtr offsetCopy(const Transform& base, float dpi,
                                      float dx, float dy,
                                      std::string_view units = "points");

/// transAxes — axes fraction (0..1) → display pixels. Reads `axes.rect`
/// live, so it tracks layout.
[[nodiscard]] TransformPtr transAxes(const Axes& axes);
/// transFigure — figure fraction (0..1) → display pixels. Reads the
/// figure's layout rect live.
[[nodiscard]] TransformPtr transFigure(const Figure& fig);
/// transData — data coords → display pixels through the axes' scales,
/// projection, viewport, and rect.
[[nodiscard]] TransformPtr transData(const Axes& axes);

/// Maps data coordinates to normalized device coordinates (NDC, [-1,1]).
/// Used by the vertex shader push-constants/uniform.
///
/// The `view` is expressed in *display space*: Axes::transform() applies
/// the axis scales to the data limits before filling it in, so shaders
/// only need to apply scale codes to vertex positions. `projection`
/// transforms (x,y) jointly (e.g. polar) — for non-rectilinear
/// projections the view is the projected-plane bounds.
struct Transform2D {
    Viewport view;
    /// If true, apply log10 to the respective axis before mapping.
    /// Kept for back-compat; equivalent to scaleX/scaleY == Log.
    bool logX = false;
    bool logY = false;
    /// Per-axis scales (shader codes + params).
    AxisScale scaleX, scaleY;
    /// Joint (x,y) projection applied after the per-axis scales.
    Projection projection;

    [[nodiscard]] Point2D toNdc(Point2D p) const noexcept;
    [[nodiscard]] Point2D fromNdc(Point2D p) const noexcept;

    /// Effective scale code for axis (logX/logY bools fold into Log).
    [[nodiscard]] ScaleKind codeX() const noexcept {
        return scaleX.kind != ScaleKind::Linear || !logX
                   ? scaleX.kind : ScaleKind::Log;
    }
    [[nodiscard]] ScaleKind codeY() const noexcept {
        return scaleY.kind != ScaleKind::Linear || !logY
                   ? scaleY.kind : ScaleKind::Log;
    }
    [[nodiscard]] float scaleFwdX(float v) const noexcept {
        return codeX() == ScaleKind::Log ? AxisScale::log().forward(v)
                                         : scaleX.forward(v);
    }
    [[nodiscard]] float scaleFwdY(float v) const noexcept {
        return codeY() == ScaleKind::Log ? AxisScale::log().forward(v)
                                         : scaleY.forward(v);
    }
};

/// Camera for 3D plots (orbit + perspective).
struct Camera3D {
    Point3D eye{2,2,2};
    Point3D target{0,0,0};
    Point3D up{0,0,1};
    float fov = 45.0f;       // degrees
    float aspect = 1.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;
    /// matplotlib 3D box normalization: when dataMin < dataMax, data is
    /// remapped into mpl's (4,4,3) box centered at the origin before the
    /// view transform. Disabled when dataMin == dataMax.
    Point3D dataMin{0, 0, 0}, dataMax{0, 0, 0};
    /// Spherical view angles in degrees — set by viewInit, NaN when the
    /// camera was built from explicit eye/target. Interactive rotation
    /// reads/updates these.
    float elevDeg = NAN, azimDeg = NAN, rollDeg = 0.0f;

    /// Compute view matrix (row-major 4x4).
    [[nodiscard]] std::array<float, 16> viewMatrix() const noexcept;
    /// Compute projection matrix (row-major 4x4).
    [[nodiscard]] std::array<float, 16> projectionMatrix() const noexcept;
    /// Combined view-projection.
    [[nodiscard]] std::array<float, 16> viewProjection() const noexcept;

    /// mpl Axes3D.view_init: build a camera from elevation/azimuth/roll
    /// angles (degrees, mpl defaults 30/-60/0) orbiting `target` at
    /// distance `dist` (mpl default 10). Roll rotates the image plane.
    /// Degenerate |elev|==90 falls back to a horizontal up vector.
    [[nodiscard]] static Camera3D viewInit(float elevDeg, float azimDeg,
                                           float rollDeg = 0.0f,
                                           Point3D target = {0, 0, 0},
                                           float dist = 10.0f) noexcept;
};

} // namespace volcano::plot
