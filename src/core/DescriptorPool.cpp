// volcano/core/DescriptorPool.cpp
#include "volcano/core/DescriptorPool.hpp"

#include <mutex>
#include <stdexcept>

namespace volcano::core {

DescriptorPool::DescriptorPool(vk::Device device, const std::vector<vk::DescriptorPoolSize>& sizes,
                               uint32_t maxSets, vk::DescriptorPoolCreateFlags flags)
    : device_(device) {
    vk::DescriptorPoolCreateInfo ci{};
    ci.setFlags(flags)
       .setMaxSets(maxSets)
       .setPoolSizes(sizes);
    pool_ = device.createDescriptorPoolUnique(ci);
}

DescriptorPool::~DescriptorPool() = default;

vk::DescriptorSet DescriptorPool::allocate(vk::DescriptorSetLayout layout) {
    vk::DescriptorSetAllocateInfo ai{};
    ai.setDescriptorPool(pool_.get())
       .setSetLayouts(layout);
    // Descriptor pools require external synchronization (primitive
    // renderers init in parallel in Renderer::prepare).
    static std::mutex allocMu;
    std::lock_guard lock(allocMu);
    auto sets = device_.allocateDescriptorSets(ai);
    return sets.front();
}

} // namespace volcano::core
