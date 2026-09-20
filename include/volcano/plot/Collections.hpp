// volcano/plot/Collections.hpp — Path/Patch primitives and collections (§14)
#pragma once

#include "volcano/plot/Path.hpp"
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/plot/Transform.hpp"

#include <optional>
#include <string>
#include <vector>

namespace volcano::plot {

/// Style shared by patches and collections.
struct PatchStyle {
    /// Face color (a==0 → unfilled, mpl "none").
    Color face{0.121f, 0.466f, 0.705f, 1.0f}; // mpl 'C0' blue
    /// Edge color (a==0 → no stroke).
    Color edge{0.0f, 0.0f, 0.0f, 1.0f};
    float lineWidth = 1.0f;
    LineStyle lineStyle = LineStyle::Solid;
    std::vector<float> dashes;      ///< explicit dash tuple (overrides style)
    float dashOffset = 0.0f;
    /// mpl hatch string: '/', '\\', '|', '-', '+', 'x' (repeats = denser).
    std::string hatch;
    float hatchSpacing = 24.0f;     ///< base spacing in px (mpl '/' ≈ 24)
    /// Optional extra transform applied to the path before data mapping.
    TransformPtr transform;
};

/// A Patch = a Path in data coords plus style (matplotlib `Patch`).
struct Patch {
    Path path;
    PatchStyle style;
};

/// mpl patch primitives — build `Patch`es in data coords.
namespace patch {

[[nodiscard]] Patch Rectangle(float x, float y, float w, float h);
[[nodiscard]] Patch Circle(Point2D center, float radius);
[[nodiscard]] Patch Ellipse(Point2D center, float width, float height,
                            float angleDeg = 0.0f);
[[nodiscard]] Patch Polygon(std::vector<Point2D> points, bool closed = true);
[[nodiscard]] Patch Wedge(Point2D center, float r, float theta1Deg,
                          float theta2Deg);
/// Rounded-box patch: (x, y, w, h) with corner rounding `pad` (data units).
[[nodiscard]] Patch FancyBboxPatch(float x, float y, float w, float h,
                                   float pad = 0.1f);
/// mpl `FancyBboxPatch` with a full `boxstyle` spec (pad etc. ×
/// `spec.mutationSize`, in data units).
[[nodiscard]] Patch FancyBboxPatch(float x, float y, float w, float h,
                                   const BoxStyleSpec& spec);
/// Simple arrow patch: shaft from `a` to `b` with a triangular head.
[[nodiscard]] Patch FancyArrowPatch(Point2D a, Point2D b,
                                    float headWidth = 0.05f,
                                    float headLen = 0.1f);
[[nodiscard]] Patch PathPatch(Path path);

} // namespace patch

// ═══ Collections ══════════════════════════════════════════════════════════

/// Base for pixel-tessellated collections. `offsets`/`offsetTransform`
/// implement mpl's instanced-rendering semantics: each item's geometry is
/// drawn translated by every offset (offsets are in data coords by default;
/// when `offsetTransform` is set it maps offsets → display pixels).
class Collection : public IPlot {
public:
    std::vector<Color> faceColors;
    std::vector<Color> edgeColors;
    std::vector<float> lineWidths;
    LineStyle lineStyle = LineStyle::Solid;
    std::vector<float> dashes;
    std::string hatch;
    float hatchSpacing = 24.0f;
    std::vector<Point2D> offsets;       ///< instancing offsets
    TransformPtr offsetTransform;       ///< mpl offset_transform
    /// mpl set_clip_path: optional clip path in data coords. Fill, edge,
    /// hatch and line geometry is clipped to its outline (single outer
    /// ring; flattened) at draw time.
    std::optional<Path> clipPath;
    std::string label_;

    void prepare(render::Renderer&) override {} // CPU tessellation per frame
    [[nodiscard]] bool canEmitVector() const override { return true; }
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override {
        return !faceColors.empty() && faceColors[0].a > 0 ? faceColors[0]
               : !edgeColors.empty() ? edgeColors[0] : Color::black();
    }
    bool applyCycleProps(const CycleProps& p) override {
        if (p.color) { faceColors = {*p.color}; edgeColors = {*p.color}; }
        if (p.lineWidth) lineWidths = {*p.lineWidth};
        if (p.lineStyle) lineStyle = *p.lineStyle;
        return p.color || p.lineWidth || p.lineStyle;
    }

protected:
    /// data point → canvas pixel.
    static Point2D toPx(const Axes& axes, Rect2D rect, Point2D p);
    /// Resolve an offset to display px (offsetTransform or data coords).
    static Point2D offsetPx(const Axes& axes, Rect2D rect,
                            const Transform* offT, Point2D off,
                            Point2D itemOriginPx);
    /// Fill+edge+hatch one pixel-space polygon soup into the draw lists.
    /// `sketchScale` > 0 applies xkcd-style wobble to stroked edges.
    /// `clipRing` (pixel space) additionally clips all emitted geometry.
    static void drawSubpaths(vk::CommandBuffer cmd,
                             render::Renderer& r,
                             vk::Rect2D clip, vk::Extent2D res,
                             const std::vector<Path::Subpath>& subs,
                             Color face, Color edge, float lw,
                             std::span<const float> dash,
                             const std::string& hatch, float hatchSpacing,
                             float sketchScale = 0.0f,
                             std::span<const Point2D> clipRing = {});
    /// Vector-export counterpart of drawSubpaths: fill + edge + hatch.
    static void emitSubpaths(render::VectorCanvas& c,
                             const std::vector<Path::Subpath>& subs,
                             Color face, Color edge, float lw,
                             std::span<const float> dash,
                             const std::string& hatch, float hatchSpacing,
                             std::span<const Point2D> clipRing = {});

