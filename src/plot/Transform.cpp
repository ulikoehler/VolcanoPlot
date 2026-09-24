// volcano/plot/Transform.cpp
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Plot.hpp"

#include <cmath>

namespace volcano::plot {

// ─── Transform base ───────────────────────────────────────────────────────

std::vector<Point2D> Transform::apply(std::span<const Point2D> pts) const {
    std::vector<Point2D> out;
    out.reserve(pts.size());
    for (auto p : pts) out.push_back(apply(p));
    return out;
}

TransformPtr Transform::then(TransformPtr after) const {
    if (isAffine() && after->isAffine()) {
        if (auto* a = dynamic_cast<const Affine2D*>(this)) {
            if (auto* b = dynamic_cast<Affine2D*>(after.get()))
                return std::make_shared<CompositeAffine2D>(*a, *b);
        }
    }
    return std::make_shared<CompositeGenericTransform>(clone(), after);
}

// ─── Affine2D ─────────────────────────────────────────────────────────────

Affine2D Affine2D::rotate(float rad) {
    float s = std::sin(rad), c = std::cos(rad);
    return {c, s, -s, c, 0, 0};
}

Affine2D Affine2D::skew(float xShear, float yShear) {
    return {1, std::tan(yShear), std::tan(xShear), 1, 0, 0};
}

Affine2D Affine2D::skewDeg(float xDeg, float yDeg) {
    return skew(xDeg * 0.017453292519943295f, yDeg * 0.017453292519943295f);
}

Affine2D Affine2D::concat(const Affine2D& o) const noexcept {
    // (*this) ∘ o : apply o first, then *this.
    return {a * o.a + c * o.b,  b * o.a + d * o.b,
            a * o.c + c * o.d,  b * o.c + d * o.d,
            a * o.e + c * o.f + e, b * o.e + d * o.f + f};
}

TransformPtr Affine2D::inverted() const {
    float det = a * d - b * c;
    if (std::abs(det) < 1e-12f) return nullptr;
    float inv = 1.0f / det;
    return std::make_shared<Affine2D>(
        d * inv, -b * inv, -c * inv, a * inv,
        (c * f - d * e) * inv, (b * e - a * f) * inv);
}

// ─── Identity / composite / blended ───────────────────────────────────────

TransformPtr IdentityTransform::inverted() const {
    return std::make_shared<IdentityTransform>();
}

TransformPtr transDisplay() {
    static TransformPtr inst = std::make_shared<IdentityTransform>();
    return inst;
}

TransformPtr CompositeGenericTransform::inverted() const {
    auto a = first_->inverted(), b = second_->inverted();
    if (!a || !b) return nullptr;
    return b->then(a);
}

TransformPtr BlendedGenericTransform::inverted() const {
    auto a = x_->inverted(), b = y_->inverted();
    if (!a || !b) return nullptr;
    return blendedTransformFactory(a, b);
}

TransformPtr blendedTransformFactory(TransformPtr x, TransformPtr y) {
    if (x->isAffine() && y->isAffine())
        if (auto* xa = dynamic_cast<Affine2D*>(x.get()))
            if (auto* ya = dynamic_cast<Affine2D*>(y.get()))
                return std::make_shared<BlendedAffine2D>(*xa, *ya);
    return std::make_shared<BlendedGenericTransform>(std::move(x), std::move(y));
}

TransformPtr compositeTransformFactory(TransformPtr a, TransformPtr b) {
    // mpl: IdentityTransform on either side returns the other transform.
    if (dynamic_cast<IdentityTransform*>(a.get())) return b;
    if (dynamic_cast<IdentityTransform*>(b.get())) return a;
    if (auto* aa = dynamic_cast<Affine2D*>(a.get()))
        if (auto* ba = dynamic_cast<Affine2D*>(b.get()))
            return std::make_shared<CompositeAffine2D>(*aa, *ba);
    return std::make_shared<CompositeGenericTransform>(std::move(a),
                                                       std::move(b));
}

TransformPtr offsetCopy(const Transform& base, float dpi, float dx, float dy,
                        std::string_view units) {
    float scale = dpi / 72.0f; // points
    if (units == "pixels" || units == "dots") scale = 1.0f;
    else if (units == "inches") scale = dpi;
    auto off = std::make_shared<ScaledTranslation>(dx, dy,
                                                   Affine2D::scale(scale));
    return base.clone()->then(off);
}

// ─── Bound transforms ─────────────────────────────────────────────────────

namespace {

/// transAxes: axes fraction → display px (Y-up input, Y-down output).
class AxesToPixels final : public Transform {
public:
    explicit AxesToPixels(const Axes& a) : axes_(&a) {}
    Point2D apply(Point2D p) const override {
        const Rect2D& r = axes_->rect;
        return {r.x + p.x * float(r.width),
                r.y + (1.0f - p.y) * float(r.height)};
    }
    bool isAffine() const noexcept override { return true; }
    TransformPtr inverted() const override;
    TransformPtr clone() const override {
        return std::make_shared<AxesToPixels>(*this);
    }
private:
    const Axes* axes_;
};

class PixelsToAxes final : public Transform {
public:
    explicit PixelsToAxes(const Axes& a) : axes_(&a) {}
    Point2D apply(Point2D p) const override {
        const Rect2D& r = axes_->rect;
        float w = r.width ? float(r.width) : 1.0f;
        float h = r.height ? float(r.height) : 1.0f;
        return {(p.x - r.x) / w, 1.0f - (p.y - r.y) / h};
    }
    bool isAffine() const noexcept override { return true; }
    TransformPtr inverted() const override {
        return std::make_shared<AxesToPixels>(*axes_);
    }
    TransformPtr clone() const override {
        return std::make_shared<PixelsToAxes>(*this);
    }
private:
    const Axes* axes_;
};

TransformPtr AxesToPixels::inverted() const {
    return std::make_shared<PixelsToAxes>(*axes_);
}

/// transFigure: figure fraction → display px.
class FigureToPixels final : public Transform {
public:
    explicit FigureToPixels(const Figure& f) : fig_(&f) {}
    Point2D apply(Point2D p) const override {
        const Rect2D& r = fig_->figRect();
        return {r.x + p.x * float(r.width),
                r.y + (1.0f - p.y) * float(r.height)};
    }
    bool isAffine() const noexcept override { return true; }
    TransformPtr inverted() const override;
    TransformPtr clone() const override {
        return std::make_shared<FigureToPixels>(*this);
    }
private:
    const Figure* fig_;
};

class PixelsToFigure final : public Transform {
public:
    explicit PixelsToFigure(const Figure& f) : fig_(&f) {}
    Point2D apply(Point2D p) const override {
        const Rect2D& r = fig_->figRect();
        float w = r.width ? float(r.width) : 1.0f;
        float h = r.height ? float(r.height) : 1.0f;
        return {(p.x - r.x) / w, 1.0f - (p.y - r.y) / h};
    }
    bool isAffine() const noexcept override { return true; }
    TransformPtr inverted() const override {
        return std::make_shared<FigureToPixels>(*fig_);
    }
    TransformPtr clone() const override {
        return std::make_shared<PixelsToFigure>(*this);
    }
private:
    const Figure* fig_;
};

TransformPtr FigureToPixels::inverted() const {
    return std::make_shared<PixelsToFigure>(*fig_);
}

/// transData: data coords → display px (scales + projection + viewport).
class DataToPixels final : public Transform {
public:
    explicit DataToPixels(const Axes& a) : axes_(&a) {}
    Point2D apply(Point2D p) const override {
        const Rect2D& r = axes_->rect;
        Point2D fr = axes_->dataToFraction(p);
        return {r.x + fr.x * float(r.width),
                r.y + (1.0f - fr.y) * float(r.height)};
    }
    bool containsDataBranch(const Axes& a) const override {
        return &a == axes_;
    }
    TransformPtr inverted() const override;
    TransformPtr clone() const override {
        return std::make_shared<DataToPixels>(*this);
    }
private:
    const Axes* axes_;
};

class PixelsToData final : public Transform {
public:
    explicit PixelsToData(const Axes& a) : axes_(&a) {}
    Point2D apply(Point2D p) const override {
        const Rect2D& r = axes_->rect;
        float w = r.width ? float(r.width) : 1.0f;
        float h = r.height ? float(r.height) : 1.0f;
        return axes_->fractionToData(
            {(p.x - r.x) / w, 1.0f - (p.y - r.y) / h});
    }
    TransformPtr inverted() const override {
        return std::make_shared<DataToPixels>(*axes_);
    }
    TransformPtr clone() const override {
        return std::make_shared<PixelsToData>(*this);
    }
private:
    const Axes* axes_;
};

TransformPtr DataToPixels::inverted() const {
    return std::make_shared<PixelsToData>(*axes_);
}

} // namespace

TransformPtr transAxes(const Axes& axes) {
    return std::make_shared<AxesToPixels>(axes);
}
TransformPtr transFigure(const Figure& fig) {
    return std::make_shared<FigureToPixels>(fig);
}
TransformPtr transData(const Axes& axes) {
    return std::make_shared<DataToPixels>(axes);
}

Point2D Transform2D::toNdc(Point2D p) const noexcept {
    Point2D d = projection.forward({scaleFwdX(p.x), scaleFwdY(p.y)});
    float nx = (d.x - view.x.min) / (view.x.span() != 0 ? view.x.span() : 1);
    float ny = (d.y - view.y.min) / (view.y.span() != 0 ? view.y.span() : 1);
    return { nx * 2.0f - 1.0f, ny * 2.0f - 1.0f };
}

Point2D Transform2D::fromNdc(Point2D p) const noexcept {
    float nx = (p.x + 1.0f) * 0.5f;
    float ny = (p.y + 1.0f) * 0.5f;
    Point2D d{ view.x.min + nx * view.x.span(),
               view.y.min + ny * view.y.span() };
    Point2D raw = projection.inverse(d);
    return { scaleX.inverse(raw.x), scaleY.inverse(raw.y) };
}

namespace {

void normalize(Point3D& v) {
    float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len > 0) { v.x /= len; v.y /= len; v.z /= len; }
}

Point3D cross(const Point3D& a, const Point3D& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

} // namespace

std::array<float, 16> Camera3D::viewMatrix() const noexcept {
    // eye/target are in box space when dataMin < dataMax (mpl 3D: the
    // (4,4,3)-aspect box is centered at the origin).
    const Point3D eye_ = eye, target_ = target;
    Point3D f = { target_.x - eye_.x, target_.y - eye_.y, target_.z - eye_.z };
    normalize(f);
    Point3D s = cross(f, up); normalize(s);
    Point3D u = cross(s, f);
    // Row-major view matrix in box space.
    std::array<float, 16> v = {
        s.x, s.y, s.z, -(s.x*eye_.x + s.y*eye_.y + s.z*eye_.z),
        u.x, u.y, u.z, -(u.x*eye_.x + u.y*eye_.y + u.z*eye_.z),
        -f.x, -f.y, -f.z, (f.x*eye_.x + f.y*eye_.y + f.z*eye_.z),
        0,0,0,1
    };
    // Right-multiply by the data→box transform M (scale + translate) so
    // incoming data coordinates land in the mpl box.
    auto scale = [](float lo, float hi, float box) {
        return lo < hi ? box / (hi - lo) : 1.0f;
    };
    float sc[3] = { scale(dataMin.x, dataMax.x, 4.0f),
                    scale(dataMin.y, dataMax.y, 4.0f),
                    scale(dataMin.z, dataMax.z, 3.0f) };
    float tc[3] = { -(dataMin.x + dataMax.x) * 0.5f * sc[0],
                    -(dataMin.y + dataMax.y) * 0.5f * sc[1],
                    -(dataMin.z + dataMax.z) * 0.5f * sc[2] };
    std::array<float, 16> out{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) out[i*4+j] = v[i*4+j] * sc[j];
        out[i*4+3] = v[i*4+0]*tc[0] + v[i*4+1]*tc[1] +
                     v[i*4+2]*tc[2] + v[i*4+3];
    }
    return out;
}

