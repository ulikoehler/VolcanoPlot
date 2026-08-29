// examples/showcase.cpp — single image demonstrating all rendering features
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <cmath>
#include <iostream>

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 1600; desc.height = 900;
    desc.samples = vk::SampleCountFlagBits::e4;

    auto backend = backend::createHeadlessBackend(desc);
    render::Renderer renderer(*backend);

    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setStyle(plot::styles::seabornStyle());

    // Title and axis labels
    axes->setTitle("VolcanoPlot Feature Showcase");
    axes->style().xAxis.label = "X Axis";
    axes->style().yAxis.label = "Y Axis";

    // Enable spines (axis border + tick marks)
    axes->style().xAxis.visible = true;
    axes->style().yAxis.visible = true;

    // Enable legend
    axes->style().legend.visible = true;

    // Enable colorbar with a z-range
    axes->style().colorbar.visible = true;
    axes->style().colorbar.colormap = "viridis";

    // Series 1: scatter (blue circles)
    plot::Series2D scatter;
    scatter.label = "Scatter Data";
    scatter.color = plot::Color::fromRgba8(31, 119, 180);
    scatter.marker = plot::MarkerStyle::Circle;
    scatter.size = 6.0f;
    for (int i = 0; i < 200; ++i) {
        float t = float(i) / 199.0f;
        float x = t * 10.0f;
        float y = std::sin(x * 2.0f) * 3.0f + 5.0f + (float(i % 7) - 3.0f) * 0.3f;
        scatter.points.push_back({x, y});
    }
    axes->addPlot(std::make_unique<plot::ScatterPlot>(std::move(scatter)));

    // Series 2: line (orange)
    plot::Series2D line;
    line.label = "Sine Wave";
    line.color = plot::Color::fromRgba8(255, 127, 14);
    line.lineWidth = 2.5f;
    for (int i = 0; i < 300; ++i) {
        float x = float(i) / 299.0f * 10.0f;
        float y = std::sin(x * 2.0f) * 3.0f + 5.0f;
        line.points.push_back({x, y});
    }
    axes->addPlot(std::make_unique<plot::LinePlot>(std::move(line)));

    // Series 3: line (green)
    plot::Series2D line2;
    line2.label = "Cosine Wave";
    line2.color = plot::Color::fromRgba8(44, 160, 44);
    line2.lineWidth = 2.5f;
    for (int i = 0; i < 300; ++i) {
        float x = float(i) / 299.0f * 10.0f;
        float y = std::cos(x * 2.0f) * 3.0f + 5.0f;
        line2.points.push_back({x, y});
    }
    axes->addPlot(std::make_unique<plot::LinePlot>(std::move(line2)));

    // Set viewport with a z-range for the colorbar
    axes->setViewport({0, 10, 0, 10, 0, 10});

    renderer.prepare(figure);
    renderer.renderFrame(figure);

    auto px = backend->readbackRgba8();
    auto enc = encode::createCpuEncoder(encode::ImageFormat::Png);
    if (enc->encodeToFile(px, desc.width, desc.height, "showcase.png")) {
        std::cout << "Wrote showcase.png\n";
    } else {
        std::cerr << "Encode failed\n";
        return 1;
    }
    return 0;
}
