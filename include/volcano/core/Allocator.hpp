// volcano/core/Allocator.hpp — VMA allocator wrapper
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace volcano::core {

class Device;

class Allocator {
public:
    Allocator() = default;
    Allocator(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device);
    ~Allocator();

    Allocator(Allocator&&) noexcept = default;
    Allocator& operator=(Allocator&&) noexcept = default;
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    [[nodiscard]] VmaAllocator handle() const noexcept { return allocator_; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

} // namespace volcano::core
