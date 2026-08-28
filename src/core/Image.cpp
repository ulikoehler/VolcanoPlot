// volcano/core/Image.cpp
#include "volcano/core/Image.hpp"
#include "volcano/core/CommandBuffer.hpp"

#include <stdexcept>

namespace volcano::core {

Image::Image(VmaAllocator allocator, const ImageDesc& desc)
    : allocator_(allocator), format_(desc.format), extent_(desc.extent) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = static_cast<VkFormat>(desc.format);
    ici.extent = {desc.extent.width, desc.extent.height, 1};
    ici.mipLevels = desc.mipLevels;
    ici.arrayLayers = desc.arrayLayers;
    ici.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
    ici.tiling = static_cast<VkImageTiling>(desc.tiling);
    ici.usage = static_cast<VkImageUsageFlags>(desc.usage);
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.hostVisible) {
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkImage img{};
    VmaAllocationInfo info{};
    VkResult res = vmaCreateImage(allocator_, &ici, &aci, &img, &allocation_, &info);
    if (res != VK_SUCCESS) throw std::runtime_error("vmaCreateImage failed");
    image_ = img;
    mapped_ = info.pMappedData;
}

Image::~Image() {
    if (allocator_ && allocation_) vmaDestroyImage(allocator_, image_, allocation_);
}

Image::Image(Image&& o) noexcept
    : allocator_(o.allocator_), allocation_(o.allocation_), image_(o.image_),
      format_(o.format_), extent_(o.extent_), mapped_(o.mapped_) {
    o.allocator_ = VK_NULL_HANDLE; o.allocation_ = VK_NULL_HANDLE;
    o.image_ = VK_NULL_HANDLE; o.format_ = vk::Format::eUndefined;
    o.extent_ = vk::Extent2D{0,0}; o.mapped_ = nullptr;
}

Image& Image::operator=(Image&& o) noexcept {
    if (this != &o) {
        if (allocator_ && allocation_) vmaDestroyImage(allocator_, image_, allocation_);
        allocator_ = o.allocator_; allocation_ = o.allocation_; image_ = o.image_;
        format_ = o.format_; extent_ = o.extent_; mapped_ = o.mapped_;
        o.allocator_ = VK_NULL_HANDLE; o.allocation_ = VK_NULL_HANDLE;
        o.image_ = VK_NULL_HANDLE; o.format_ = vk::Format::eUndefined;
        o.extent_ = vk::Extent2D{0,0}; o.mapped_ = nullptr;
    }
    return *this;
}

void Image::transitionLayout(vk::CommandBuffer cmd, vk::Image image,
                             vk::Format /*format*/, vk::ImageLayout oldLayout,
                             vk::ImageLayout newLayout, vk::ImageAspectFlags aspect,
                             uint32_t mipLevels, uint32_t arrayLayers) {
    vk::ImageMemoryBarrier barrier{};
    barrier.setOldLayout(oldLayout)
           .setNewLayout(newLayout)
           .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
           .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
           .setImage(image)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(aspect)
               .setBaseMipLevel(0).setLevelCount(mipLevels)
               .setBaseArrayLayer(0).setLayerCount(arrayLayers));

    vk::PipelineStageFlags srcStage, dstStage;
    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.setSrcAccessMask({}).setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite).setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eGeneral) {
        barrier.setSrcAccessMask({}).setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eComputeShader;
    } else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::eTransferSrcOptimal) {
        barrier.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite).setDstAccessMask(vk::AccessFlagBits::eTransferRead);
        srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else {
        barrier.setSrcAccessMask(vk::AccessFlagBits::eNone).setDstAccessMask(vk::AccessFlagBits::eNone);
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    }
    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);
}

} // namespace volcano::core
