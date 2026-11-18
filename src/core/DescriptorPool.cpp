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

std::mutex& DescriptorPool::opMutex() {
    static std::mutex mu;
    return mu;
}

vk::DescriptorSet DescriptorPool::allocate(vk::DescriptorSetLayout layout) {
    vk::DescriptorSetAllocateInfo ai{};
    ai.setDescriptorPool(pool_.get())
       .setSetLayouts(layout);
    std::lock_guard lock(opMutex());
    auto sets = device_.allocateDescriptorSets(ai);
    return sets.front();
}

void DescriptorPool::free(vk::DescriptorSet set) {
    if (!set) return;
    std::lock_guard lock(opMutex());
    device_.freeDescriptorSets(pool_.get(), set);
}

} // namespace volcano::core
