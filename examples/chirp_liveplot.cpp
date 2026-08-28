// examples/chirp_liveplot.cpp — ggplot-style chirp with infinite zoom (screen)
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/FunctionPlot.hpp>
#include <volcano/plot/Style.hpp>

#include <cmath>
#include <iostream>

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 1280; desc.height = 720;
    desc.windowTitle = "VolcanoPlot — Chirp Liveplot";
    desc.samples = vk::SampleCountFlagBits::e8;

    auto backend = backend::createScreenBackend(desc);
    render::Renderer renderer(*backend);

    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setStyle(plot::styles::ggplotStyle());
    axes->setTitle("Chirp: y = sin(2π(f₀t + ½kt²))");
    axes->addPlot(std::make_unique<plot::FunctionPlot>(
        "sin(6.28318 * (1.0 * x + 0.5 * 10.0 * x * x))",
        plot::Range{0, 10}, 2048,
        plot::Color::fromRgba8(31, 119, 180), 2.0f, "chirp"));

    renderer.prepare(figure);
    while (backend->pollEvents()) renderer.renderFrame(figure);
    return 0;
}
