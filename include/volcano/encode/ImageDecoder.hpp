// volcano/encode/ImageDecoder.hpp — CPU image decoding for imread.
//
// Decodes PNG (libpng) and WebP (libwebp) files/buffers to RGBA8,
// mirroring matplotlib.image.imread.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "volcano/encode/ImageEncoder.hpp"

namespace volcano::encode {

struct DecodedImage {
    uint32_t width = 0, height = 0;
    /// RGBA8 pixels, row-major top-down.
    std::vector<uint8_t> rgba;
};

/// Detect the image format from magic bytes (PNG, WebP, or nullopt).
[[nodiscard]] std::optional<ImageFormat> detectImageFormat(
    std::span<const uint8_t> bytes);

/// Decode PNG/WebP image bytes to RGBA8. Returns nullopt when the codec
/// is unavailable or the data is not a supported image.
[[nodiscard]] std::optional<DecodedImage> decodeImage(
    std::span<const uint8_t> bytes);

/// mpl `imread` — decode an image file to RGBA8.
[[nodiscard]] std::optional<DecodedImage> imread(
    const std::filesystem::path& path);

} // namespace volcano::encode
