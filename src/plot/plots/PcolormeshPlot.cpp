// volcano/plot/plots/PcolormeshPlot.cpp — pseudocolor mesh plot implementation
#include "volcano/plot/plots/PcolormeshPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <volcano/core/Buffer.hpp>
#include <volcano/core/CommandBuffer.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

/// Compute shader: expands one cell per invocation into fill triangles
/// (vec2 pos + vec4 color), matching buildGeometry()'s CPU output.

} // namespace

PcolormeshPlot::PcolormeshPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> C,
                               uint32_t nCols, uint32_t nRows,
                               PcolormeshConfig config)
    : x_(std::move(x)), y_(std::move(y)), C_(std::move(C)),
      nCols_(nCols), nRows_(nRows), config_(std::move(config)) {
    if (config_.shading == PcmShading::Gouraud) {
        // mpl shading='gouraud': coordinates and C share the same (M, N)
        // corner shape — x/y are corner coords, not cell edges.
        if (x_.size() != nCols_)
            throw std::invalid_argument(
                "PcolormeshPlot (gouraud): x must have nCols elements");
        if (y_.size() != nRows_)
            throw std::invalid_argument(
                "PcolormeshPlot (gouraud): y must have nRows elements");
        if (nCols_ < 2 || nRows_ < 2)
            throw std::invalid_argument(
                "PcolormeshPlot (gouraud): grid must be at least 2x2");
    } else {
        if (x_.size() != nCols_ + 1)
            throw std::invalid_argument("PcolormeshPlot: x must have nCols+1 elements");
        if (y_.size() != nRows_ + 1)
            throw std::invalid_argument("PcolormeshPlot: y must have nRows+1 elements");
    }
    if (C_.size() != nCols_ * nRows_)
        throw std::invalid_argument("PcolormeshPlot: C must have nCols*nRows elements");
}

Color PcolormeshPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void PcolormeshPlot::setArray(std::vector<float> a) {
    if (a.size() != C_.size())
        throw std::invalid_argument(
            std::format("set_array: expected {} values, got {}",
                        C_.size(), a.size()));
    C_ = std::move(a);
    config_.valueRange = {0, 0};  // invalid → re-autoscale
    prepared_ = false;
    touch();
}

void PcolormeshPlot::setClim(std::optional<float> vmin,
                             std::optional<float> vmax) {
    // mpl set_clim feeds the norm; without one the explicit range.
    if (!config_.norm)
        config_.norm = std::make_shared<NormalizeLinear>();
    config_.norm->setVmin(vmin.value_or(std::nanf("")));
    config_.norm->setVmax(vmax.value_or(std::nanf("")));
    prepared_ = false;
    touch();
}

void PcolormeshPlot::computeValueRange() {
    // If a norm is set, autoscale it from the data (if vmin/vmax not set).
    if (config_.norm) {
        config_.norm->autoscale(C_);
        valueRange_ = {config_.norm->vmin(), config_.norm->vmax()};
        return;
    }
    if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
        return;
    }
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : C_) {
        if (std::isnan(v)) continue;
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
    valueRange_ = {vmin, vmax};
}

void PcolormeshPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Color from colormap: use norm if set, else linear mapping.
    // NaN t -> cmap.bad (or transparent); out-of-range -> under/over.
    auto colorOf = [&](float val) {
        float t = config_.norm ? (*config_.norm)(val)
                               : (val - valueRange_.min) / vspan;
        return cmap.sample(t);
    };

    if (config_.shading == PcmShading::Gouraud) {
        // mpl QuadMesh._convert_mesh_to_triangles: each quad is split
        // into 4 triangles meeting at the center vertex, whose position
        // and color are the average of the 4 corners. Per-corner colors
        // come from C at the corner grid points.
        for (uint32_t j = 0; j + 1 < nRows_; ++j) {
            for (uint32_t i = 0; i + 1 < nCols_; ++i) {
                float va = C_[j * nCols_ + i];         // a = (i,   j)
                float vb = C_[j * nCols_ + i + 1];     // b = (i+1, j)
                float vc = C_[(j+1) * nCols_ + i + 1]; // c = (i+1, j+1)
                float vd = C_[(j+1) * nCols_ + i];     // d = (i,   j+1)
                // mpl drops a gouraud quad when any corner is masked.
                if (config_.skipNaN && !cmap.bad &&
                    (std::isnan(va) || std::isnan(vb) ||
                     std::isnan(vc) || std::isnan(vd)))
                    continue;

                Point2D pa{x_[i],   y_[j]};
                Point2D pb{x_[i+1], y_[j]};
                Point2D pc{x_[i+1], y_[j+1]};
                Point2D pd{x_[i],   y_[j+1]};
                Point2D pCtr{(pa.x + pb.x + pc.x + pd.x) * 0.25f,
                             (pa.y + pb.y + pc.y + pd.y) * 0.25f};

                Color ca = colorOf(va), cb = colorOf(vb);
                Color cc = colorOf(vc), cd = colorOf(vd);
                Color cCtr{(ca.r + cb.r + cc.r + cd.r) * 0.25f,
                           (ca.g + cb.g + cc.g + cd.g) * 0.25f,
                           (ca.b + cb.b + cc.b + cd.b) * 0.25f,
                           (ca.a + cb.a + cc.a + cd.a) * 0.25f};

                const Point2D* pts[12] = {
                    &pa, &pb, &pCtr, &pb, &pc, &pCtr,
                    &pc, &pd, &pCtr, &pd, &pa, &pCtr};
                const Color* cls[12] = {
                    &ca, &cb, &cCtr, &cb, &cc, &cCtr,
                    &cc, &cd, &cCtr, &cd, &ca, &cCtr};
                for (int k = 0; k < 12; ++k) {
                    fillPositions_.push_back(*pts[k]);
                    fillColors_.push_back(*cls[k]);
                }
            }
        }
        return;
    }

    for (uint32_t j = 0; j < nRows_; ++j) {
        for (uint32_t i = 0; i < nCols_; ++i) {
            float val = C_[j * nCols_ + i];
            if (config_.skipNaN && std::isnan(val) && !cmap.bad) continue;

            // Cell corners.
            float x0 = x_[i], x1 = x_[i + 1];
            float y0 = y_[j], y1 = y_[j + 1];

            Color color = colorOf(val);

            // Two triangles per cell.
            Point2D bl{x0, y0}, br{x1, y0}, ul{x0, y1}, ur{x1, y1};
            // Triangle 1: bl, br, ul
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ul);
            // Triangle 2: br, ur, ul
            fillPositions_.push_back(br);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ul);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(color);
        }
    }
}

bool PcolormeshPlot::buildGeometryGpu(render::Renderer& r) {
    const bool gouraud = config_.shading == PcmShading::Gouraud;
    const uint32_t cells = gouraud ? (nCols_ - 1) * (nRows_ - 1)
                                   : nCols_ * nRows_;
    const uint32_t vertsPerCell = gouraud ? 12 : 6;
    const uint64_t nVerts = uint64_t(cells) * vertsPerCell;
    if (cells == 0 || nVerts > (1ull << 31)) return false;

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Per-cell / per-corner normalized t on the CPU — one evaluation per
    // value, so arbitrary polymorphic norms stay supported.
    std::vector<float> tvals(C_.size());
    for (size_t i = 0; i < C_.size(); ++i) {
        float v = C_[i];
        tvals[i] = std::isnan(v) ? std::numeric_limits<float>::quiet_NaN()
                   : config_.norm ? (*config_.norm)(v)
                                  : (v - valueRange_.min) / vspan;
    }

    // 259-entry LUT: [0..255] regular, 256 under, 257 over, 258 bad.
    std::vector<Color> lut(259);
    for (int i = 0; i < 256; ++i)
        lut[i] = cmap.sample(float(i) / 255.0f);
    lut[256] = cmap.under.value_or(cmap.sample(0.0f));
    lut[257] = cmap.over.value_or(cmap.sample(1.0f));
    lut[258] = cmap.bad.value_or(Color::transparent());

    const uint32_t flags =
        (cmap.bad ? 1u : 0u) | (config_.skipNaN ? 2u : 0u);
    render::GpuBuf posTok = 0, colTok = 0;
    if (!r.gpu().pcmTessellate(x_, y_, tvals, lut, nCols_, nRows_,
                               gouraud, flags, posTok, colTok))
        return false;
    fillRenderer_->adoptBuffers(posTok, colTok, uint32_t(nVerts));
    return true;
}

