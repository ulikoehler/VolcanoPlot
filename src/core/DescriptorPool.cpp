// volcano/core/DescriptorPool.cpp
#include "volcano/core/DescriptorPool.hpp"

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
    auto sets = device_.allocateDescriptorSets(ai);
    return sets.front();
}

} // namespace volcano::core
