// volcano/render/Renderer.hpp — top-level renderer orchestrating all primitives
#pragma once

#include <volcano/backend/Backend.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

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

private:
    backend::IBackend& backend_;
    std::unique_ptr<core::PipelineCache> pipelineCache_;
    std::unique_ptr<core::DescriptorPool> descriptorPool_;
    bool prepared_ = false;
};

} // namespace volcano::render
