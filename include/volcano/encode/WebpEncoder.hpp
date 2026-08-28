// volcano/encode/WebpEncoder.hpp — WebP encoder (CPU via libwebp)
#pragma once

#include "volcano/encode/ImageEncoder.hpp"

namespace volcano::encode {

class CpuWebpEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override { return ImageFormat::Webp; }
};

} // namespace volcano::encode
