// volcano/cli/main_headless.cpp — headless render-to-file
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    using namespace volcano;
    std::string outFile = "volcano_headless.png";
    if (argc > 1) outFile = argv[1];

    backend::BackendDesc desc;
    desc.width = 1600;
    desc.height = 1200;
    desc.samples = vk::SampleCountFlagBits::e4;

    auto backend = backend::createHeadlessBackend(desc);
    if (!backend) { std::cerr << "Failed to create headless backend\n"; return 1; }

    render::Renderer renderer(*backend);

    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setStyle(plot::styles::ggplotStyle());
    axes->setTitle("VolcanoPlot Headless Export");

    plot::Series2D line;
    line.label = "cos(x)";
    line.color = plot::Color::fromRgba8(31, 119, 180);
    line.lineWidth = 2.0f;
    for (int i = 0; i <= 500; ++i) {
        float x = -6.28f + 12.56f * i / 500;
        line.points.push_back({x, std::cos(x)});
    }
    axes->addPlot(std::make_unique<plot::LinePlot>(std::move(line)));

    plot::Series2D scatter;
    scatter.label = "samples";
    scatter.color = plot::Color::fromRgba8(214, 39, 40);
    for (int i = 0; i < 100; ++i) {
        float x = -6.28f + 12.56f * i / 99;
        scatter.points.push_back({x, std::cos(x) + 0.3f * std::sin(5.0f * x)});
    }
    axes->addPlot(std::make_unique<plot::ScatterPlot>(std::move(scatter)));

    renderer.prepare(figure);
    renderer.renderFrame(figure);

    auto pixels = backend->readbackRgba8();
    auto encoder = encode::createGpuEncoder(encode::ImageFormat::Png,
                                            backend->context().device.handle(),
                                            backend->context().device.graphicsQueue(),
                                            backend->context().graphicsPool.handle(),
                                            backend->context().allocator.handle());
    if (encoder->encodeToFile(pixels, desc.width, desc.height, outFile)) {
        std::cout << "Wrote " << outFile << " (" << pixels.size() << " raw bytes)\n";
    } else {
        std::cerr << "Failed to encode image\n";
        return 1;
    }
    return 0;
}
