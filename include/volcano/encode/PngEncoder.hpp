// volcano/encode/PngEncoder.hpp — PNG encoder (CPU via libpng + GPU stub)
#pragma once

#include "volcano/encode/ImageEncoder.hpp"

namespace volcano::encode {

/// CPU PNG encoder using libpng.
class CpuPngEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override { return ImageFormat::Png; }

    /// Write a pHYs chunk (pixels-per-meter) when dpi > 0 — matplotlib
    /// savefig(dpi=...) parity.
    void setDpi(float dpi) override { dpi_ = dpi; }
    /// zlib level 0-9 (-1 keeps the libpng default).
    void setCompressionLevel(int level) override { level_ = level; }

private:
    float dpi_ = 0.0f;
    int level_ = -1;
};

/// CPU BMP encoder (no dependency).
class CpuBmpEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override { return ImageFormat::Bmp; }
};

/// Raw RGBA dump (no encoding).
class RawEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override { return ImageFormat::Raw; }
};

} // namespace volcano::encode
