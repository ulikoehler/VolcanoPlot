// volcano/encode/GpuPngEncoder.cpp
#include "volcano/encode/GpuPngEncoder.hpp"

#include <fstream>

namespace volcano::encode {

GpuPngEncoder::GpuPngEncoder(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                             VmaAllocator allocator)
    : device_(device), queue_(queue), pool_(pool), allocator_(allocator) {
    // TODO: create compute pipeline for PNG filtering.
}

GpuPngEncoder::~GpuPngEncoder() = default;

EncodeResult GpuPngEncoder::encode(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
    // TODO: dispatch compute shader to perform PNG row filtering, read back,
    // then CPU-side zlib DEFLATE + PNG container assembly.
    // For now, fall back to CPU encoder.
    CpuPngEncoder cpu;
    return cpu.encode(rgba, width, height);
}

bool GpuPngEncoder::encodeToFile(std::span<const uint8_t> rgba, uint32_t w, uint32_t h,
                                 const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(res.bytes.data()), res.bytes.size());
    return true;
}

} // namespace volcano::encode
