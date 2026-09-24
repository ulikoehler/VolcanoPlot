// volcano/encode/ImageDecoder.cpp — PNG/WebP → RGBA8 decoding.

#include "volcano/encode/ImageDecoder.hpp"

#include <cstdio>
#include <fstream>

#if defined(VOLCANO_HAS_LIBPNG)
#include <png.h>
#endif
#if defined(VOLCANO_HAS_LIBWEBP)
#include <webp/decode.h>
#endif

namespace volcano::encode {

std::optional<ImageFormat> detectImageFormat(
    std::span<const uint8_t> bytes) {
    static constexpr uint8_t kPngMagic[] = {0x89, 'P', 'N', 'G',
                                            0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() >= 8 &&
        std::equal(std::begin(kPngMagic), std::end(kPngMagic),
                   bytes.begin()))
        return ImageFormat::Png;
    // RIFF....WEBP
    if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' &&
        bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'W' &&
        bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
        return ImageFormat::Webp;
    // JPEG SOI
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 &&
        bytes[2] == 0xFF)
        return ImageFormat::Jpeg;
    return std::nullopt;
}

#if defined(VOLCANO_HAS_LIBPNG)
namespace {

// libpng simplified API decode — handles grayscale/palette/RGB/RGBA
// and 8/16-bit inputs, always producing RGBA8.
std::optional<DecodedImage> decodePng(std::span<const uint8_t> bytes) {
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, bytes.data(),
                                          bytes.size()))
        return std::nullopt;
    img.format = PNG_FORMAT_RGBA;
    DecodedImage out{img.width, img.height, {}};
    out.rgba.resize(size_t(out.width) * out.height * 4);
    if (!png_image_finish_read(&img, nullptr, out.rgba.data(),
                               /*row_stride=*/0, nullptr))
        return std::nullopt;
    return out;
}
} // namespace
#endif

#if defined(VOLCANO_HAS_LIBWEBP)
namespace {
std::optional<DecodedImage> decodeWebp(std::span<const uint8_t> bytes) {
    int w = 0, h = 0;
    if (!WebPGetInfo(bytes.data(), bytes.size(), &w, &h) || w <= 0 ||
        h <= 0)
        return std::nullopt;
    DecodedImage out{uint32_t(w), uint32_t(h), {}};
    out.rgba.resize(size_t(out.width) * out.height * 4);
    if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), out.rgba.data(),
                            int(out.rgba.size()), int(out.width) * 4))
        return std::nullopt;
    return out;
}
} // namespace
#endif

std::optional<DecodedImage> decodeImage(std::span<const uint8_t> bytes) {
    auto fmt = detectImageFormat(bytes);
    if (!fmt) return std::nullopt;
    switch (*fmt) {
    case ImageFormat::Png:
#if defined(VOLCANO_HAS_LIBPNG)
        return decodePng(bytes);
#else
        return std::nullopt;
#endif
    case ImageFormat::Webp:
#if defined(VOLCANO_HAS_LIBWEBP)
        return decodeWebp(bytes);
#else
        return std::nullopt;
#endif
    default:
        return std::nullopt;
    }
}

std::optional<DecodedImage> imread(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size <= 0) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size))
        return std::nullopt;
    return decodeImage(bytes);
}

} // namespace volcano::encode
