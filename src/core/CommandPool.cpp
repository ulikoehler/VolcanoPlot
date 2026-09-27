// volcano/core/CommandPool.cpp
#include "volcano/core/CommandPool.hpp"

namespace volcano::core {

CommandPool::CommandPool(vk::Device device, uint32_t queueFamily, vk::CommandPoolCreateFlags flags) {
    vk::CommandPoolCreateInfo ci{};
    ci.setFlags(flags)
       .setQueueFamilyIndex(queueFamily);
    pool_ = std::shared_ptr<vk::CommandPool>(
        new vk::CommandPool(device.createCommandPool(ci)),
        [device](vk::CommandPool* p) {
            if (*p) device.destroyCommandPool(*p);
            delete p;
        });
}

} // namespace volcano::core
