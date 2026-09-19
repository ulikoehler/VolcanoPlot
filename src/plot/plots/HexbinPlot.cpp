// volcano/plot/plots/HexbinPlot.cpp — hexagonal binning implementation
#include "volcano/plot/plots/HexbinPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

/// Hash function for hex axial coordinates (q, r).
struct IntPairHash {
    size_t operator()(std::pair<int,int> p) const noexcept {
        return std::hash<int64_t>()(
            (static_cast<int64_t>(p.first) << 32) | static_cast<uint32_t>(p.second));
    }
};

} // namespace

HexbinPlot::HexbinPlot(std::vector<float> x, std::vector<float> y,
                       HexbinConfig config)
    : x_(std::move(x)), y_(std::move(y)), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("HexbinPlot: x and y must have the same size");
}

Color HexbinPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

std::array<Point2D, 6> HexbinPlot::hexVertices(float cx, float cy, float r) const {
    std::array<Point2D, 6> verts;
    if (config_.orientation == HexOrientation::PointyTop) {
        // mpl polygon: [sx, sy/3] * [[.5,-.5],[.5,.5],[0,1],[-.5,.5],
        //                            [-.5,-.5],[0,-1]]  (sx/sy = cell pitch)
        constexpr float kX[6] = {.5f, .5f, 0.f, -.5f, -.5f, 0.f};
        constexpr float kY[6] = {-.5f, .5f, 1.f, .5f, -.5f, -1.f};
        for (int i = 0; i < 6; ++i)
            verts[i] = {cx + kX[i] * hexSX_, cy + kY[i] * (hexSY_ / 3.0f)};
    } else {
        // Flat-top: vertices at 0, 60, 120, 180, 240, 300 degrees.
        for (int i = 0; i < 6; ++i) {
            float angle = (60.0f * i) * static_cast<float>(M_PI) / 180.0f;
            verts[i] = {cx + r * std::cos(angle), cy + r * std::sin(angle)};
        }
    }
    return verts;
}

