// tests/PlotTestHarness.hpp — regression test utilities for rendered plots
//
// Provides a PlotTestHarness that renders a Figure headlessly and reads back
// the RGBA8 framebuffer for pixel-level verification. Includes analysis
// helpers (pixel sampling, region statistics, color matching, connected
// components, centroid, bounding box) so tests can assert on the actual
// rendered output rather than just "it didn't crash".
//
// Design principles for crafted-plot tests:
//   * Use a small canvas (e.g. 256x256) so each pixel is meaningful and the
//     test runs fast.
//   * Use a flat (no-grid) style with a known background color so the
//     background is a single constant color across the whole axes rect.
//   * Place data at known data coordinates and convert to expected pixel
//     positions using the same transform the renderer uses, so assertions
//     can target exact pixels.
//   * Use saturated, distinct colors (pure red, pure green, pure blue) so
//     color-matching is robust against minor blending differences.
//   * Disable MSAA (samples = e1) where possible to make pixel assertions
//     deterministic; enable MSAA only in dedicated anti-aliasing tests.
#pragma once

#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace volcano::test {

/// A single RGBA8 pixel.
struct Pixel {
    uint8_t r = 0, g = 0, b = 0, a = 0;

    bool operator==(const Pixel& o) const = default;
    bool operator!=(const Pixel& o) const = default;

    static constexpr Pixel white()   { return {255, 255, 255, 255}; }
    static constexpr Pixel black()   { return {0, 0, 0, 255}; }
    static constexpr Pixel red()     { return {255, 0, 0, 255}; }
    static constexpr Pixel green()   { return {0, 255, 0, 255}; }
    static constexpr Pixel blue()    { return {0, 0, 255, 255}; }
    static constexpr Pixel transparent() { return {0, 0, 0, 0}; }

    /// Color distance squared (0..195075).
    [[nodiscard]] uint32_t distSq(const Pixel& o) const {
        int dr = int(r) - int(o.r);
        int dg = int(g) - int(o.g);
        int db = int(b) - int(o.b);
        int da = int(a) - int(o.a);
        return uint32_t(dr*dr + dg*dg + db*db + da*da);
    }

    /// True if this pixel is within `tolerance` (per-channel) of `o`.
    [[nodiscard]] bool approx(const Pixel& o, int tolerance) const {
        return std::abs(int(r) - int(o.r)) <= tolerance &&
               std::abs(int(g) - int(o.g)) <= tolerance &&
               std::abs(int(b) - int(o.b)) <= tolerance &&
               std::abs(int(a) - int(o.a)) <= tolerance;
    }
};

/// 2D image buffer (RGBA8, row-major, tightly packed).
class Image {
public:
    Image() = default;
    Image(uint32_t w, uint32_t h)
        : width_(w), height_(h), data_(size_t(w) * h * 4, 0) {}

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::span<const uint8_t> raw() const noexcept { return data_; }

    /// Construct from a readback byte span (RGBA8, row-major).
    static Image fromRgba8(std::span<const uint8_t> bytes, uint32_t w, uint32_t h) {
        Image img(w, h);
        std::copy_n(bytes.data(), std::min(bytes.size(), img.data_.size()),
                    img.data_.begin());
        return img;
    }

    /// Get pixel at (x, y). Out-of-bounds returns transparent black.
    [[nodiscard]] Pixel get(uint32_t x, uint32_t y) const {
        if (x >= width_ || y >= height_) return {};
        const size_t idx = (size_t(y) * width_ + x) * 4;
        return {data_[idx], data_[idx+1], data_[idx+2], data_[idx+3]};
    }

    /// Set pixel at (x, y).
    void set(uint32_t x, uint32_t y, Pixel p) {
        if (x >= width_ || y >= height_) return;
        const size_t idx = (size_t(y) * width_ + x) * 4;
        data_[idx] = p.r; data_[idx+1] = p.g;
        data_[idx+2] = p.b; data_[idx+3] = p.a;
    }

