// volcano/core/Buffer.hpp — VMA-backed GPU buffer
#pragma once

#include <volcano/core/Allocator.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <cstddef>
#include <span>

namespace volcano::core {

class Allocator;

enum class BufferUsage {
    Vertex,
    Index,
    Storage,
    Uniform,
    Staging,
    Indirect,
};

struct BufferDesc {
    vk::DeviceSize size = 0;
    BufferUsage usage = BufferUsage::Storage;
    /// If true, memory is HOST_VISIBLE and mapped persistently.
    bool hostVisible = false;
    /// If true, create with HOST_COHERENT + HOST_CACHED (for readback).
    bool hostCached = false;
};

/// VMA-backed buffer. Owns its allocation.
class Buffer {
public:
    Buffer() = default;
    Buffer(VmaAllocator allocator, const BufferDesc& desc);
    ~Buffer();

    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] vk::Buffer handle() const noexcept { return buffer_; }
    [[nodiscard]] vk::DeviceSize size() const noexcept { return size_; }
    [[nodiscard]] void* mappedData() const noexcept { return mapped_; }

    /// Upload data from a CPU span. Requires staging if not host-visible.
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool, std::span<const std::byte> data);

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    vk::Buffer buffer_ = VK_NULL_HANDLE;
    vk::DeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace volcano::core
