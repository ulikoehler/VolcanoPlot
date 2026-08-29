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

    Allocator(Allocator&& o) noexcept : allocator_(o.allocator_) { o.allocator_ = VK_NULL_HANDLE; }
    Allocator& operator=(Allocator&& o) noexcept {
        if (this != &o) {
            if (allocator_) vmaDestroyAllocator(allocator_);
            allocator_ = o.allocator_;
            o.allocator_ = VK_NULL_HANDLE;
        }
        return *this;
    }
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    [[nodiscard]] VmaAllocator handle() const noexcept { return allocator_; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

} // namespace volcano::core