    /// Resolve `clipPath` to a pixel-space ring (first/largest subpath),
    /// or empty when unset.
    std::vector<Point2D> clipRingPx(const Axes& axes, Rect2D rect) const;
};

/// PatchCollection — a list of styled patches.
class PatchCollection : public Collection {
public:
    std::vector<Patch> patches;
    explicit PatchCollection(std::vector<Patch> p) : patches(std::move(p)) {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// Picking: hit when the data point is inside a patch.
    bool contains(const Axes&, Point2D pt) const override;
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
};

/// PathCollection — one path instanced at `offsets`, scaled per-item.
/// (mpl PathCollection / the machinery behind scatter.)
class PathCollection : public Collection {
public:
    Path path;                          ///< template path (unit-ish)
    std::vector<Point2D> sizes;         ///< per-item (sx, sy) scale, data units
    std::vector<TransformPtr> transforms; ///< optional per-item transform
    bool strokeOnly = false;            ///< asterisk-style: no fill

    PathCollection(Path p, std::vector<Point2D> offs)
        : path(std::move(p)) { offsets = std::move(offs); }
    /// Set uniform item size (data units).
    void setSize(float sx, float sy) { sizes = {{sx, sy}}; }

    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

    /// Call after mutating `path` to re-upload the GPU template.
    void markTemplateDirty() noexcept { templateDirty_ = true; }

private:
    /// True until the GPU instancing template reflects `path`.
    bool templateDirty_ = true;
};

/// LineCollection — independent line segments/curves with per-line style.
class LineCollection : public Collection {
public:
    std::vector<std::vector<Point2D>> segments;
    explicit LineCollection(std::vector<std::vector<Point2D>> segs)
        : segments(std::move(segs)) {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
};

/// PolyCollection — filled polygons with per-poly face/edge colors.
class PolyCollection : public Collection {
public:
    std::vector<std::vector<Point2D>> polys;
    explicit PolyCollection(std::vector<std::vector<Point2D>> p)
        : polys(std::move(p)) {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// Picking: hit when the data point is inside a polygon.
    bool contains(const Axes&, Point2D pt) const override;
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
};

/// QuadMesh — a (rows×cols) grid of colored quads defined by a
/// (rows+1)×(cols+1) corner lattice (mpl QuadMesh / pcolormesh).
class QuadMesh : public Collection {
public:
    uint32_t rows = 0, cols = 0;
    std::vector<Point2D> corners;       ///< (rows+1)*(cols+1) lattice
    std::vector<Color> cellColors;      ///< rows*cols

    QuadMesh(uint32_t rows, uint32_t cols, std::vector<Point2D> lattice,
             std::vector<Color> colors)
        : rows(rows), cols(cols), corners(std::move(lattice)),
          cellColors(std::move(colors)) {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
};

/// TriMeshCollection — filled triangles on a shared vertex list
/// (mpl TriMesh; named to avoid clash with Stroke.hpp's TriMesh).
class TriMeshCollection : public Collection {
public:
    std::vector<Point2D> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
    TriMeshCollection(std::vector<Point2D> verts,
                      std::vector<std::array<uint32_t, 3>> tris)
        : vertices(std::move(verts)), triangles(std::move(tris)) {}
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
};

/// CircleCollection — circles of per-item radius at `offsets` (mpl).
class CircleCollection : public PathCollection {
public:
    CircleCollection(std::vector<float> radii, std::vector<Point2D> offs);
};

/// RegularPolyCollection — n-gons of per-item radius at `offsets`.
class RegularPolyCollection : public PathCollection {
public:
    RegularPolyCollection(int n, std::vector<float> radii,
                          std::vector<Point2D> offs);
};

/// AsteriskPolygonCollection — n-armed asterisks (stroke-only).
class AsteriskPolygonCollection : public PathCollection {
public:
    AsteriskPolygonCollection(int n, std::vector<float> radii,
                              std::vector<Point2D> offs);
};

/// Generate hatch line segments (thin quads) clipped to a pixel-space
/// polygon. Pattern chars: / \ | - + x ; repeats increase density.
[[nodiscard]] std::vector<Point2D>
hatchTriangles(std::span<const Point2D> poly, std::string_view pattern,
               float spacing);

} // namespace volcano::plot
