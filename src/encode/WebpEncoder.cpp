// volcano/encode/WebpEncoder.cpp
#include "volcano/encode/WebpEncoder.hpp"

#include <fstream>

#ifdef VOLCANO_HAS_LIBWEBP
#include <webp/encode.h>
#endif

namespace volcano::encode {

#ifdef VOLCANO_HAS_LIBWEBP

EncodeResult CpuWebpEncoder::encode(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
    EncodeResult res;
    WebPConfig cfg;
    WebPConfigInit(&cfg);
    cfg.quality = 90.0f;
    WebPPicture pic;
    WebPPictureInit(&pic);
    pic.width = width;
    pic.height = height;
    pic.use_argb = 1;
    WebPPictureImportRGBA(&pic, rgba.data(), width * 4);
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    if (!WebPEncode(&cfg, &pic)) {
        res.error = "WebPEncode failed";
    } else {
        res.bytes.assign(writer.mem, writer.mem + writer.size);
        res.success = true;
    }
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&writer);
    return res;
}

#else

EncodeResult CpuWebpEncoder::encode(std::span<const uint8_t>, uint32_t, uint32_t) {
    return {false, {}, "libwebp not available; build with VOLCANO_HAS_LIBWEBP=1"};
}

#endif

bool CpuWebpEncoder::encodeToFile(std::span<const uint8_t> rgba, uint32_t w, uint32_t h,
                                  const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(res.bytes.data()), res.bytes.size());
    return true;
}

} // namespace volcano::encode
