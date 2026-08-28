// volcano/encode/GpuPngEncoder.hpp — GPU-side PNG encoder (compute shader)
#pragma once

#include "volcano/encode/ImageEncoder.hpp"

namespace volcano::encode {

/// GPU PNG encoder. Performs PNG filtering (None/Sub/Up/Average/Paeth) on the
/// GPU via a compute shader, then assembles the PNG stream on the CPU.
/// DEFLATE compression is done on the CPU (zlib) — a future enhancement could
/// use a GPU DEFLATE implementation.
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

private:
    vk::Device device_;
    vk::Queue queue_;
    vk::CommandPool pool_;
    VmaAllocator allocator_;
    // TODO: compute pipeline + descriptor set for filtering.
};

} // namespace volcano::encode
