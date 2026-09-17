// volcano/encode/ExtraEncoders.hpp — JPEG/TIFF/PDF/SVG/EPS encoders
#pragma once

#include "volcano/encode/ImageEncoder.hpp"

namespace volcano::encode {

/// CPU JPEG encoder via libjpeg (VOLCANO_HAS_JPEG). RGB only — alpha is
/// composited onto white. Metadata → COM markers ("key=value").
class CpuJpegEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Jpeg;
    }
    void setQuality(int q) override { quality_ = q; }
private:
    int quality_ = 95;
};

/// Uncompressed RGBA TIFF writer (no libtiff dependency). Stores a
/// single strip; dpi → XResolution/YResolution tags; metadata →
/// ImageDescription + per-key DocumentName-style tags are not standard,
/// so all metadata is serialized into ImageDescription lines.
class CpuTiffEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Tiff;
    }
    void setDpi(float dpi) override { dpi_ = dpi; }
private:
    float dpi_ = 100.0f;
};

/// Single-page PDF with the framebuffer embedded as a raster image
/// (FlateDecode via zlib, ASCIIHexDecode fallback). Metadata → /Info.
class CpuPdfEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Pdf;
    }
    void setDpi(float dpi) override { dpi_ = dpi; }
private:
    float dpi_ = 100.0f;
};

/// SVG with the framebuffer embedded as a base64 PNG <image>.
/// Metadata → <metadata>/<dc:...> elements and <title>/<desc>.
class CpuSvgEncoder : public IImageEncoder {
public:
    /// gzip the output (svgz) — requires zlib.
    bool compress = false;
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                      uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Svg;
    }
};

/// EPS (Encapsulated PostScript) with the framebuffer as an RGB hex
/// image. Metadata → DSC comments (%%Title, %%Creator, ...).
class CpuEpsEncoder : public IImageEncoder {
public:
    [[nodiscard]] EncodeResult encode(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height) override;
    [[nodiscard]] bool encodeToFile(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height,
                                    const std::filesystem::path& path) override;
    [[nodiscard]] ImageFormat format() const noexcept override {
        return ImageFormat::Eps;
    }
};

} // namespace volcano::encode
