// volcano/core/Image.hpp — VMA-backed GPU image
#pragma once

#include <volcano/core/Allocator.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

namespace volcano::core {

struct ImageDesc {
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    vk::Extent2D extent{0, 0};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    vk::ImageUsageFlags usage;
    bool hostVisible = false;
};

class Image {
public:
    Image() = default;
    Image(VmaAllocator allocator, const ImageDesc& desc);
    ~Image();

    Image(Image&&) noexcept;
    Image& operator=(Image&&) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    [[nodiscard]] vk::Image handle() const noexcept { return image_; }
    [[nodiscard]] vk::Format format() const noexcept { return format_; }
    [[nodiscard]] vk::Extent2D extent() const noexcept { return extent_; }
    [[nodiscard]] void* mappedData() const noexcept { return mapped_; }

    /// Transition image layout (records a barrier into the given command buffer).
    static void transitionLayout(vk::CommandBuffer cmd, vk::Image image,
                                 vk::Format format, vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout,
                                 vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
                                 uint32_t mipLevels = 1, uint32_t arrayLayers = 1);

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    vk::Image image_ = VK_NULL_HANDLE;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent2D extent_{0, 0};
    void* mapped_ = nullptr;
};

} // namespace volcano::core