    /// Count pixels matching `target` within `tolerance` across the whole image.
    [[nodiscard]] size_t countColor(Pixel target, int tolerance = 0) const {
        size_t count = 0;
        for (uint32_t y = 0; y < height_; ++y)
            for (uint32_t x = 0; x < width_; ++x)
                if (get(x, y).approx(target, tolerance)) ++count;
        return count;
    }

    /// Count pixels matching `target` within a rectangular region.
    [[nodiscard]] size_t countColorInRegion(Pixel target,
                                            uint32_t x0, uint32_t y0,
                                            uint32_t x1, uint32_t y1,
                                            int tolerance = 0) const {
        size_t count = 0;
        for (uint32_t y = y0; y < std::min(y1, height_); ++y)
            for (uint32_t x = x0; x < std::min(x1, width_); ++x)
                if (get(x, y).approx(target, tolerance)) ++count;
        return count;
    }

    /// Count pixels matching a predicate.
    [[nodiscard]] size_t countIf(const std::function<bool(uint32_t, uint32_t, Pixel)>& pred) const {
        size_t count = 0;
        for (uint32_t y = 0; y < height_; ++y)
            for (uint32_t x = 0; x < width_; ++x)
                if (pred(x, y, get(x, y))) ++count;
        return count;
    }

    /// Average color over a region.
    [[nodiscard]] Pixel averageRegion(uint32_t x0, uint32_t y0,
                                      uint32_t x1, uint32_t y1) const {
        uint64_t r = 0, g = 0, b = 0, a = 0;
        size_t n = 0;
        for (uint32_t y = y0; y < std::min(y1, height_); ++y)
            for (uint32_t x = x0; x < std::min(x1, width_); ++x) {
                Pixel p = get(x, y);
                r += p.r; g += p.g; b += p.b; a += p.a; ++n;
            }
        if (n == 0) return {};
        return {uint8_t(r/n), uint8_t(g/n), uint8_t(b/n), uint8_t(a/n)};
    }

    /// Bounding box of all pixels matching `target` within `tolerance`.
    struct BBox { uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool found = false; };
    [[nodiscard]] BBox boundingBox(Pixel target, int tolerance = 0) const {
        BBox bb;
        for (uint32_t y = 0; y < height_; ++y)
            for (uint32_t x = 0; x < width_; ++x)
                if (get(x, y).approx(target, tolerance)) {
                    if (!bb.found) { bb.x0 = bb.x1 = x; bb.y0 = bb.y1 = y; bb.found = true; }
                    else {
                        bb.x0 = std::min(bb.x0, x); bb.x1 = std::max(bb.x1, x);
                        bb.y0 = std::min(bb.y0, y); bb.y1 = std::max(bb.y1, y);
                    }
                }
        return bb;
    }

    /// Centroid (mean x, y) of all pixels matching `target` within `tolerance`.
    struct Centroid { double x = 0, y = 0; size_t count = 0; };
    [[nodiscard]] Centroid centroid(Pixel target, int tolerance = 0) const {
        Centroid c;
        for (uint32_t y = 0; y < height_; ++y)
            for (uint32_t x = 0; x < width_; ++x)
                if (get(x, y).approx(target, tolerance)) {
                    c.x += x; c.y += y; ++c.count;
                }
        if (c.count > 0) { c.x /= c.count; c.y /= c.count; }
        return c;
    }

    /// Save as PPM (for debugging test failures). Returns true on success.
    [[nodiscard]] bool savePpm(const std::string& path) const;

    /// Save as PNG if a PNG encoder is available, else PPM.
    [[nodiscard]] bool save(const std::string& path) const;

private:
    uint32_t width_ = 0, height_ = 0;
    std::vector<uint8_t> data_;
};

/// The main test harness: renders a Figure headlessly and provides the
/// resulting Image for analysis.
class PlotTestHarness {
public:
    /// Construct a harness with a canvas of the given size and sample count.
    /// Use samples = vk::SampleCountFlagBits::e1 for deterministic pixel tests.
    explicit PlotTestHarness(uint32_t width = 256, uint32_t height = 256,
                             vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);