std::array<float, 16> Camera3D::projectionMatrix() const noexcept {
    float fovRad = fov * 3.14159265358979f / 180.0f;
    float f = 1.0f / std::tan(fovRad * 0.5f);
    // Vulkan NDC has +y down; negate so world up renders upward.
    return {
        f / aspect, 0, 0, 0,
        0, -f, 0, 0,
        0, 0, -farZ / (farZ - nearZ), -(nearZ * farZ) / (farZ - nearZ),
        0, 0, -1, 0
    };
}

std::array<float, 16> Camera3D::viewProjection() const noexcept {
    auto v = viewMatrix();
    auto p = projectionMatrix();
    std::array<float, 16> out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += p[i*4+k] * v[k*4+j];
            out[i*4+j] = s;
        }
    return out;
}

Camera3D Camera3D::viewInit(float elevDeg, float azimDeg, float rollDeg,
                            Point3D target, float dist) noexcept {
    constexpr float kDeg = float(M_PI) / 180.0f;
    float e = elevDeg * kDeg, a = azimDeg * kDeg, r = rollDeg * kDeg;
    // mpl axes3d: eye = R + dist * [cos e·cos a, cos e·sin a, sin e].
    Point3D w{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a),
              std::sin(e)};                      // center → eye
    // Vertical axis flips when looking from below (mpl _calc_view_axes).
    Point3D V{0, 0, std::fabs(e) > float(M_PI_2) ? -1.0f : 1.0f};
    Point3D u = cross(V, w);                     // screen-right
    if (u.x*u.x + u.y*u.y + u.z*u.z < 1e-12f)
        u = {1, 0, 0};                           // looking straight down/up
    normalize(u);
    Point3D v = cross(w, u);                     // screen-up
    if (r != 0.0f) {
        // mpl proj3d._view_axes: rotate u,v by -roll about w.
        float cr = std::cos(r), sr = std::sin(r);
        Point3D u2{u.x*cr - v.x*sr, u.y*cr - v.y*sr, u.z*cr - v.z*sr};
        v = {v.x*cr + u.x*sr, v.y*cr + u.y*sr, v.z*cr + u.z*sr};
        u = u2;
    }
    Camera3D cam;
    cam.eye = {target.x + dist * w.x, target.y + dist * w.y,
               target.z + dist * w.z};
    cam.target = target;
    cam.up = v;
    cam.elevDeg = elevDeg;
    cam.azimDeg = azimDeg;
    cam.rollDeg = rollDeg;
    return cam;
}

} // namespace volcano::plot
