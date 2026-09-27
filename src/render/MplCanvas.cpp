// volcano/render/MplCanvas.cpp — generic Vulkan canvas for the
// matplotlib backend bridge. See MplCanvas.hpp for the model.
#include "volcano/render/MplCanvas.hpp"

#include <volcano/backend/HeadlessBackend.hpp>
#ifdef VOLCANO_HAVE_SCREEN
#include <volcano/backend/ScreenBackend.hpp>
#endif
#include <volcano/plot/Collections.hpp>  // hatchTriangles
#include <volcano/plot/Stroke.hpp>
#include <volcano/text/MathText.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <variant>

namespace volcano::render {

namespace {

constexpr float kFontBasePx = 16.0f;  // glyph design size at scale=1

plot::Rect2D fullRect(vk::Extent2D e) {
    return {0, 0, e.width, e.height};
}
vk::Rect2D vkRect(plot::Rect2D r) {
    return vk::Rect2D{vk::Offset2D{r.x, r.y},
                      vk::Extent2D{r.width, r.height}};
}
vk::Rect2D clipVk(std::optional<plot::Rect2D> clip, vk::Extent2D e) {
    return vkRect(clip ? *clip : fullRect(e));
}

// Signed polygon area (+CCW in the canvas' Y-down space is visually CW —
// sign convention only matters relative to itself here).
float ringArea(std::span<const plot::Point2D> r) {
    if (r.size() < 3) return 0.0f;
    float a = 0;
    for (size_t i = 0, n = r.size(); i < n; ++i) {
        auto p = r[i], q = r[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

// Even-odd point-in-ring (ray cast +x).
bool insideRing(plot::Point2D p, std::span<const plot::Point2D> r) {
    bool odd = false;
    for (size_t i = 0, n = r.size(); n >= 3 && i < n; ++i) {
        auto a = r[i], b = r[(i + 1) % n];
        if ((a.y > p.y) == (b.y > p.y)) continue;
        float x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
        if (x > p.x) odd = !odd;
    }
    return odd;
}

/// Merge a hole ring into an outer ring by bridging (Eberly): ray +x
/// from the hole's rightmost vertex, splice at the nearest parent-edge
/// intersection (or the best reflex vertex inside the probe triangle).
/// Returns the merged ring, or empty on failure.
std::vector<plot::Point2D> bridgeHole(std::span<const plot::Point2D> outer,
                                      std::span<const plot::Point2D> hole) {
    if (outer.size() < 3 || hole.size() < 3) return {};
    // Hole vertex with maximum x.
    size_t hi = 0;
    for (size_t i = 1; i < hole.size(); ++i)
        if (hole[i].x > hole[hi].x) hi = i;
    auto h = hole[hi];
    // Ray +x from h: nearest edge intersection on the outer ring.
    float bestT = std::numeric_limits<float>::max();
    size_t bestEdge = SIZE_MAX;
    plot::Point2D hit{0, 0};
    for (size_t i = 0, n = outer.size(); i < n; ++i) {
        auto a = outer[i], b = outer[(i + 1) % n];
        if ((a.y > h.y) == (b.y > h.y)) continue;
        if (b.y == a.y) continue;
        float t = (h.y - a.y) / (b.y - a.y);
        float x = a.x + t * (b.x - a.x);
        if (x <= h.x || x - h.x >= bestT) continue;
        bestT = x - h.x; bestEdge = i; hit = {x, h.y};
    }
    if (bestEdge == SIZE_MAX) return {};
    auto a = outer[bestEdge], b = outer[(bestEdge + 1) % outer.size()];
    // Candidate parent vertex: nearest endpoint of the hit edge, then
    // prefer any reflex vertex inside triangle (h, hit, endpoint).
    auto cand = [&](plot::Point2D p) {
        float dx = p.x - h.x, dy = p.y - h.y;
        return dx * dx + dy * dy;
    };
    size_t oi = cand(a) <= cand(b) ? bestEdge : (bestEdge + 1) % outer.size();
    // Check for outer vertices strictly inside triangle (h, hit, o).
    auto inTri = [&](plot::Point2D p) {
        auto sign = [](plot::Point2D p1, plot::Point2D p2, plot::Point2D p3) {
            return (p1.x - p3.x) * (p2.y - p3.y) -
                   (p2.x - p3.x) * (p1.y - p3.y);
        };
        auto o = outer[oi];
        float d1 = sign(p, h, hit), d2 = sign(p, hit, o), d3 = sign(p, o, h);
        bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(neg && pos);
    };
    for (size_t i = 0, n = outer.size(); i < n; ++i) {
        if (i == bestEdge || i == (bestEdge + 1) % n) continue;
        auto p = outer[i];
        if (p.x < h.x) continue;
        if (inTri(p) && cand(p) < cand(outer[oi])) oi = i;
    }
    // Splice: outer rotated at oi, then hole rotated at hi (reversed to
    // keep the merged ring's winding consistent with the outer ring).
    std::vector<plot::Point2D> out;
    out.reserve(outer.size() + hole.size() + 2);
    size_t n = outer.size();
    for (size_t k = 0; k <= n; ++k) out.push_back(outer[(oi + k) % n]);
    for (size_t k = 0; k <= hole.size(); ++k)
        out.push_back(hole[(hi + hole.size() - k) % hole.size()]);
    out.push_back(outer[oi]);
    return out;
}

} // namespace

// ═══ construction / teardown ═══════════════════════════════════════

MplCanvas::MplCanvas(std::unique_ptr<backend::IBackend> backend,
                     bool windowed)
    : backend_(std::move(backend)), windowed_(windowed) {}

std::unique_ptr<MplCanvas>
MplCanvas::headless(uint32_t width, uint32_t height,
                    vk::SampleCountFlagBits samples) {
    backend::BackendDesc d{};
    d.width = width; d.height = height; d.samples = samples;
    d.windowTitle = "volcanoplot-mpl";
    auto b = std::make_unique<backend::HeadlessBackend>(
        d, backend::sharedGpuContext());
    auto c = std::unique_ptr<MplCanvas>(new MplCanvas(std::move(b), false));
    c->initRenderers();
    return c;
}

std::unique_ptr<MplCanvas>
MplCanvas::windowed(uint32_t width, uint32_t height, std::string title,
                    vk::SampleCountFlagBits samples) {
#ifdef VOLCANO_HAVE_SCREEN
    try {
        backend::BackendDesc d{};
        d.width = width; d.height = height; d.samples = samples;
        d.windowTitle = std::move(title);
        auto b = std::make_unique<backend::ScreenBackend>(d);
        auto c = std::unique_ptr<MplCanvas>(
            new MplCanvas(std::move(b), true));
        c->initRenderers();
        return c;
    } catch (const std::exception&) {
        return nullptr;
    }
#else
    (void)width; (void)height; (void)title; (void)samples;
    return nullptr;
#endif
}

void MplCanvas::initRenderers() {
    auto& ctx = backend_->context();
    pipelineCache_ = std::make_unique<core::PipelineCache>(
        ctx.device.handle());
    std::vector<vk::DescriptorPoolSize> sizes = {
        {vk::DescriptorType::eUniformBuffer, 128},
        {vk::DescriptorType::eStorageBuffer, 128},
        {vk::DescriptorType::eCombinedImageSampler, 64},
    };
    descPool_ = std::make_unique<core::DescriptorPool>(
        ctx.device.handle(), sizes, 256,
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    spine_.init(ctx.device.handle(), ctx.allocator.handle(),
                backend_->renderPass(), backend_->sampleCount(),
                *pipelineCache_, *descPool_);
    instFill_.init(ctx.device.handle(), ctx.allocator.handle(),
                   backend_->renderPass(), backend_->sampleCount(),
                   *pipelineCache_, *descPool_);
    instEdge_.init(ctx.device.handle(), ctx.allocator.handle(),
                   backend_->renderPass(), backend_->sampleCount(),
                   *pipelineCache_, *descPool_);
    gpuLine_.init(ctx.device.handle(), ctx.allocator.handle(),
                  *descPool_, *pipelineCache_);
    pointR_.init(ctx.device.handle(), backend_->renderPass(),
                 backend_->sampleCount(), *descPool_, *pipelineCache_);
    maxPointSize_ =
        ctx.device.physical().getProperties().limits.pointSizeRange[1];
    heat_.init(ctx.device.handle(), backend_->renderPass(),
               backend_->sampleCount(), *pipelineCache_, *descPool_);
    text_.init(ctx.device.handle(), ctx.allocator.handle(),
               backend_->renderPass(), backend_->sampleCount(),
               *pipelineCache_, *descPool_);
    text_.prepareAtlas(ctx.device.graphicsQueue(),
                       ctx.graphicsPool.handle());
    inited_ = true;
}

MplCanvas::~MplCanvas() {
    if (backend_) {
        // Let in-flight frames finish before tearing down Vulkan
        // objects held by the renderers.
        auto& ctx = backend_->context();
        if (auto dev = ctx.device.handle()) dev.waitIdle();
    }
}

void MplCanvas::resize(uint32_t w, uint32_t h) {
    if (!backend_ || (w == 0 || h == 0)) return;
    backend_->resize(w, h);
}

// ═══ frame lifecycle ═══════════════════════════════════════════════

void MplCanvas::beginFrame() {
    ops_.clear();
    clear_ = plot::Color{1, 1, 1, 1};
}

void MplCanvas::endFrame() {
    // GPU pre-pass: tessellate large solid edge strokes on the GPU —
    // CPU stroking dominates replay for ≥~1k-vertex polylines.
    gpuPrepass();
    for (int pass = 0; pass < 2; ++pass) {
        backend_->setClearColor(clear_.r, clear_.g, clear_.b, clear_.a);
        auto cmd = backend_->beginFrame();
        spine_.resetScratch();
        instFill_.resetScratch();
        instEdge_.resetScratch();
        pointR_.resetScratch();
        text_.resetScratch();
        execute(cmd);
        backend_->endFrame();
        if (!text_.atlasDirty()) break;
        // New glyphs rasterized mid-pass: re-upload and repaint once.
        auto& ctx = backend_->context();
        text_.syncAtlas(ctx.device.graphicsQueue(),
                        ctx.graphicsPool.handle());
    }
    ops_.clear();
}

std::vector<uint8_t> MplCanvas::readback() const {
    return backend_ ? backend_->readbackRgba8() : std::vector<uint8_t>{};
}

// ═══ op recording ═══════════════════════════════════════════════════

void MplCanvas::path(std::span<const float> verts,
                     std::span<const uint8_t> codes,
                     plot::Color face, bool fillEvenOdd,
                     plot::Color edge, float lwPx,
                     float dashOffset, std::span<const float> dashSeq,
                     plot::JoinStyle join, plot::CapStyle cap,
                     std::string_view hatch, plot::Color hatchColor,
                     std::optional<plot::Rect2D> clip,
                     std::span<const plot::Point2D> clipRing) {
    PathOp op;
    op.verts.assign(verts.begin(), verts.end());
    op.codes.assign(codes.begin(), codes.end());
    op.face = face; op.fillEvenOdd = fillEvenOdd;
    op.edge = edge; op.lwPx = lwPx;
    op.dashOffset = dashOffset;
    op.dashSeq.assign(dashSeq.begin(), dashSeq.end());
    op.join = join; op.cap = cap;
    op.hatch = hatch;
    op.hatchColor = hatchColor;
    op.clip = clip;
    op.clipRing.assign(clipRing.begin(), clipRing.end());
    ops_.emplace_back(std::move(op));
}

void MplCanvas::image(std::span<const uint8_t> rgba, uint32_t w,
                      uint32_t h, plot::Rect2D dstPx, int interp,
                      std::optional<plot::Rect2D> clip) {
    ImageOp op;
    op.rgba.assign(rgba.begin(), rgba.end());
    op.w = w; op.h = h; op.dst = dstPx; op.interp = interp;
    op.clip = clip;
    ops_.emplace_back(std::move(op));
}

void MplCanvas::text(float x, float y, std::string utf8, float sizePx,
                     plot::Color color, float rotDeg,
                     std::string family, std::string style,
                     std::string weight,
                     std::optional<plot::Rect2D> clip) {
    TextOp op;
    op.x = x; op.y = y; op.text = std::move(utf8);
    op.sizePx = sizePx; op.color = color; op.rotDeg = rotDeg;
    op.family = std::move(family); op.style = std::move(style);
    op.weight = std::move(weight); op.clip = clip;
    ops_.emplace_back(std::move(op));
}

void MplCanvas::mathText(float x, float y, std::string utf8,
                         float sizePx, plot::Color color, float rotDeg,
                         std::string fontset, std::string family,
                         std::string style, std::string weight,
                         std::optional<plot::Rect2D> clip) {
    TextOp op;
    op.x = x; op.y = y; op.text = std::move(utf8);
    op.sizePx = sizePx; op.color = color; op.rotDeg = rotDeg;
    op.math = true; op.fontset = std::move(fontset);
    op.family = std::move(family); op.style = std::move(style);
    op.weight = std::move(weight); op.clip = clip;
    ops_.emplace_back(std::move(op));
}

void MplCanvas::gouraud(float x0, float y0, float x1, float y1,
                        float x2, float y2, plot::Color c0,
                        plot::Color c1, plot::Color c2,
                        std::optional<plot::Rect2D> clip) {
    GouraudOp op;
    op.x[0] = x0; op.y[0] = y0;
    op.x[1] = x1; op.y[1] = y1;
    op.x[2] = x2; op.y[2] = y2;
    op.c[0] = c0; op.c[1] = c1; op.c[2] = c2;
    op.clip = clip;
    ops_.emplace_back(std::move(op));
}

void MplCanvas::tris(std::span<const float> verts,
                     std::span<const float> colors,
                     std::optional<plot::Rect2D> clip) {
    TrisOp op;
    op.verts.assign(verts.begin(), verts.end());
    op.colors.assign(colors.begin(), colors.end());
    op.clip = clip;
    ops_.emplace_back(std::move(op));
}

void MplCanvas::instances(std::span<const float> tplVerts,
                          std::span<const uint8_t> tplCodes,
                          std::span<const float> instXY,
                          std::span<const float> instRGBA,
                          plot::Color uniform,
                          float lwPx, plot::JoinStyle join,
                          plot::CapStyle cap,
                          std::span<const float> edgeRGBA,
                          plot::Color uniformEdge,
                          std::span<const float> instScale,
                          std::optional<plot::Rect2D> clip) {
    InstanceOp op;
    op.tplVerts.assign(tplVerts.begin(), tplVerts.end());
    op.tplCodes.assign(tplCodes.begin(), tplCodes.end());
    op.instXY.assign(instXY.begin(), instXY.end());
    op.instRGBA.assign(instRGBA.begin(), instRGBA.end());
    op.uniform = uniform;
    op.lwPx = lwPx; op.join = join; op.cap = cap;
    op.edgeRGBA.assign(edgeRGBA.begin(), edgeRGBA.end());
    op.uniformEdge = uniformEdge;
    op.instScale.assign(instScale.begin(), instScale.end());
    op.clip = clip;
    ops_.emplace_back(std::move(op));
}

void MplCanvas::points(std::span<const float> xy,
                       std::span<const float> rgba,
                       std::span<const float> sizes,
                       plot::Color uniform,
                       float markerCode, float markerFill,
                       float markerSides, float markerAngle,
                       std::optional<plot::Rect2D> clip) {
    PointsOp op;
    op.xy.assign(xy.begin(), xy.end());
    op.rgba.assign(rgba.begin(), rgba.end());
    op.sizes.assign(sizes.begin(), sizes.end());
    op.uniform = uniform;
    op.code = markerCode; op.fill = markerFill;
    op.sides = markerSides; op.angle = markerAngle;
    op.clip = clip;
    ops_.emplace_back(std::move(op));
}

// ═══ metrics ════════════════════════════════════════════════════════

MplCanvas::TextMetrics
MplCanvas::measureText(std::string_view utf8, float sizePx,
                       const std::string& family,
                       const std::string& style,
                       const std::string& weight) {
    auto fm = text_.faceFor(family, style, weight);
    auto m = text_.measureText(utf8, sizePx / kFontBasePx, fm.face);
    return {m.width, m.height, m.ascent};
}

MplCanvas::TextMetrics
MplCanvas::measureMath(std::string_view utf8, float sizePx,
                       const std::string& fontset,
                       const std::string& family,
                       const std::string& style,
                       const std::string& weight) {
    auto fm = text_.faceFor(family, style, weight);
    float scale = sizePx / kFontBasePx;
    text::MeasureFn measure = [this, scale, f = fm.face](
                                  std::string_view s, float sc) {
        auto m = text_.measureText(s, sc, f);
        return text::TextMeasure{m.width, m.height, m.ascent};
    };
    text::MeasureFn alt;
    if (auto* serif = text_.serifFace())
        alt = [this, serif](std::string_view s, float sc) {
            auto m = text_.measureText(s, sc, serif);
            return text::TextMeasure{m.width, m.height, m.ascent};
        };
    auto lay = text::layoutMathText(utf8, scale, measure,
                                    text::parseMathFontset(fontset), alt);
    return {lay.width, lay.ascent + lay.descent, lay.ascent};
}

// ═══ window interaction ═════════════════════════════════════════════

bool MplCanvas::pollEvents() {
    return backend_ ? backend_->pollEvents() : false;
}
std::vector<backend::InputEvent> MplCanvas::takeEvents() {
    return backend_ ? backend_->takeEvents()
                    : std::vector<backend::InputEvent>{};
}
void MplCanvas::toggleFullscreen() {
    if (backend_) backend_->toggleFullscreen();
}
void MplCanvas::setTitle(std::string_view title) {
    if (backend_) backend_->setWindowTitle(title);
}

// ═══ replay ═════════════════════════════════════════════════════════

void MplCanvas::execute(vk::CommandBuffer cmd) {
    for (const auto& op : ops_)
        std::visit([&](const auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, PathOp>) execPath(cmd, o);
            else if constexpr (std::is_same_v<T, ImageOp>) execImage(cmd, o);
            else if constexpr (std::is_same_v<T, TextOp>) execText(cmd, o);
            else if constexpr (std::is_same_v<T, TrisOp>) execTris(cmd, o);
            else if constexpr (std::is_same_v<T, InstanceOp>)
                execInstances(cmd, o);
            else if constexpr (std::is_same_v<T, PointsOp>)
                execPoints(cmd, o);
            else execGouraud(cmd, o);
        }, op);
}

plot::Path MplCanvas::pathFromOp(const PathOp& op) {
    // Rebuild the path. mpl may emit more codes than vertices (e.g.
    // QuadMesh paths where the CLOSEPOLY code has no vertex): iterate
    // over the codes and synthesize the closing vertex from the
    // current subpath start so `toPolylines` still marks it closed.
    plot::Path p;
    size_t nv = op.verts.size() / 2;
    p.vertices.reserve(std::max(nv, op.codes.size()));
    p.codes.reserve(std::max(nv, op.codes.size()));
    size_t subStart = 0;
    for (size_t i = 0; i < op.codes.size(); ++i) {
        auto code = static_cast<plot::Path::Code>(op.codes[i]);
        plot::Point2D pt;
        if (i < nv) {
            pt = {op.verts[2 * i], op.verts[2 * i + 1]};
        } else if (code == plot::Path::ClosePoly &&
                   !p.vertices.empty()) {
            pt = p.vertices[subStart];
        } else {
            break;
        }
        if (code == plot::Path::MoveTo) subStart = p.vertices.size();
        p.vertices.push_back(pt);
        p.codes.push_back(code);
    }
    // Vertices without codes become implicit LINETO segments.
    for (size_t i = p.vertices.size(); i < nv; ++i) {
        p.vertices.push_back({op.verts[2 * i], op.verts[2 * i + 1]});
        p.codes.push_back(p.codes.empty() ? plot::Path::MoveTo
                                          : plot::Path::LineTo);
    }
    return p;
}

void MplCanvas::gpuPrepass() {
    if (!gpuLine_.inited()) return;
    auto& ctx = backend_->context();
    bool recorded = false;
    if (!preCmd_)
        preCmd_.emplace(ctx.device.handle(), ctx.graphicsPool.handle());
    preCmd_->reset();
    preCmd_->begin();
    gpuLine_.resetScratch();
    for (auto& v : ops_) {
        auto* op = std::get_if<PathOp>(&v);
        if (!op || op->edge.a <= 0.0f || op->lwPx <= 0.0f ||
            !op->dashSeq.empty() || !op->clipRing.empty())
            continue;
        op->subsCache = pathFromOp(*op).toPolylines();
        size_t total = 0;
        bool anyClosed = false;
        for (auto& sp : op->subsCache) {
            anyClosed |= sp.closed;
            total += sp.points.size();
        }
        if (anyClosed) {
            op->subsCache.clear();
            continue;
        }
        // Massively oversampled polylines: replay strokes the per-
        // pixel-column min/max envelope instead — raster-equivalent
        // coverage at ~2 verts per column.
        auto ext = backend_->extent();
        float clipW = op->clip ? float(op->clip->width)
                               : float(ext.width);
        size_t huge = size_t(std::max(8192.0f, clipW * 4.0f));
        if (total > huge) {
            op->decimateEdge = true;
            continue;
        }
        // CPU stroking wins for small paths (dispatch overhead).
        if (total < 1024) {
            op->subsCache.clear();
            continue;
        }
        plot::StrokeParams sp;
        sp.width = op->lwPx;
        sp.join = op->join;
        sp.cap = op->cap;
        for (auto& sub : op->subsCache) {
            if (sub.points.size() < 2) continue;
            auto ms = gpuLine_.tessellate(preCmd_->handle(), sub.points,
                                          sp, op->edge);
            op->edgeMeshes.insert(op->edgeMeshes.end(), ms.begin(),
                                  ms.end());
        }
        op->gpuStroke = true;
        recorded = true;
    }
    preCmd_->end();
    // Same-queue ordering makes the tessellated meshes visible to the
    // render-pass submission (same mechanism as staging uploads).
    if (recorded) {
        vk::SubmitInfo si{};
        vk::CommandBuffer pcb = preCmd_->handle();
        si.setCommandBuffers(pcb);
        ctx.device.graphicsQueue().submit(si);
    }
}

void MplCanvas::execPath(vk::CommandBuffer cmd, const PathOp& op) {
    auto res = backend_->extent();
    auto clip = clipVk(op.clip, res);
    std::vector<plot::Path::Subpath> localSubs;
    if (op.subsCache.empty())
        localSubs = pathFromOp(op).toPolylines();
    const auto& subs = !op.subsCache.empty() ? op.subsCache : localSubs;

    // mpl closes some paths by repeating the first vertex as LINETO
    // (e.g. QuadMesh quads) without a CLOSEPOLY code — treat a subpath
    // ending at its start as a closed ring and drop the duplicate.
    auto ringPoints = [](const plot::Path::Subpath& sp)
            -> std::optional<std::vector<plot::Point2D>> {
        auto pts = sp.points;
        if (pts.size() >= 4 && pts.front().x == pts.back().x &&
            pts.front().y == pts.back().y)
            pts.pop_back();
        else if (!sp.closed)
            return std::nullopt;
        if (pts.size() < 3) return std::nullopt;
        return pts;
    };

    // ── fill ──
    if (op.face.a > 0.0f && !subs.empty()) {
        // Partition closed subpaths into outer rings + holes (odd
        // containment depth), then bridge-merge holes into their
        // parent ring before ear-clipping.
        std::vector<std::vector<plot::Point2D>> closed;
        size_t closedVerts = 0;
        for (auto& sp : subs) {
            if (auto r = ringPoints(sp)) {
                closedVerts += r->size();
                closed.push_back(std::move(*r));
            }
        }
        std::vector<plot::Point2D> tris;
        if (closedVerts > 512) {
            // earClip rescans the ring per ear (O(n²)-worse on
            // reflex-heavy boundaries like stackplot's jagged polys);
            // scanline-fill per pixel column instead (even-odd parity
            // across all rings = mpl's fill rule).
            int cx0 = op.clip ? op.clip->x : 0;
            int cx1 = (op.clip ? op.clip->x + int(op.clip->width)
                               : int(res.width)) - 1;
            tris = plot::columnFill(closed, cx0, cx1);
            if (!op.clipRing.empty())
                tris = plot::clipTrianglesToPolygon(tris, op.clipRing);
            if (!tris.empty())
                spine_.drawTriangles(cmd, clip, res, tris, op.face);
        } else {
        // Largest first so outers precede their holes.
        std::ranges::sort(closed, {}, [](const auto& s) {
            return -std::abs(ringArea(s));
        });
        std::vector<std::pair<std::vector<plot::Point2D>, int>> rings; // ring, depth
        for (auto& sp : closed) {
            int depth = 0;
            const std::vector<plot::Point2D>* parent = nullptr;
            for (auto& [ring, d] : rings) {
                if (insideRing(sp[0], ring)) { depth++; parent = &ring; }
            }
            if (depth % 2 == 1 && parent) {
                auto merged = bridgeHole(*parent, sp);
                if (!merged.empty()) {
                    auto t = earClip(merged);
                    tris.insert(tris.end(), t.begin(), t.end());
                    rings.emplace_back(std::move(merged), depth);
                    continue;
                }
            }
            auto t = earClip(sp);
            tris.insert(tris.end(), t.begin(), t.end());
            rings.emplace_back(sp, depth);
        }
        if (!op.clipRing.empty())
            tris = plot::clipTrianglesToPolygon(tris, op.clipRing);
        if (!tris.empty())
            spine_.drawTriangles(cmd, clip, res, tris, op.face);
        }
    }

    // ── hatch (mpl: pattern drawn in edge/hatch color, clipped to fill) ──
    if (!op.hatch.empty()) {
        std::vector<plot::Point2D> ht;
        for (auto& sp : subs) {
            auto r = ringPoints(sp);
            if (!r) continue;
            auto h = plot::hatchTriangles(*r, op.hatch, 6.0f);
            ht.insert(ht.end(), h.begin(), h.end());
        }
        if (!op.clipRing.empty())
            ht = plot::clipTrianglesToPolygon(ht, op.clipRing);
        if (!ht.empty())
            spine_.drawTriangles(cmd, clip, res, ht,
                                 op.hatchColor.a > 0 ? op.hatchColor
                                                     : op.edge);
    }

    // ── edge ──
    if (op.gpuStroke) {
        for (auto& m : op.edgeMeshes)
            spine_.drawTrianglesGpu(cmd, clip, res, m.buffer,
                                    m.firstVertex * 6 * sizeof(float),
                                    m.vertexCount);
    } else if (op.edge.a > 0.0f && op.lwPx > 0.0f) {
        plot::StrokeParams sp;
        sp.width = op.lwPx;
        sp.dashes = op.dashSeq;
        sp.dashOffset = op.dashOffset;
        sp.join = op.join;
        sp.cap = op.cap;
        std::vector<plot::Point2D> ev;
        if (op.decimateEdge) {
            int cx0 = op.clip ? op.clip->x : 0;
            int cx1 = (op.clip ? op.clip->x + int(op.clip->width)
                               : int(res.width)) - 1;
            for (auto& sub : subs) {
                if (sub.points.size() < 2) continue;
                for (auto& run :
                     plot::columnDecimate(sub.points, cx0, cx1)) {
                    auto mesh = plot::strokePolyline(run, sp);
                    ev.insert(ev.end(), mesh.verts.begin(),
                              mesh.verts.end());
                }
            }
        } else {
            for (auto& sub : subs) {
                if (sub.points.size() < 2) continue;
                auto pts = sub.points;
                if (sub.closed &&
                    (pts.front().x != pts.back().x ||
                     pts.front().y != pts.back().y))
                    pts.push_back(pts.front());
                auto mesh = plot::strokePolyline(pts, sp);
                ev.insert(ev.end(), mesh.verts.begin(),
                          mesh.verts.end());
            }
        }
        if (!op.clipRing.empty())
            ev = plot::clipTrianglesToPolygon(ev, op.clipRing);
        if (!ev.empty()) spine_.drawTriangles(cmd, clip, res, ev, op.edge);
    }
}

void MplCanvas::execImage(vk::CommandBuffer cmd, const ImageOp& op) {
    if (op.w == 0 || op.h == 0 || op.rgba.size() < size_t(op.w) * op.h * 4)
        return;
    auto& ctx = backend_->context();
    plot::Grid2D g;
    g.width = op.w; g.height = op.h;
    g.rgba.resize(size_t(op.w) * op.h);
    for (size_t i = 0; i < g.rgba.size(); ++i) {
        uint32_t r = op.rgba[4 * i], gg = op.rgba[4 * i + 1];
        uint32_t b = op.rgba[4 * i + 2], a = op.rgba[4 * i + 3];
        g.rgba[i] = r | (gg << 8) | (b << 16) | (a << 24);
    }
    g.xRange = {0, 1}; g.yRange = {0, 1};
    plot::Colormap cmap;  // unused in rgba mode
    heat_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                 ctx.graphicsPool.handle(), ctx.allocator.handle(), g,
                 cmap);
    // The grid rect maps onto the dst pixel rect; clip to dst ∩ op.clip.
    plot::Rect2D sc = op.dst;
    if (op.clip) {
        int x0 = std::max(op.dst.x, op.clip->x);
        int y0 = std::max(op.dst.y, op.clip->y);
        int x1 = std::min(op.dst.x + int(op.dst.width),
                          op.clip->x + int(op.clip->width));
        int y1 = std::min(op.dst.y + int(op.dst.height),
                          op.clip->y + int(op.clip->height));
        sc = {x0, y0, uint32_t(std::max(0, x1 - x0)),
              uint32_t(std::max(0, y1 - y0))};
    }
    if (sc.width == 0 || sc.height == 0) return;
    plot::Transform2D t{};
    t.view.x = {0, 1}; t.view.y = {0, 1};
    heat_.draw(cmd, vkRect(sc), t);
}

font_face* MplCanvas::faceFor(const TextOp& op) {
    auto fm = text_.faceFor(op.family, op.style, op.weight);
    return fm.face;
}

void MplCanvas::execText(vk::CommandBuffer cmd, const TextOp& op) {
    if (op.text.empty()) return;
    auto res = backend_->extent();
    auto clip = clipVk(op.clip, res);
    float scale = op.sizePx / kFontBasePx;
    float rot = op.rotDeg * float(M_PI) / 180.0f;
    if (!op.math) {
        text_.draw(cmd, clip, op.text, op.x, op.y, op.color, scale, rot,
                   plot::HAlign::Left, faceFor(op));
        return;
    }
    // Mathtext: layout runs + rules around the baseline origin.
    float baseScale = scale;
    text::MeasureFn measure = [this, f = faceFor(op)](
                                  std::string_view s, float sc) {
        auto m = text_.measureText(s, sc, f);
        return text::TextMeasure{m.width, m.height, m.ascent};
    };
    text::MeasureFn alt;
    auto* serif = text_.serifFace();
    if (serif)
        alt = [this, serif](std::string_view s, float sc) {
            auto m = text_.measureText(s, sc, serif);
            return text::TextMeasure{m.width, m.height, m.ascent};
        };
    auto lay = text::layoutMathText(
        op.text, baseScale, measure,
        text::parseMathFontset(op.fontset), alt);
    float cosR = std::cos(rot), sinR = std::sin(rot);
    auto rp = [&](float px, float py) {
        return plot::Point2D{op.x + px * cosR - py * sinR,
                       op.y + px * sinR + py * cosR};
    };
    for (auto& r : lay.runs) {
        auto p = rp(r.x, r.baseline);
        text_.draw(cmd, clip, r.text, p.x, p.y, op.color,
                   baseScale * r.scale, rot, plot::HAlign::Left,
                   r.face == 1 ? serif : faceFor(op));
    }
    for (auto& rl : lay.rules) {
        auto p0 = rp(rl.x0, rl.y0), p1 = rp(rl.x1, rl.y0);
        plot::Point2D pts[2] = {p0, p1};
        spine_.drawLineStrip(cmd, clip, res, std::span{pts, 2},
                             op.color, rl.thickness);
    }
}

void MplCanvas::execGouraud(vk::CommandBuffer cmd, const GouraudOp& op) {
    plot::Point2D tri[3] = {{op.x[0], op.y[0]}, {op.x[1], op.y[1]},
                            {op.x[2], op.y[2]}};
    auto res = backend_->extent();
    spine_.drawTrianglesVC(cmd, clipVk(op.clip, res), res,
                           std::span{tri, 3}, std::span{op.c, 3});
}

void MplCanvas::execTris(vk::CommandBuffer cmd, const TrisOp& op) {
    size_t n = op.verts.size() / 2;
    if (n < 3 || op.colors.size() < n * 4) return;
    trisScratch_.resize(n);
    for (size_t i = 0; i < n; ++i)
        trisScratch_[i] = {op.verts[2 * i], op.verts[2 * i + 1]};
    auto res = backend_->extent();
    spine_.drawTrianglesVC(
        cmd, clipVk(op.clip, res), res, trisScratch_,
        std::span{reinterpret_cast<const plot::Color*>(op.colors.data()),
                  n});
}

void MplCanvas::execInstances(vk::CommandBuffer cmd,
                              const InstanceOp& op) {
    auto res = backend_->extent();
    auto clip = clipVk(op.clip, res);
    size_t n = op.instXY.size() / 2;
    if (n == 0) return;
    bool perInstFace = op.instRGBA.size() >= n * 4;
    bool perInstEdge = op.edgeRGBA.size() >= n * 4;

    // Rebuild the template path (orphan CLOSEPOLY tolerated, as in
    // execPath), then split into subpaths.
    plot::Path p;
    size_t nv = op.tplVerts.size() / 2;
    p.vertices.reserve(std::max(nv, op.tplCodes.size()));
    p.codes.reserve(std::max(nv, op.tplCodes.size()));
    size_t subStart = 0;
    for (size_t i = 0; i < op.tplCodes.size(); ++i) {
        auto code = static_cast<plot::Path::Code>(op.tplCodes[i]);
        plot::Point2D pt;
        if (i < nv) {
            pt = {op.tplVerts[2 * i], op.tplVerts[2 * i + 1]};
        } else if (code == plot::Path::ClosePoly &&
                   !p.vertices.empty()) {
            pt = p.vertices[subStart];
        } else {
            break;
        }
        if (code == plot::Path::MoveTo) subStart = p.vertices.size();
        p.vertices.push_back(pt);
        p.codes.push_back(code);
    }
    for (size_t i = p.vertices.size(); i < nv; ++i) {
        p.vertices.push_back({op.tplVerts[2 * i], op.tplVerts[2 * i + 1]});
        p.codes.push_back(p.codes.empty() ? plot::Path::MoveTo
                                          : plot::Path::LineTo);
    }
    // Templates are already in pixel units (marker size is baked into
    // the verts and instance scale is 1). Tessellating every curve at
    // the default 16 steps gives ~130 verts for a 3px marker — pure
    // waste of rasterizer throughput. Scale the subdivision count to
    // the template's on-screen extent (~1px chords).
    float tplMax = 1.0f;
    for (auto& v : p.vertices) {
        tplMax = std::max(tplMax, std::max(std::abs(v.x), std::abs(v.y)));
    }
    int curveSteps = tplMax <= 3.0f
        ? 1
        : std::clamp(int(std::lround(tplMax / 1.5f)), 2, 16);
    auto subs = p.toPolylines(curveSteps);

    bool perInstScale = op.instScale.size() >= n * 2;
    auto fillInsts = [&](std::span<const float> rgba,
                         plot::Color uniform) {
        instScratch_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            auto& pi = instScratch_[i];
            pi.ox = op.instXY[2 * i];
            pi.oy = op.instXY[2 * i + 1];
            if (perInstScale) {
                pi.sx = op.instScale[2 * i];
                pi.sy = op.instScale[2 * i + 1];
            } else {
                pi.sx = 1.0f; pi.sy = 1.0f;
            }
            if (!rgba.empty()) {
                pi.r = rgba[4 * i];     pi.g = rgba[4 * i + 1];
                pi.b = rgba[4 * i + 2]; pi.a = rgba[4 * i + 3];
            } else {
                pi.r = uniform.r; pi.g = uniform.g;
                pi.b = uniform.b; pi.a = uniform.a;
            }
        }
        return std::span<const primitives::PathInstance>(instScratch_);
    };

    // ── fill: ear-clip each closed subpath, then ONE instanced draw ──
    bool wantFill = perInstFace
        ? ([&] { for (size_t i = 0; i < n; ++i)
                     if (op.instRGBA[4 * i + 3] > 0) return true;
                 return false; }())
        : op.uniform.a > 0.0f;
    if (wantFill) {
        tplScratch_.clear();
        for (auto& sp : subs) {
            if (!sp.closed || sp.points.size() < 3) continue;
            auto pts = sp.points;
            if (pts.size() >= 4 && pts.front().x == pts.back().x &&
                pts.front().y == pts.back().y)
                pts.pop_back();
            if (pts.size() < 3) continue;
            auto t = earClip(pts);
            tplScratch_.insert(tplScratch_.end(), t.begin(), t.end());
        }
        if (!tplScratch_.empty()) {
            auto& ctx = backend_->context();
            instFill_.setTemplate(ctx.device.handle(),
                                  ctx.device.graphicsQueue(),
                                  ctx.graphicsPool.handle(), tplScratch_);
            instFill_.drawInstanced(
                cmd, clip, res,
                fillInsts(op.instRGBA, op.uniform));
        }
    }

    // ── edge: stroke template, then ONE instanced draw ──
    bool anyEdge = perInstEdge
        ? ([&] { for (size_t i = 0; i < n; ++i)
                     if (op.edgeRGBA[4 * i + 3] > 0) return true;
                 return false; }())
        : op.uniformEdge.a > 0.0f;
    if (anyEdge && op.lwPx > 0.0f) {
        plot::StrokeParams sp;
        sp.width = op.lwPx;
        sp.join = op.join;
        sp.cap = op.cap;
        tplScratch_.clear();
        for (auto& sub : subs) {
            if (sub.points.size() < 2) continue;
            auto pts = sub.points;
            if (sub.closed &&
                (pts.front().x != pts.back().x ||
                 pts.front().y != pts.back().y))
                pts.push_back(pts.front());
            auto mesh = plot::strokePolyline(pts, sp);
            tplScratch_.insert(tplScratch_.end(), mesh.verts.begin(),
                               mesh.verts.end());
        }
        if (!tplScratch_.empty()) {
            auto& ctx = backend_->context();
            instEdge_.setTemplate(ctx.device.handle(),
                                  ctx.device.graphicsQueue(),
                                  ctx.graphicsPool.handle(), tplScratch_);
            instEdge_.drawInstanced(
                cmd, clip, res,
                fillInsts(op.edgeRGBA, op.uniformEdge));
        }
    }
}

void MplCanvas::execPoints(vk::CommandBuffer cmd, const PointsOp& op) {
    size_t n = op.xy.size() / 2;
    if (!n || op.sizes.empty()) return;
    auto* pts = reinterpret_cast<const plot::Point2D*>(op.xy.data());
    std::span<const plot::Point2D> pspan{pts, n};

    std::span<const plot::Color> cols;
    std::vector<plot::Color> colScratch;
    if (op.rgba.size() >= n * 4) {
        cols = {reinterpret_cast<const plot::Color*>(op.rgba.data()), n};
    } else {
        colScratch.assign(n, op.uniform);
        cols = colScratch;
    }
    std::vector<float> szScratch;
    std::span<const float> sz = op.sizes;
    if (sz.size() == 1 && n > 1) {
        szScratch.assign(n, sz[0]);
        sz = szScratch;
    } else if (sz.size() < n) {
        szScratch.resize(n);
        for (size_t i = 0; i < n; ++i) szScratch[i] = sz[i % sz.size()];
        sz = szScratch;
    }

    auto& ctx = backend_->context();
    if (!pointInit_) {
        pointR_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                       ctx.graphicsPool.handle(), ctx.allocator.handle(),
                       pspan, cols, sz);
        pointInit_ = true;
    } else {
        pointR_.updatePoints(pspan, cols, sz);
    }

    // Canvas-pixel view: x spans [0,W]; y is given the reversed range
    // [H,0] so canvas Y-down coords land correctly after the shader's
    // Y-flip (viewMin=H, span=-H).
    auto res = backend_->extent();
    plot::Transform2D t;
    t.view.x = {0.0f, static_cast<float>(res.width)};
    t.view.y = {static_cast<float>(res.height), 0.0f};
    primitives::MarkerParams m;
    m.code = op.code; m.fill = op.fill;
    m.numsides = op.sides; m.angle = op.angle;
    vk::Rect2D vp{vk::Offset2D{0, 0}, res};
    pointR_.draw(cmd, vp, clipVk(op.clip, res), t,
                 static_cast<uint32_t>(n), m);
}

} // namespace volcano::render