bool PcolormeshPlot::eligibleForTexture() const {
    if (config_.shading != PcmShading::Flat) return false;
    if (config_.edgeColor.a > 0.0f) return false;
    if (nCols_ == 0 || nRows_ == 0) return false;
    // Custom under/over or an opaque bad color need the LUT path.
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    if (cmap.under || cmap.over) return false;
    if (cmap.bad && cmap.bad->a > 0.0f) return false;
    // Only regular rectangular grids map to a texture quad.
    auto uniform = [](const std::vector<float>& e) {
        float d0 = e[1] - e[0];
        float tol = std::max(std::abs(e.back() - e.front()), 1.0f) * 1e-5f;
        for (size_t i = 1; i + 1 < e.size(); ++i)
            if (std::abs(e[i + 1] - e[i] - d0) > tol) return false;
        return true;
    };
    return uniform(x_) && uniform(y_);
}

void PcolormeshPlot::uploadTexture(render::Renderer& r) {
    Grid2D g;
    g.width = nCols_;
    g.height = nRows_;
    // Edges verbatim — reversed ranges are handled by the span sign.
    g.xRange = {x_.front(), x_.back()};
    g.yRange = {y_.front(), y_.back()};
    // Row 0 sits at y_[0] (the grid-range origin side).
    g.origin = "lower";
    g.interpolation = "nearest";
    g.values = C_;
    bool hasNaN = false;
    if (config_.norm) {
        config_.norm->autoscale(C_);
        for (auto& v : g.values) {
            if (std::isnan(v)) { hasNaN = true; continue; }
            v = (*config_.norm)(v);
        }
        g.valueRange = {0.0f, 1.0f};
    } else {
        for (float v : g.values) hasNaN |= std::isnan(v);
        g.valueRange = valueRange_;
    }
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    texRenderer_->upload(g, cmap, config_.skipNaN && hasNaN);
}

void PcolormeshPlot::prepare(render::Renderer& r) {
    computeValueRange();
    useTex_ = eligibleForTexture();
    if (useTex_) {
        if (!texInit_) {
            if (!texRenderer_) texRenderer_ = r.gpu().createHeatmapRenderer();
            texInit_ = true;
        }
        uploadTexture(r);
        prepared_ = true;
        return;
    }
    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    const bool gouraud = config_.shading == PcmShading::Gouraud;
    const uint64_t cells = gouraud ? uint64_t(nCols_ - 1) * (nRows_ - 1)
                                   : uint64_t(nCols_) * nRows_;
    bool gpu = config_.gpuTessellate > 0 ||
               (config_.gpuTessellate < 0 && cells >= 16384);
    if (gpu && !buildGeometryGpu(r))
        gpu = false;
    if (!gpu) {
        buildGeometry();
        if (!fillPositions_.empty()) {
            fillRenderer_->upload(std::span{fillPositions_}, std::span{fillColors_});
        }
    }
    prepared_ = true;
}

void PcolormeshPlot::draw(render::Cmd& cmd, render::Renderer& r,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    if (useTex_) {
        texRenderer_->draw(cmd, vrect, t);
        return;
    }
    if (fillRenderer_->pointCount() == 0) return;
    fillRenderer_->draw(cmd, vrect, t);
}

void PcolormeshPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    for (float yv : y_) {
        v.y.min = std::min(v.y.min, yv);
        v.y.max = std::max(v.y.max, yv);
    }
}

} // namespace volcano::plot
