// volcano/encode/PngEncoder.cpp
#include "volcano/encode/PngEncoder.hpp"

#include <fstream>
#include <stdexcept>

#ifdef VOLCANO_HAS_LIBPNG
#include <png.h>
#endif

namespace volcano::encode {

#ifdef VOLCANO_HAS_LIBPNG

namespace {
void pngWriteCb(png_structp png, png_bytep data, png_size_t len) {
    auto* v = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(png));
    v->insert(v->end(), data, data + len);
}
} // namespace

EncodeResult CpuPngEncoder::encode(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
    EncodeResult res;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { res.error = "png_create_write_struct failed"; return res; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); res.error = "png_create_info_struct failed"; return res; }

    std::vector<uint8_t> out;
    png_set_write_fn(png, &out, pngWriteCb, nullptr);
    // Suppress the warning about unused 'out' if libpng doesn't write
    (void)out;

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        res.error = "libpng error";
        return res;
    }

    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(height);
    for (uint32_t y = 0; y < height; ++y)
        rows[y] = const_cast<png_bytep>(rgba.data() + y * width * 4);
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);

    res.success = true;
    res.bytes = std::move(out);
    return res;
}

#else

EncodeResult CpuPngEncoder::encode(std::span<const uint8_t>, uint32_t, uint32_t) {
    return {false, {}, "libpng not available; build with VOLCANO_HAS_LIBPNG=1"};
}

#endif

bool CpuPngEncoder::encodeToFile(std::span<const uint8_t> rgba, uint32_t w, uint32_t h,
                                 const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(res.bytes.data()), res.bytes.size());
    return true;
}

// ---- BMP ----

EncodeResult CpuBmpEncoder::encode(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
    EncodeResult res;
    uint32_t rowSize = ((width * 3 + 3) / 4) * 4;
    uint32_t imageSize = rowSize * height;
    uint32_t fileSize = 54 + imageSize;
    res.bytes.resize(fileSize);
    // BMP header
    auto& b = res.bytes;
    b[0] = 'B'; b[1] = 'M';
    *reinterpret_cast<uint32_t*>(&b[2]) = fileSize;
    *reinterpret_cast<uint32_t*>(&b[10]) = 54;
    *reinterpret_cast<uint32_t*>(&b[14]) = 40;
    *reinterpret_cast<uint32_t*>(&b[18]) = width;
    *reinterpret_cast<uint32_t*>(&b[22]) = height;
    *reinterpret_cast<uint16_t*>(&b[26]) = 1;
    *reinterpret_cast<uint16_t*>(&b[28]) = 24;
    // Pixel data (bottom-up, BGR)
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* px = rgba.data() + ((height - 1 - y) * width + x) * 4;
            uint32_t off = 54 + y * rowSize + x * 3;
            b[off] = px[2]; b[off+1] = px[1]; b[off+2] = px[0];
        }
    }
    res.success = true;
    return res;
}

bool CpuBmpEncoder::encodeToFile(std::span<const uint8_t> rgba, uint32_t w, uint32_t h,
                                 const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(res.bytes.data()), res.bytes.size());
    return true;
}

// ---- Raw ----

EncodeResult RawEncoder::encode(std::span<const uint8_t> rgba, uint32_t, uint32_t) {
    return {true, {rgba.begin(), rgba.end()}, {}};
}

bool RawEncoder::encodeToFile(std::span<const uint8_t> rgba, uint32_t, uint32_t,
                              const std::filesystem::path& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(rgba.data()), rgba.size());
    return true;
}

} // namespace volcano::encode
