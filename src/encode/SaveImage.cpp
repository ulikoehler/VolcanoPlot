// volcano/encode/SaveImage.cpp — savefig orchestration
#include "volcano/encode/ImageEncoder.hpp"
#include "volcano/encode/ExtraEncoders.hpp"
#include "volcano/encode/PngEncoder.hpp"
#include "volcano/encode/WebpEncoder.hpp"
#ifdef VOLCANO_GPU_ENCODE
#include "volcano/encode/GpuPngEncoder.hpp"
#endif

#include <algorithm>
#include <cmath>

namespace volcano::encode {

std::optional<ImageFormat> formatFromPath(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".png") return ImageFormat::Png;
    if (ext == ".webp") return ImageFormat::Webp;
    if (ext == ".bmp") return ImageFormat::Bmp;
    if (ext == ".raw") return ImageFormat::Raw;
    if (ext == ".jpg" || ext == ".jpeg") return ImageFormat::Jpeg;
    if (ext == ".tif" || ext == ".tiff") return ImageFormat::Tiff;
    if (ext == ".pdf") return ImageFormat::Pdf;
    if (ext == ".svg" || ext == ".svgz") return ImageFormat::Svg;
    if (ext == ".eps" || ext == ".ps") return ImageFormat::Eps;
    if (ext == ".pgf") return ImageFormat::Pgf;
    return std::nullopt;
}

std::unique_ptr<IImageEncoder> createCpuEncoder(ImageFormat fmt) {
    switch (fmt) {
        case ImageFormat::Png: return std::make_unique<CpuPngEncoder>();
        case ImageFormat::Webp: return std::make_unique<CpuWebpEncoder>();
        case ImageFormat::Bmp: return std::make_unique<CpuBmpEncoder>();
        case ImageFormat::Raw: return std::make_unique<RawEncoder>();
        case ImageFormat::Jpeg: return std::make_unique<CpuJpegEncoder>();
        case ImageFormat::Tiff: return std::make_unique<CpuTiffEncoder>();
        case ImageFormat::Pdf: return std::make_unique<CpuPdfEncoder>();
        case ImageFormat::Svg: return std::make_unique<CpuSvgEncoder>();
        case ImageFormat::Eps: return std::make_unique<CpuEpsEncoder>();
        case ImageFormat::Pgf: return nullptr; // not implemented
    }
    return nullptr;
}

std::unique_ptr<IImageEncoder> createGpuEncoder(ImageFormat fmt,
                                                vk::Device device,
                                                vk::Queue queue,
                                                vk::CommandPool pool,
                                                VmaAllocator allocator) {
#ifdef VOLCANO_GPU_ENCODE
    if (fmt == ImageFormat::Png) {
        return std::make_unique<GpuPngEncoder>(device, queue, pool, allocator);
    }
#endif
    (void)device; (void)queue; (void)pool; (void)allocator;
    // Fall back to CPU for unsupported formats.
    return createCpuEncoder(fmt);
}

namespace {

/// Bilinear rescale.
std::vector<uint8_t> rescale(std::span<const uint8_t> px,
                             uint32_t w, uint32_t h,
                             uint32_t nw, uint32_t nh) {
    std::vector<uint8_t> out(size_t(nw) * nh * 4);
    float sx = float(w) / nw, sy = float(h) / nh;
    for (uint32_t y = 0; y < nh; ++y) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = int(std::floor(fy)), y1 = y0 + 1;
        float ty = fy - y0;
        y0 = std::clamp(y0, 0, int(h) - 1);
        y1 = std::clamp(y1, 0, int(h) - 1);
        for (uint32_t x = 0; x < nw; ++x) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = int(std::floor(fx)), x1 = x0 + 1;
            float tx = fx - x0;
            x0 = std::clamp(x0, 0, int(w) - 1);
            x1 = std::clamp(x1, 0, int(w) - 1);
            for (int c = 0; c < 4; ++c) {
                float p00 = px[(size_t(y0) * w + x0) * 4 + c];
                float p10 = px[(size_t(y0) * w + x1) * 4 + c];
                float p01 = px[(size_t(y1) * w + x0) * 4 + c];
                float p11 = px[(size_t(y1) * w + x1) * 4 + c];
                float v = (p00 * (1 - tx) + p10 * tx) * (1 - ty) +
                          (p01 * (1 - tx) + p11 * tx) * ty;
                out[(size_t(y) * nw + x) * 4 + c] =
                    uint8_t(std::clamp(std::lround(v), 0l, 255l));
            }
        }
    }
    return out;
}

