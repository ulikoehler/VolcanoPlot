// volcano/render/Renderer.cpp
#include "volcano/render/Renderer.hpp"

namespace volcano::render {

Renderer::Renderer(backend::IBackend& backend) : backend_(backend) {
    auto& ctx = backend_.context();
    pipelineCache_ = std::make_unique<core::PipelineCache>(ctx.device.handle());
    std::vector<vk::DescriptorPoolSize> sizes = {
        { vk::DescriptorType::eUniformBuffer, 256 },
        { vk::DescriptorType::eStorageBuffer, 256 },
        { vk::DescriptorType::eCombinedImageSampler, 64 },
    };
    descriptorPool_ = std::make_unique<core::DescriptorPool>(ctx.device.handle(), sizes, 512,
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
}

Renderer::~Renderer() = default;

void Renderer::prepare(plot::Figure& figure) {
    for (auto& p : figure.placements()) {
        p.axes->autoscale();
        for (auto& plot : p.axes->plots()) {
            plot->prepare(*this);
        }
    }
    prepared_ = true;
}

void Renderer::renderFrame(plot::Figure& figure) {
    auto& ctx = backend_.context();
    auto ext = backend_.extent();
    figure.layout(plot::Extent2D{ext.width, ext.height});

    auto cmd = backend_.beginFrame();
    for (auto& p : figure.placements()) {
        plot::Rect2D rect = p.axes->rect;
        for (auto& plot : p.axes->plots()) {
            plot->draw(cmd, *this, *p.axes, rect);
        }
    }
    backend_.endFrame();
}

} // namespace volcano::render
