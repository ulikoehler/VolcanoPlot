// examples/headless_scatter.cpp — headless scatter plot to PNG
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <cmath>
#include <iostream>
#include <random>

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 1920; desc.height = 1080;
    desc.samples = vk::SampleCountFlagBits::e4;

    auto backend = backend::createHeadlessBackend(desc);
    render::Renderer renderer(*backend);

    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setStyle(plot::styles::seabornStyle());
    axes->setTitle("Headless Scatter Demo");
    axes->style().xAxis.label = "X Axis";
    axes->style().yAxis.label = "Y Axis";

    std::mt19937 rng(42);
    std::normal_distribution<float> dist(0, 1);

    plot::Series2D scatter;
    scatter.color = plot::Color::fromRgba8(31, 119, 180);
    scatter.marker = plot::MarkerStyle::Circle;
    scatter.size = 5.0f;
    for (int i = 0; i < 1000; ++i) {
        scatter.points.push_back({dist(rng), dist(rng)});
    }
    axes->addPlot(std::make_unique<plot::ScatterPlot>(std::move(scatter)));

    renderer.prepare(figure);
    renderer.renderFrame(figure);

    auto px = backend->readbackRgba8();
    auto enc = encode::createCpuEncoder(encode::ImageFormat::Png);
    if (enc->encodeToFile(px, desc.width, desc.height, "headless_scatter.png")) {
        std::cout << "Wrote headless_scatter.png\n";
    } else {
        std::cerr << "Encode failed\n"; return 1;
    }
    return 0;
}
