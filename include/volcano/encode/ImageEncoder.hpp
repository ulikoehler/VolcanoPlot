// volcano/encode/ImageEncoder.hpp — image encoding interface
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace volcano::encode {

enum class ImageFormat { Png, Webp, Bmp, Raw, Jpeg, Tiff, Pdf, Svg, Eps, Pgf };

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

    /// Per-format metadata (PNG tEXt, JPEG COM, TIFF tags, PDF /Info,
    /// SVG dc:*, EPS %% comments). Default: ignored.
    virtual void setMetadata(std::map<std::string, std::string> meta) {
        metadata_ = std::move(meta);
    }
    /// Lossy quality hint 0-100 (JPEG). Default: ignored.
    virtual void setQuality(int q) { (void)q; }
    /// Resolution hint in DPI (TIFF tags, PDF page size). Default: ignored.
    virtual void setDpi(float dpi) { (void)dpi; }

protected:
    std::map<std::string, std::string> metadata_;
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

/// Infer the output format from a file extension (matplotlib savefig
/// format detection). Returns nullopt for unknown extensions.
std::optional<ImageFormat> formatFromPath(const std::filesystem::path& path);

/// matplotlib savefig options.
struct SaveOptions {
    /// Output resolution: output pixels = canvas * dpi / 100
    /// (figure.dpi is 100).
    float dpi = 100.0f;
    /// Transparent background (clear alpha 0 instead of opaque facecolor).
    bool transparent = false;
    /// Crop to the drawn content bounding box (bbox_inches="tight").
    bool tight = false;
    /// Padding around the tight bbox, in inches (pad_inches).
    float padInches = 0.1f;
    /// Lossy quality for JPEG (pil_kwargs quality equivalent).
    int quality = 95;
    /// Per-format metadata (matplotlib savefig metadata=...).
    std::map<std::string, std::string> metadata;
    /// Explicit format override (matplotlib format="pdf"); when set the
    /// file extension is ignored.
    std::optional<ImageFormat> format;
};

/// High-level save: applies dpi scaling, tight bbox crop + pad, and
/// dispatches to the format encoder inferred from `path` (or
/// options.format). `rgba` is the RGBA8 framebuffer readback; for
/// transparent output the caller should have rendered with a clear
/// alpha of 0.
[[nodiscard]] bool saveImage(std::span<const uint8_t> rgba,
                             uint32_t width, uint32_t height,
                             const std::filesystem::path& path,
                             const SaveOptions& options = {});

} // namespace volcano::encode
