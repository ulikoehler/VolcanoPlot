// volcano/encode/GpuYuvEncoder.hpp — GPU-side RGBA→YCbCr 4:2:0 conversion
// shared by the GPU JPEG and WebP encoders.
#pragma once

#include "volcano/encode/ImageEncoder.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

namespace volcano::encode {

/// Planes produced by GpuYuvConverter, already padded to JPEG iMCU
/// multiples: luma is `yw × yh` (multiples of 16), chroma `cw × ch`
/// (yw/2 × yh/2, 4:2:0, edge-replicated at the borders).
struct GpuYuvPlanes {
    std::vector<uint8_t> y, cb, cr;
    uint32_t yw = 0, yh = 0, cw = 0, ch = 0;
};

/// Compute-shader RGBA8 → full-range BT.601 YCbCr 4:2:0 converter.
class GpuYuvConverter {
public:
    GpuYuvConverter(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                    VmaAllocator allocator);
    ~GpuYuvConverter();

    /// Returns empty planes (y empty) on failure.
    [[nodiscard]] GpuYuvPlanes convert(std::span<const uint8_t> rgba,
                                       uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    vk::Device device_;
    vk::Queue queue_;
    vk::CommandPool pool_;
    VmaAllocator allocator_;
};

/// GPU-assisted baseline JPEG: the GPU does RGBA→YCbCr + 4:2:0 chroma
/// downsampling; libjpeg performs DCT/quantization/Huffman on the planes
/// via jpeg_write_raw_data. Falls back to the CPU encoder when libjpeg
/// is unavailable.
class GpuJpegEncoder : public IImageEncoder {
public:
    GpuJpegEncoder(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                   VmaAllocator allocator);
    ~GpuJpegEncoder() override;

    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Jpeg;
    }
    void setQuality(int q) override { quality_ = q; }
    /// DPI → JFIF APP0 density (dpi unit).
    void setDpi(float dpi) override { dpi_ = dpi; }

private:
    std::unique_ptr<GpuYuvConverter> conv_;
    int quality_ = 95;
    float dpi_ = 0.0f;
};

/// GPU-assisted WebP: GPU RGBA→YUV420 planes feed
/// WebPPictureImportYUVA; the VP8/VP8L encode stays in libwebp.
class GpuWebpEncoder : public IImageEncoder {
public:
    GpuWebpEncoder(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                   VmaAllocator allocator);
    ~GpuWebpEncoder() override;

    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Webp;
    }
    void setQuality(int q) override { quality_ = q; }

private:
    std::unique_ptr<GpuYuvConverter> conv_;
    int quality_ = 90;
};

} // namespace volcano::encode
