// volcano/core/Allocator.hpp — VMA allocator wrapper
#pragma once

#include <vulkan/vulkan.hpp>

// VMA defines implement_stb in a single TU; we set it here.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.hpp>

#include <memory>

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
