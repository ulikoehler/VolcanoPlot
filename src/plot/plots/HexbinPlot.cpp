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
        // Pointy-top: vertices at 30, 90, 150, 210, 270, 330 degrees.
        for (int i = 0; i < 6; ++i) {
            float angle = (30.0f + 60.0f * i) * static_cast<float>(M_PI) / 180.0f;
            verts[i] = {cx + r * std::cos(angle), cy + r * std::sin(angle)};
        }
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
    if (x_.empty()) {
        centers_.clear();
        counts_.clear();
        return;
    }

    // Determine data range.
    xMin_ = *std::ranges::min_element(x_);
    xMax_ = *std::ranges::max_element(x_);
    yMin_ = *std::ranges::min_element(y_);
    yMax_ = *std::ranges::max_element(y_);
    if (xMax_ <= xMin_) xMax_ = xMin_ + 1.0f;
    if (yMax_ <= yMin_) yMax_ = yMin_ + 1.0f;

    float xSpan = xMax_ - xMin_;
    float ySpan = yMax_ - yMin_;

    // Compute hex size.
    // For pointy-top: hex width = sqrt(3) * r, hex height = 2 * r.
    // Horizontal spacing = sqrt(3) * r, vertical spacing = 1.5 * r.
    // For flat-top: hex width = 2 * r, hex height = sqrt(3) * r.
    // Horizontal spacing = 1.5 * r, vertical spacing = sqrt(3) * r.
    // gridsize = number of hexes along x.
    int gs = std::max(1, config_.gridsize);
    if (config_.orientation == HexOrientation::PointyTop) {
        float hexWidth = xSpan / gs;
        hexRadius_ = hexWidth / std::sqrt(3.0f);
    } else {
        float hexWidth = xSpan / gs;
        hexRadius_ = hexWidth / 2.0f;
    }

    // Bin points into hex cells using axial coordinates.
    std::unordered_map<std::pair<int,int>, int, IntPairHash> bins;

    for (size_t k = 0; k < x_.size(); ++k) {
        float px = x_[k] - xMin_;
        float py = y_[k] - yMin_;

        int q, r;
        if (config_.orientation == HexOrientation::PointyTop) {
            // Pointy-top axial coordinates.
            // q = (sqrt(3)/3 * x - 1/3 * y) / r
            // r = (2/3 * y) / r
            float qf = (std::sqrt(3.0f) / 3.0f * px - 1.0f / 3.0f * py) / hexRadius_;
            float rf = (2.0f / 3.0f * py) / hexRadius_;
            // Round to nearest hex.
            q = static_cast<int>(std::round(qf));
            r = static_cast<int>(std::round(rf));
        } else {
            // Flat-top axial coordinates.
            // q = (2/3 * x) / r
            // r = (-1/3 * x + sqrt(3)/3 * y) / r
            float qf = (2.0f / 3.0f * px) / hexRadius_;
            float rf = (-1.0f / 3.0f * px + std::sqrt(3.0f) / 3.0f * py) / hexRadius_;
            q = static_cast<int>(std::round(qf));
            r = static_cast<int>(std::round(rf));
        }
        bins[{q, r}]++;
    }

    // Convert axial coordinates to pixel centers.
    centers_.clear();
    counts_.clear();
    for (const auto& [key, count] : bins) {
        if (count < config_.minCount) continue;
        int q = key.first;
        int r = key.second;
        float cx, cy;
        if (config_.orientation == HexOrientation::PointyTop) {
            cx = xMin_ + hexRadius_ * (std::sqrt(3.0f) * q + std::sqrt(3.0f) / 2.0f * r);
            cy = yMin_ + hexRadius_ * 1.5f * r;
        } else {
            cx = xMin_ + hexRadius_ * 1.5f * q;
            cy = yMin_ + hexRadius_ * (std::sqrt(3.0f) / 2.0f * q + std::sqrt(3.0f) * r);
        }
        centers_.push_back({cx, cy});
        counts_.push_back(static_cast<float>(count));
    }

    // Apply normalization.
    if (config_.norm == HexbinNorm::Density) {
        float total = static_cast<float>(x_.size());
        if (total > 0) {
            for (auto& c : counts_) c /= total;
        }
    }

    // Compute value range.
    if (config_.valueRange.valid()) {
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
        float t = (counts_[i] - valueRange_.min) / vspan;
        t = std::clamp(t, 0.0f, 1.0f);
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
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void HexbinPlot::contributeToAutoscale(Viewport& v) const {
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
