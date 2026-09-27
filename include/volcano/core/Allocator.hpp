// volcano/core/Allocator.hpp — VMA allocator wrapper
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <memory>

namespace volcano::core {

class Device;

/// Owns a VmaAllocator; share() produces a handle kept alive by shared
/// ownership — the last Allocator destroys the VMA allocator.
class Allocator {
public:
    Allocator() = default;
    Allocator(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device);
    ~Allocator() = default;

    Allocator(Allocator&&) noexcept = default;
    Allocator& operator=(Allocator&&) noexcept = default;
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    /// A second Allocator sharing this one's VmaAllocator; the
    /// underlying object dies with the last holder.
    [[nodiscard]] Allocator share() const {
        Allocator s;
        s.allocator_ = allocator_;
        return s;
    }

    [[nodiscard]] VmaAllocator handle() const noexcept {
        return allocator_ ? *allocator_ : VK_NULL_HANDLE;
    }

private:
    std::shared_ptr<VmaAllocator> allocator_;
};

} // namespace volcano::core