/// bbox_inches="tight": find the bounding box of drawn content.
/// Transparent → alpha > 0; otherwise → pixels differing from the
/// top-left corner (the figure facecolor).
struct BBox { uint32_t x0, y0, x1, y1; bool empty; };
BBox contentBBox(std::span<const uint8_t> px, uint32_t w, uint32_t h,
                 bool transparent) {
    uint32_t bg0 = px[0], bg1 = px[1], bg2 = px[2], bg3 = px[3];
    BBox b{w, h, 0, 0, true};
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            size_t i = (size_t(y) * w + x) * 4;
            bool drawn = transparent
                ? px[i + 3] != 0
                : (px[i] != bg0 || px[i + 1] != bg1 ||
                   px[i + 2] != bg2 || px[i + 3] != bg3);
            if (drawn) {
                b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
                b.empty = false;
            }
        }
    return b;
}

std::vector<uint8_t> crop(std::span<const uint8_t> px, uint32_t w,
                          const BBox& b) {
    std::vector<uint8_t> out(size_t(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1) * 4);
    for (uint32_t y = b.y0; y <= b.y1; ++y)
        std::copy_n(px.begin() + (size_t(y) * w + b.x0) * 4,
                    (b.x1 - b.x0 + 1) * 4,
                    out.begin() + (size_t(y - b.y0) * (b.x1 - b.x0 + 1)) * 4);
    return out;
}

/// Pad the image with a background (transparent, or the corner pixel).
struct Padded { std::vector<uint8_t> px; uint32_t w, h; };
Padded pad(std::vector<uint8_t> px, uint32_t w, uint32_t h,
           uint32_t padPx, bool transparent) {
    if (padPx == 0) return {std::move(px), w, h};
    uint32_t nw = w + 2 * padPx, nh = h + 2 * padPx;
    std::vector<uint8_t> out(size_t(nw) * nh * 4);
    uint8_t bg[4] = {0, 0, 0, 0};
    if (!transparent) {
        bg[0] = px[0]; bg[1] = px[1]; bg[2] = px[2]; bg[3] = px[3];
    }
    for (size_t i = 0; i < size_t(nw) * nh; ++i)
        std::copy_n(bg, 4, out.begin() + i * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::copy_n(px.begin() + size_t(y) * w * 4, size_t(w) * 4,
                    out.begin() + (size_t(y + padPx) * nw + padPx) * 4);
    return {std::move(out), nw, nh};
}

} // namespace

bool saveImage(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
               const std::filesystem::path& path, const SaveOptions& opts) {
    ImageFormat fmt = ImageFormat::Png;
    if (opts.format) {
        fmt = *opts.format;
    } else {
        auto inferred = formatFromPath(path);
        if (!inferred) return false;
        fmt = *inferred;
    }
    auto enc = createCpuEncoder(fmt);
    if (!enc) return false; // e.g. Pgf — no encoder

    std::vector<uint8_t> px(rgba.begin(), rgba.end());
    uint32_t w = width, h = height;

    // bbox_inches="tight": crop to content in canvas pixels.
    if (opts.tight) {
        auto b = contentBBox(px, w, h, opts.transparent);
        if (!b.empty) {
            px = crop(px, w, b);
            w = b.x1 - b.x0 + 1;
            h = b.y1 - b.y0 + 1;
        }
    }

    // dpi: output = canvas * dpi / canvasDpi (the figure's dpi).
    if (opts.dpi != opts.canvasDpi) {
        float k = opts.dpi / opts.canvasDpi;
        uint32_t nw = std::max(1u, uint32_t(std::lround(w * k)));
        uint32_t nh = std::max(1u, uint32_t(std::lround(h * k)));
        px = rescale(px, w, h, nw, nh);
        w = nw; h = nh;
    }

    // pad_inches applies at output resolution (mpl pads the saved bbox).
    if (opts.tight) {
        uint32_t padPx = uint32_t(std::lround(opts.padInches * opts.dpi));
        auto p = pad(std::move(px), w, h, padPx, opts.transparent);
        px = std::move(p.px); w = p.w; h = p.h;
    }

    enc->setMetadata(opts.metadata);
    enc->setQuality(opts.quality);
    enc->setDpi(opts.dpi);
    return enc->encodeToFile(px, w, h, path);
}

} // namespace volcano::encode