    /// Access the underlying renderer to prepare plots.
    [[nodiscard]] render::Renderer& renderer() noexcept { return *renderer_; }
    /// Access the backend.
    [[nodiscard]] backend::IBackend& backend() noexcept { return *backend_; }

    /// Prepare and render a figure, returning the readback image.
    [[nodiscard]] Image render(plot::Figure& figure);

    /// Convert a data coordinate to a pixel coordinate within an axes rect,
    /// using the same linear transform the renderer uses.
    /// (For log axes, apply log yourself before calling this.)
    static std::pair<float, float> dataToPixel(const plot::Axes& axes,
                                               const plot::Rect2D& rect,
                                               float dataX, float dataY) {
        const auto& v = axes.viewport();
        float nx = (dataX - v.x.min) / v.x.span();       // [0,1]
        float ny = (dataY - v.y.min) / v.y.span();       // [0,1]
        // The renderer maps [0,1] -> [rect.x, rect.x + rect.width] for x,
        // and [0,1] -> [rect.y, rect.y + rect.height] for y (top-down).
        float px = rect.x + nx * rect.width;
        float py = rect.y + ny * rect.height;
        return {px, py};
    }

private:
    std::unique_ptr<backend::IBackend> backend_;
    std::unique_ptr<render::Renderer> renderer_;
};

/// Create a flat test style: white background, no grid, black axes.
/// This makes the background a single constant color and removes grid lines
/// that would interfere with pixel assertions.
inline plot::FigureStyle flatTestStyle() {
    plot::FigureStyle s;
    s.faceColor = plot::Color::white();
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.xAxis.visible = false;
    s.yAxis.visible = false;
    return s;
}

/// Create a flat test style with a specific background color.
inline plot::FigureStyle flatTestStyle(plot::Color bg) {
    plot::FigureStyle s;
    s.faceColor = bg;
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.xAxis.visible = false;
    s.yAxis.visible = false;
    return s;
}

// ─── GTest assertion helpers ──────────────────────────────────────────────

/// EXPECT that `img` has at least `minCount` pixels approximating `target`.
#define EXPECT_PIXEL_COUNT(img, target, minCount, tolerance) \
    EXPECT_GE((img).countColor((target), (tolerance)), (minCount)) \
        << "Expected at least " << (minCount) << " pixels matching " \
        << #target << " (tolerance " << (tolerance) << ")"

/// EXPECT that a specific pixel approximates `target`.
#define EXPECT_PIXEL_AT(img, x, y, target, tolerance) \
    EXPECT_TRUE((img).get((x), (y)).approx((target), (tolerance))) \
        << "Pixel at (" << (x) << "," << (y) << ") is " \
        << int((img).get((x),(y)).r) << "," << int((img).get((x),(y)).g) \
        << "," << int((img).get((x),(y)).b) << "," << int((img).get((x),(y)).a) \
        << " expected ~" << #target

/// EXPECT that a region is approximately uniform and matches `target`.
#define EXPECT_REGION_UNIFORM(img, x0, y0, x1, y1, target, tolerance) \
    do { \
        auto _img = (img); auto _t = (target); auto _tol = (tolerance); \
        size_t _bad = 0; \
        for (uint32_t _y = (y0); _y < (y1); ++_y) \
            for (uint32_t _x = (x0); _x < (x1); ++_x) \
                if (!_img.get(_x, _y).approx(_t, _tol)) ++_bad; \
        EXPECT_EQ(_bad, 0u) << "Region [" << (x0) << "," << (y0) \
            << ")-(" << (x1) << "," << (y1) << ") has " << _bad \
            << " pixels not matching " << #target; \
    } while (0)

/// EXPECT that the whole image is fully opaque (alpha == 255 everywhere).
#define EXPECT_FULLY_OPAQUE(img) \
    EXPECT_EQ((img).countIf([](uint32_t, uint32_t, Pixel p){ return p.a == 255; }), \
              size_t((img).width()) * (img).height()) \
        << "Image has transparent pixels"

} // namespace volcano::test
