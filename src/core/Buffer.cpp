// volcano/core/Buffer.cpp
#include "volcano/core/Buffer.hpp"
#include "volcano/core/CommandBuffer.hpp"

#include <stdexcept>

namespace volcano::core {

namespace {

vk::BufferUsageFlags usageToFlags(BufferUsage u) {
    switch (u) {
        case BufferUsage::Vertex:   return vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        case BufferUsage::Index:    return vk::BufferUsageFlagBits::eIndexBuffer  | vk::BufferUsageFlagBits::eTransferDst;
        case BufferUsage::Storage:  return vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        case BufferUsage::Uniform:  return vk::BufferUsageFlagBits::eUniformBuffer;
        case BufferUsage::Staging:  return vk::BufferUsageFlagBits::eTransferSrc;
        case BufferUsage::Indirect: return vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst;
    }
    return {};
}

} // namespace

Buffer::Buffer(VmaAllocator allocator, const BufferDesc& desc)
    : allocator_(allocator), size_(desc.size) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = desc.size;
    bci.usage = static_cast<VkBufferUsageFlags>(usageToFlags(desc.usage));
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.hostVisible) {
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (desc.hostCached) aci.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }

    VkBuffer buf{};
    VmaAllocationInfo info{};
    VkResult res = vmaCreateBuffer(allocator_, &bci, &aci, &buf, &allocation_, &info);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateBuffer failed");
    }
    buffer_ = buf;
    mapped_ = info.pMappedData;
}

Buffer::~Buffer() {
    if (allocator_ && allocation_) vmaDestroyBuffer(allocator_, buffer_, allocation_);
}

Buffer::Buffer(Buffer&& o) noexcept
    : allocator_(o.allocator_), allocation_(o.allocation_), buffer_(o.buffer_),
      size_(o.size_), mapped_(o.mapped_) {
    o.allocator_ = VK_NULL_HANDLE; o.allocation_ = VK_NULL_HANDLE;
    o.buffer_ = VK_NULL_HANDLE; o.size_ = 0; o.mapped_ = nullptr;
}

Buffer& Buffer::operator=(Buffer&& o) noexcept {
    if (this != &o) {
        if (allocator_ && allocation_) vmaDestroyBuffer(allocator_, buffer_, allocation_);
        allocator_ = o.allocator_; allocation_ = o.allocation_; buffer_ = o.buffer_;
        size_ = o.size_; mapped_ = o.mapped_;
        o.allocator_ = VK_NULL_HANDLE; o.allocation_ = VK_NULL_HANDLE;
        o.buffer_ = VK_NULL_HANDLE; o.size_ = 0; o.mapped_ = nullptr;
    }
    return *this;
}

void Buffer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                    std::span<const std::byte> data) {
    if (mapped_) {
        std::memcpy(mapped_, data.data(), data.size());
        return;
    }
    // Staging upload
    BufferDesc stagingDesc{};
    stagingDesc.size = data.size();
    stagingDesc.usage = BufferUsage::Staging;
    stagingDesc.hostVisible = true;
    Buffer staging(allocator_, stagingDesc);
    std::memcpy(staging.mappedData(), data.data(), data.size());

    OneTimeCommands cmd(device, pool, queue);
    vk::BufferCopy region{};
    region.setSize(data.size());
    cmd.handle().copyBuffer(staging.handle(), buffer_, region);
}

} // namespace volcano::core
