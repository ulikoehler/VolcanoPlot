// volcano/encode/ImageEncoder.hpp — image encoding interface
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace volcano::encode {

enum class ImageFormat { Png, Webp, Bmp, Raw };

struct EncodeResult {
    bool success = false;
    std::vector<uint8_t> bytes;
    std::string error;
};

/// Interface for image encoders.
class IImageEncoder {
public:
    virtual ~IImageEncoder() = default;
    /// Encode RGBA8 pixel data to a specific format.
    [[nodiscard]] virtual EncodeResult encode(std::span<const uint8_t> rgba,
                                              uint32_t width, uint32_t height) = 0;
    /// Encode and write to a file.
    [[nodiscard]] virtual bool encodeToFile(std::span<const uint8_t> rgba,
                                            uint32_t width, uint32_t height,
                                            const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual ImageFormat format() const noexcept = 0;
};

/// Factory: create a CPU encoder for the given format.
std::unique_ptr<IImageEncoder> createCpuEncoder(ImageFormat fmt);

/// Factory: create a GPU encoder (compute shader) for the given format.
/// Falls back to CPU if GPU encoding is not available for that format.
std::unique_ptr<IImageEncoder> createGpuEncoder(ImageFormat fmt,
                                                vk::Device device,
                                                vk::Queue queue,
                                                vk::CommandPool pool,
                                                VmaAllocator allocator);

} // namespace volcano::encode
