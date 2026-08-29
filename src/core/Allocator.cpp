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
    vmaCreateAllocator(&ci, &allocator_);
}

Allocator::~Allocator() {
    if (allocator_) vmaDestroyAllocator(allocator_);
}

} // namespace volcano::core
