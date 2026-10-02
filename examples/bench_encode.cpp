// examples/bench_encode.cpp — image-encoder microbenchmark.
//
// Encodes fixed synthetic RGBA frames (a "plot-like" mostly-flat frame
// and a photographic noise gradient) through every available encoder,
// CPU vs GPU-hybrid, and prints median encode ms + output size.
// This isolates the encode stage from render/readback.
#include <volcano/backend/Backend.hpp>
#include <volcano/backend/HeadlessBackend.hpp>
#include <volcano/encode/ImageEncoder.hpp>
#include <volcano/encode/GpuPngEncoder.hpp>
#include <volcano/encode/GpuYuvEncoder.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace volcano;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0)
        .count();
}

static std::vector<uint8_t> framePlot(uint32_t w, uint32_t h) {
    // Mostly-white frame with a few smooth sinusoid bands — the
    // compression-friendly case typical of savefig output.
    std::vector<uint8_t> px(size_t(w) * h * 4, 255);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            float fx = float(x) / w, fy = float(y) / h;
            float s0 = std::abs(std::sin(fx * 25.0f) - fy);
            float s1 = std::abs(std::cos(fx * 17.0f) - fy);
            auto* p = px.data() + (size_t(y) * w + x) * 4;
            if (s0 < 0.003f) { p[0] = 200; p[1] = 40; p[2] = 40; }
            if (s1 < 0.003f) { p[0] = 40; p[1] = 60; p[2] = 200; }
            if (y < 40 || y + 40 > h || x < 50 || x + 20 > w) {
                p[0] = 230; p[1] = 230; p[2] = 230; // axes-like margins
            }
        }
    return px;
}

static std::vector<uint8_t> frameNoise(uint32_t w, uint32_t h) {
    // Pseudorandom gradient — the imshow-style hard-to-compress case.
    std::vector<uint8_t> px(size_t(w) * h * 4);
    std::mt19937 rng(7);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            auto* p = px.data() + (size_t(y) * w + x) * 4;
            p[0] = uint8_t((x * 255 / w + rng() % 32));
            p[1] = uint8_t((y * 255 / h + rng() % 32));
            p[2] = uint8_t(rng() % 256);
            p[3] = 255;
        }
    return px;
}

static void bench(const char* name, encode::IImageEncoder* enc,
                  const std::vector<uint8_t>& px, uint32_t w, uint32_t h,
                  int reps = 5) {
    if (!enc) { printf("%-22s n/a\n", name); return; }
    std::vector<double> ts;
    size_t sz = 0;
    for (int i = 0; i < reps + 1; ++i) {
        auto t0 = Clock::now();
        auto res = enc->encode(px, w, h);
        double dt = ms(t0);
        if (i == 0) continue; // warm-up
        if (!res.success) {
            printf("%-22s FAILED: %s\n", name, res.error.c_str());
            return;
        }
        ts.push_back(dt);
        sz = res.bytes.size();
    }
    std::sort(ts.begin(), ts.end());
    printf("%-22s %8.2f ms  %9.1f KB\n", name, ts[ts.size() / 2],
           sz / 1024.0);
}

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 64; desc.height = 64;
    auto backend = backend::createHeadlessBackend(desc);
    auto& ctx = static_cast<backend::HeadlessBackend&>(*backend).context();
    auto dev = ctx.device.handle();
    auto queue = ctx.device.graphicsQueue();
    auto pool = ctx.graphicsPool.handle();
    auto alloc = ctx.allocator.handle();

    struct Case { uint32_t w, h; const char* tag; };
    for (auto [w, h, tag] : {Case{640, 480, "640x480"},
                             Case{1920, 1440, "1920x1440"}}) {
        for (int kind = 0; kind < 2; ++kind) {
            auto px = kind == 0 ? framePlot(w, h) : frameNoise(w, h);
            printf("== %s %s ==\n", tag, kind == 0 ? "plot" : "noise");

            auto pngCpu = encode::createCpuEncoder(encode::ImageFormat::Png);
            bench("png cpu", pngCpu.get(), px, w, h);
            encode::GpuPngEncoder pngGpu(dev, queue, pool, alloc);
            bench("png gpu+level3", &pngGpu, px, w, h);
            pngGpu.setCompressionLevel(6);
            bench("png gpu+level6", &pngGpu, px, w, h);

            auto jpgCpu = encode::createCpuEncoder(encode::ImageFormat::Jpeg);
            bench("jpeg cpu", jpgCpu.get(), px, w, h);
#ifdef VOLCANO_GPU_ENCODE
            try {
                encode::GpuJpegEncoder jpgGpu(dev, queue, pool, alloc);
                bench("jpeg gpu-hybrid", &jpgGpu, px, w, h);
            } catch (...) {
                printf("%-22s n/a (pipeline)\n", "jpeg gpu-hybrid");
            }
#endif

            auto webpCpu =
                encode::createCpuEncoder(encode::ImageFormat::Webp);
            bench("webp cpu", webpCpu.get(), px, w, h);
#ifdef VOLCANO_GPU_ENCODE
            try {
                encode::GpuWebpEncoder webpGpu(dev, queue, pool, alloc);
                bench("webp gpu-hybrid", &webpGpu, px, w, h);
            } catch (...) {
                printf("%-22s n/a (pipeline)\n", "webp gpu-hybrid");
            }
#endif
        }
    }
    return 0;
}