void HexbinPlot::computeBins() {
    centers_.clear();
    counts_.clear();
    if (x_.empty()) return;

    // Determine data range.
    xMin_ = *std::ranges::min_element(x_);
    xMax_ = *std::ranges::max_element(x_);
    yMin_ = *std::ranges::min_element(y_);
    yMax_ = *std::ranges::max_element(y_);
    if (xMax_ <= xMin_) xMax_ = xMin_ + 1.0f;
    if (yMax_ <= yMin_) yMax_ = yMin_ + 1.0f;
    float xSpan = xMax_ - xMin_;
    float ySpan = yMax_ - yMin_;

    if (config_.orientation == HexOrientation::PointyTop) {
        // matplotlib hexbin (axes/_axes.py::hexbin):
        //   nx = gridsize, ny = int(nx / sqrt(3))
        //   sx = (xmax-xmin)/nx, sy = (ymax-ymin)/ny
        // Two interleaved lattices: A = (nx+1)x(ny+1) at (i*sx, j*sy),
        // B = nx*ny offset by (+0.5sx, +0.5sy). Each point goes to the
        // nearer cell under d = dx^2 + 3*dy^2 (hex-metric in index space).
        int nx = std::max(1, config_.gridsize);
        int ny = std::max(1, int(nx / std::sqrt(3.0)));
        float sx = xSpan / nx, sy = ySpan / ny;
        hexSX_ = sx; hexSY_ = sy;
        hexRadius_ = sy / 3.0f;  // vertical vertex distance (mpl polygon)

        // counts indexed [lattice][q][r]: A in [0..nx]x[0..ny],
        // B in [0..nx)x[0..ny).
        std::vector<int> cA((nx + 1) * (ny + 1), 0), cB(nx * ny, 0);
        for (size_t k = 0; k < x_.size(); ++k) {
            float ix = (x_[k] - xMin_) / sx;
            float iy = (y_[k] - yMin_) / sy;
            int ix1 = int(std::round(ix)), iy1 = int(std::round(iy));
            int ix2 = int(std::floor(ix)), iy2 = int(std::floor(iy));
            float d1 = (ix - ix1) * (ix - ix1) + 3.0f * (iy - iy1) * (iy - iy1);
            float d2 = (ix - ix2 - 0.5f) * (ix - ix2 - 0.5f) +
                       3.0f * (iy - iy2 - 0.5f) * (iy - iy2 - 0.5f);
            if (d1 < d2) {
                if (ix1 >= 0 && ix1 <= nx && iy1 >= 0 && iy1 <= ny)
                    cA[ix1 * (ny + 1) + iy1]++;
            } else {
                if (ix2 >= 0 && ix2 < nx && iy2 >= 0 && iy2 < ny)
                    cB[ix2 * ny + iy2]++;
            }
        }
        // Emit every lattice cell (mpl draws all cells; mincnt filters).
        for (int i = 0; i <= nx; ++i)
            for (int j = 0; j <= ny; ++j) {
                if (cA[i * (ny + 1) + j] < config_.minCount) continue;
                centers_.push_back({xMin_ + i * sx, yMin_ + j * sy});
                counts_.push_back(float(cA[i * (ny + 1) + j]));
            }
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j) {
                if (cB[i * ny + j] < config_.minCount) continue;
                centers_.push_back({xMin_ + (i + 0.5f) * sx,
                                    yMin_ + (j + 0.5f) * sy});
                counts_.push_back(float(cB[i * ny + j]));
            }
    } else {
        // Flat-top axial coordinates (non-mpl extension).
        float hexWidth = xSpan / std::max(1, config_.gridsize);
        hexRadius_ = hexWidth / 2.0f;
        hexSX_ = hexRadius_ * 1.5f;
        hexSY_ = hexRadius_ * std::sqrt(3.0f);
        std::unordered_map<std::pair<int,int>, int, IntPairHash> bins;
        for (size_t k = 0; k < x_.size(); ++k) {
            float px = x_[k] - xMin_, py = y_[k] - yMin_;
            int q = int(std::round((2.0f / 3.0f * px) / hexRadius_));
            int r = int(std::round((-1.0f / 3.0f * px +
                                    std::sqrt(3.0f) / 3.0f * py) / hexRadius_));
            bins[{q, r}]++;
        }
        for (const auto& [key, count] : bins) {
            if (count < config_.minCount) continue;
            int q = key.first, r = key.second;
            centers_.push_back(
                {xMin_ + hexRadius_ * 1.5f * q,
                 yMin_ + hexRadius_ * (std::sqrt(3.0f) / 2.0f * q +
                                       std::sqrt(3.0f) * r)});
            counts_.push_back(static_cast<float>(count));
        }
    }

    // Apply normalization.
    if (config_.normMode == HexbinNorm::Density) {
        float total = static_cast<float>(x_.size());
        if (total > 0) {
            for (auto& c : counts_) c /= total;
        }
    }

    // Compute value range.
    if (config_.norm) {
        config_.norm->autoscale(counts_);
        valueRange_ = {config_.norm->vmin(), config_.norm->vmax()};
    } else if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
    } else {
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (float c : counts_) {
            vmin = std::min(vmin, c);
            vmax = std::max(vmax, c);
        }
        if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
        valueRange_ = {vmin, vmax};
    }
}

void HexbinPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    for (size_t i = 0; i < centers_.size(); ++i) {
        float t;
        if (config_.norm) {
            t = (*config_.norm)(counts_[i]);
        } else {
            t = (counts_[i] - valueRange_.min) / vspan;
        }
        Color color = cmap.sample(t);

        auto verts = hexVertices(centers_[i].x, centers_[i].y, hexRadius_);
        // Fan triangulation: center + 2 vertices per triangle.
        Point2D center = centers_[i];
        for (int k = 0; k < 6; ++k) {
            int k2 = (k + 1) % 6;
            fillPositions_.push_back(center);
            fillPositions_.push_back(verts[k]);
            fillPositions_.push_back(verts[k2]);
            for (int j = 0; j < 3; ++j) fillColors_.push_back(color);
        }
    }
}

void HexbinPlot::prepare(render::Renderer& r) {
    computeBins();
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

void HexbinPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void HexbinPlot::contributeToAutoscale(Viewport& v) const {
    // mpl's datalim covers the hex lattice: data range + half a cell.
    if (x_.empty()) return;
    float xMin = *std::ranges::min_element(x_);
    float xMax = *std::ranges::max_element(x_);
    float yMin = *std::ranges::min_element(y_);
    float yMax = *std::ranges::max_element(y_);
    int nx = std::max(1, config_.gridsize);
    int ny = std::max(1, int(nx / std::sqrt(3.0)));
    float sx = (xMax - xMin) / nx, sy = (yMax - yMin) / ny;
    v.x.min = std::min(v.x.min, xMin - sx * 0.5f);
    v.x.max = std::max(v.x.max, xMax + sx * 0.5f);
    v.y.min = std::min(v.y.min, yMin - sy / 3.0f);
    v.y.max = std::max(v.y.max, yMax + sy / 3.0f);
}

} // namespace volcano::plot
