// volcano/render/Renderer.hpp — top-level renderer orchestrating all primitives
#pragma once

#include <volcano/backend/Backend.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/render/GridRenderer.hpp>
#include <volcano/render/primitives/SpineRenderer.hpp>
#include <volcano/render/primitives/ReduceRenderer.hpp>
#include <volcano/text/TextRenderer.hpp>

#include <volcano/plot/Plot.hpp>

#include <vulkan/vulkan.hpp>

#include <memory>

namespace volcano::render {

class Renderer {
public:
    explicit Renderer(backend::IBackend& backend);
    ~Renderer();

    /// Prepare (upload GPU resources) for a figure.
    void prepare(plot::Figure& figure);

    /// Render one frame of the figure.
    void renderFrame(plot::Figure& figure);

    [[nodiscard]] backend::IBackend& backend() noexcept { return backend_; }
    [[nodiscard]] core::PipelineCache& pipelineCache() noexcept { return *pipelineCache_; }
    [[nodiscard]] core::DescriptorPool& descriptorPool() noexcept { return *descriptorPool_; }
    [[nodiscard]] text::TextRenderer& textRenderer() noexcept { return textRenderer_; }
    [[nodiscard]] primitives::ReduceRenderer& reduceRenderer() noexcept { return reduceRenderer_; }
    [[nodiscard]] primitives::SpineRenderer& spineRenderer() noexcept { return spineRenderer_; }

private:
    backend::IBackend& backend_;
    std::unique_ptr<core::PipelineCache> pipelineCache_;
    std::unique_ptr<core::DescriptorPool> descriptorPool_;
    GridRenderer gridRenderer_;
    primitives::SpineRenderer spineRenderer_;
    primitives::ReduceRenderer reduceRenderer_;
    text::TextRenderer textRenderer_;
    bool gridInited_ = false;
    bool textInited_ = false;
    bool spineInited_ = false;
    bool reduceInited_ = false;
    bool textReady_ = false;
    bool prepared_ = false;

    /// Draw axis labels, tick labels, and title for one axes.
    void drawText(vk::CommandBuffer cmd, const plot::Axes& axes,
                  plot::Rect2D rect);

    /// Draw axis spines (border lines) and tick marks for one axes.
    void drawSpines(vk::CommandBuffer cmd, const plot::Axes& axes,
                    plot::Rect2D rect);

    /// Draw a legend for the axes (if enabled in style).
    void drawLegend(vk::CommandBuffer cmd, const plot::Axes& axes,
                    plot::Rect2D rect);

    /// Draw a colorbar for the axes (if enabled in style).
    void drawColorbar(vk::CommandBuffer cmd, const plot::Axes& axes,
                      plot::Rect2D rect);
};

} // namespace volcano::render
