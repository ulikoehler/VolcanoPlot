// volcano/encode/ImageEncoder.cpp — factory
#include "volcano/encode/ImageEncoder.hpp"
#include "volcano/encode/PngEncoder.hpp"
#include "volcano/encode/WebpEncoder.hpp"
#ifdef VOLCANO_GPU_ENCODE
#include "volcano/encode/GpuPngEncoder.hpp"
#endif

namespace volcano::encode {

std::unique_ptr<IImageEncoder> createCpuEncoder(ImageFormat fmt) {
    switch (fmt) {
        case ImageFormat::Png: return std::make_unique<CpuPngEncoder>();
        case ImageFormat::Webp: return std::make_unique<CpuWebpEncoder>();
        case ImageFormat::Bmp: return std::make_unique<CpuBmpEncoder>();
        case ImageFormat::Raw: return std::make_unique<RawEncoder>();
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

} // namespace volcano::encode
