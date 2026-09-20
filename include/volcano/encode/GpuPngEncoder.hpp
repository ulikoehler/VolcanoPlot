// volcano/encode/GpuPngEncoder.hpp — GPU-side PNG encoder (compute shader)
#pragma once

#include "volcano/encode/ImageEncoder.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

namespace volcano::encode {

/// GPU PNG encoder. Performs adaptive PNG row filtering (None/Sub/Up/
/// Average/Paeth, min sum-of-absolute-differences per row) on the GPU via a
/// compute shader; DEFLATE (zlib) and container assembly run on the CPU.
class GpuPngEncoder : public IImageEncoder {
public:
    GpuPngEncoder(vk::Device device, vk::Queue queue, vk::CommandPool pool, VmaAllocator allocator);
    ~GpuPngEncoder() override;

    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override { return ImageFormat::Png; }

    /// GPU-filtered scanline stream: `h` rows of `1 filter byte + w*4 bytes`.
    /// Empty on failure. Used by the APNG writer so animation frames get
    /// GPU-side filtering too.
    [[nodiscard]] std::vector<uint8_t> filterScanlines(std::span<const uint8_t> rgba,
                                                       uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    vk::Device device_;
    vk::Queue queue_;
    vk::CommandPool pool_;
    VmaAllocator allocator_;
};

} // namespace volcano::encode
