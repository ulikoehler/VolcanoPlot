// volcano/cli/main_screen.cpp — screen liveplot demo
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/Style.hpp>

#include <chrono>
#include <cmath>
#include <iostream>

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.windowTitle = "VolcanoPlot — Screen Liveplot";
    desc.samples = vk::SampleCountFlagBits::e4;
#ifdef NDEBUG
    desc.enableValidation = false;
#else
    desc.enableValidation = true;
#endif

    auto backend = backend::createScreenBackend(desc);
    if (!backend) { std::cerr << "Failed to create screen backend\n"; return 1; }

    render::Renderer renderer(*backend);

    // Build a figure with a sine wave line plot + scatter.
    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setStyle(plot::styles::ggplotStyle());
    axes->setTitle("VolcanoPlot Live Demo");

    // Sine wave line
    plot::Series2D lineSeries;
    lineSeries.label = "sin(x)";
    lineSeries.color = plot::Color::fromRgba8(31, 119, 180);
    lineSeries.lineWidth = 2.0f;
    for (int i = 0; i <= 1000; ++i) {
        float x = -10.0f + 20.0f * i / 1000;
        lineSeries.points.push_back({x, std::sin(x)});
    }
    axes->addPlot(std::make_unique<plot::LinePlot>(std::move(lineSeries)));

    // Scatter
    plot::Series2D scatterSeries;
    scatterSeries.label = "data";
    scatterSeries.color = plot::Color::fromRgba8(255, 127, 14);
    scatterSeries.marker = plot::MarkerStyle::Circle;
    scatterSeries.size = 8.0f;
    for (int i = 0; i < 50; ++i) {
        float x = -10.0f + 20.0f * i / 49;
        scatterSeries.points.push_back({x, std::sin(x) + 0.2f * std::cos(3.0f * x)});
    }
    axes->addPlot(std::make_unique<plot::ScatterPlot>(std::move(scatterSeries)));

    renderer.prepare(figure);

    while (backend->pollEvents()) {
        renderer.renderFrame(figure);
    }
    return 0;
}
