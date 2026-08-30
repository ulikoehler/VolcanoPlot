// volcano/plot/plots/XCorrPlot.cpp — autocorrelation / cross-correlation implementation
#include "volcano/plot/plots/XCorrPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace volcano::plot {

XCorrPlot::XCorrPlot(std::vector<float> x, XCorrConfig config)
    : x_(std::move(x)), isAuto_(true), config_(std::move(config)) {}

XCorrPlot::XCorrPlot(std::vector<float> x, std::vector<float> y,
                     XCorrConfig config)
    : x_(std::move(x)), y_(std::move(y)), isAuto_(false), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("XCorrPlot: x and y must have the same size");
}

void XCorrPlot::computeCorrelation() {
    lags_.clear();
    values_.clear();

    size_t n = x_.size();
    if (n == 0) return;

    // Determine max lags.
    uint32_t maxLag = config_.maxLags;
    if (maxLag == 0) maxLag = static_cast<uint32_t>(n - 1);
    maxLag = std::min(maxLag, static_cast<uint32_t>(n - 1));

    // Compute means.
    double xMean = std::accumulate(x_.begin(), x_.end(), 0.0) / n;
    double yMean = isAuto_ ? xMean :
        std::accumulate(y_.begin(), y_.end(), 0.0) / n;

    // Compute variances for normalization.
    double xVar = 0.0, yVar = 0.0;
    for (size_t i = 0; i < n; ++i) {
        xVar += (x_[i] - xMean) * (x_[i] - xMean);
        if (isAuto_)
            yVar += (x_[i] - xMean) * (x_[i] - xMean);
        else
            yVar += (y_[i] - yMean) * (y_[i] - yMean);
    }
    double norm = std::sqrt(xVar * yVar);
    if (norm < 1e-30) norm = 1.0;

    // Compute correlation at each lag from -maxLag to +maxLag.
    for (int lag = -static_cast<int>(maxLag); lag <= static_cast<int>(maxLag); ++lag) {
        double corr = 0.0;
        // r[lag] = sum over i of (x[i] - xMean) * (y[i+lag] - yMean)
        // where i+lag must be in [0, n-1].
        int startI = std::max(0, -lag);
        int endI = static_cast<int>(n) - 1 - std::max(0, lag);
        for (int i = startI; i <= endI; ++i) {
            int j = i + lag;
            float xv = x_[i] - static_cast<float>(xMean);
            float yv = isAuto_ ? (x_[j] - static_cast<float>(xMean))
                              : (y_[j] - static_cast<float>(yMean));
            corr += xv * yv;
        }
        if (config_.normed) corr /= norm;

        lags_.push_back(static_cast<float>(lag));
        values_.push_back(static_cast<float>(corr));
    }
}

void XCorrPlot::prepare(render::Renderer& r) {
    computeCorrelation();
    auto& ctx = r.backend().context();

    // Build stem segments: vertical line from (lag, 0) to (lag, value).
    stemSegments_.clear();
    markerPoints_.clear();
    markerColors_.clear();
    markerSizes_.clear();

    for (size_t i = 0; i < lags_.size(); ++i) {
        Point2D base{lags_[i], 0.0f};
        Point2D top{lags_[i], values_[i]};
        stemSegments_.push_back(base);
        stemSegments_.push_back(top);

        if (config_.markers) {
            markerPoints_.push_back(top);
            markerColors_.push_back(config_.color);
            markerSizes_.push_back(config_.markerSize);
        }
    }

    stemRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!stemSegments_.empty()) {
        stemRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{stemSegments_}, config_.color,
                             config_.lineWidth);
    }

    if (config_.markers && !markerPoints_.empty()) {
        markerRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                             r.backend().sampleCount(), r.descriptorPool(),
                             r.pipelineCache());
        markerRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                                ctx.graphicsPool.handle(), ctx.allocator.handle(),
                                std::span{markerPoints_}, std::span{markerColors_},
                                std::span{markerSizes_});
    }

    prepared_ = true;
}

void XCorrPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_ || stemSegments_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    stemRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(stemSegments_.size()));

    if (config_.markers && !markerPoints_.empty()) {
        markerRenderer_.draw(cmd, vrect, t,
                             static_cast<uint32_t>(markerPoints_.size()));
    }
}

void XCorrPlot::contributeToAutoscale(Viewport& v) const {
    if (lags_.empty()) return;
    for (float lag : lags_) {
        v.x.min = std::min(v.x.min, lag);
        v.x.max = std::max(v.x.max, lag);
    }
    v.y.min = std::min(v.y.min, 0.0f);
    v.y.max = std::max(v.y.max, 0.0f);
    for (float val : values_) {
        v.y.min = std::min(v.y.min, val);
        v.y.max = std::max(v.y.max, val);
    }
}

} // namespace volcano::plot
