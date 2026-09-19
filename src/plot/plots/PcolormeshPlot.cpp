// volcano/plot/plots/PcolormeshPlot.cpp — pseudocolor mesh plot implementation
#include "volcano/plot/plots/PcolormeshPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

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

void PcolormeshPlot::prepare(render::Renderer& r) {
    computeValueRange();
    buildGeometry();
    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void PcolormeshPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
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
