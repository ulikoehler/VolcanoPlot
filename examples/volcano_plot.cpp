// examples/volcano_plot.cpp — genomics volcano plot (headless)
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/VolcanoPlot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <cmath>
#include <iostream>
#include <random>

int main() {
    using namespace volcano;
    backend::BackendDesc desc;
    desc.width = 1600; desc.height = 1200;
    desc.samples = vk::SampleCountFlagBits::e4;

    auto backend = backend::createHeadlessBackend(desc);
    render::Renderer renderer(*backend);

    plot::Figure figure(1, 1);
    auto* axes = figure.addAxes();
    axes->setStyle(plot::styles::ggplotStyle());
    axes->setTitle("Volcano Plot");
    axes->style().xAxis.label = "log2 Fold Change";
    axes->style().yAxis.label = "-log10 p-value";

    // Synthetic volcano data
    std::mt19937 rng(123);
    plot::VolcanoData vd;
    for (int i = 0; i < 5000; ++i) {
        float fc = (rng() % 1000 - 500) / 100.0f;
        float p = std::pow(10.0f, -(1.0f + (rng() % 400) / 100.0f));
        vd.log2FoldChange.push_back(fc);
        vd.pValue.push_back(p);
    }
    axes->addPlot(std::make_unique<plot::VolcanoPlot>(std::move(vd)));

    renderer.prepare(figure);
    renderer.renderFrame(figure);

    auto px = backend->readbackRgba8();
    auto enc = encode::createCpuEncoder(encode::ImageFormat::Png);
    enc->encodeToFile(px, desc.width, desc.height, "volcano_plot.png");
    std::cout << "Wrote volcano_plot.png\n";
    return 0;
}
