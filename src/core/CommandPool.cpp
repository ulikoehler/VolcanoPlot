// volcano/core/CommandPool.cpp
#include "volcano/core/CommandPool.hpp"

namespace volcano::core {

CommandPool::CommandPool(vk::Device device, uint32_t queueFamily, vk::CommandPoolCreateFlags flags) {
    vk::CommandPoolCreateInfo ci{};
    ci.setFlags(flags)
       .setQueueFamilyIndex(queueFamily);
    pool_ = device.createCommandPoolUnique(ci);
}

CommandPool::~CommandPool() = default;

} // namespace volcano::core
