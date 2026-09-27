// examples/bench_line.cpp — temporary: time native 10M-point line render
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <chrono>
#include <cstdio>
#include <random>

static double ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 800; desc.height = 600;
    desc.samples = vk::SampleCountFlagBits::e4;
    auto backend = backend::createHeadlessBackend(desc);
    render::Renderer renderer(*backend);

    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setTitle("bench");

    const size_t n = 10'000'000;
    plot::Series2D s;
    s.points.reserve(n);
    std::mt19937 rng(42);
    std::normal_distribution<float> d(0, 1);
    float y = 0;
    for (size_t i = 0; i < n; ++i) {
        y += d(rng);
        s.points.push_back({float(i) / float(n) * 100.f, y});
    }
    axes->addPlot(std::make_unique<plot::LinePlot>(std::move(s)));

    for (int i = 0; i < 3; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        renderer.prepare(figure);
        double tPrep = ms(t0);
        t0 = std::chrono::steady_clock::now();
        renderer.renderFrame(figure);
        double tFrame = ms(t0);
        encode::SaveOptions opt;
        t0 = std::chrono::steady_clock::now();
        bool ok = renderer.savefig(figure, "/tmp/bench_line.png", opt);
        double tSave = ms(t0);
        printf("rep%d: prepare=%.0f renderFrame=%.0f savefig=%.0f ok=%d\n",
               i, tPrep, tFrame, tSave, int(ok));
    }
    return 0;
}
