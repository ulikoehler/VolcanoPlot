// volcano/core/Allocator.cpp
#define VMA_IMPLEMENTATION
#include "volcano/core/Allocator.hpp"

namespace volcano::core {

Allocator::Allocator(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device) {
    VmaAllocatorCreateInfo ci{};
    ci.instance = instance;
    ci.physicalDevice = physical;
    ci.device = device;
    ci.vulkanApiVersion = VK_API_VERSION_1_3;
    allocator_ = std::shared_ptr<VmaAllocator>(
        new VmaAllocator(VK_NULL_HANDLE), [](VmaAllocator* a) {
            if (*a) vmaDestroyAllocator(*a);
            delete a;
        });
    vmaCreateAllocator(&ci, allocator_.get());
}

} // namespace volcano::core
