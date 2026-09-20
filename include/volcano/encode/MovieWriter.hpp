// volcano/encode/MovieWriter.hpp — animation output writers
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace volcano::encode {

/// Frame-by-frame movie/animated-image writer (matplotlib MovieWriter).
class MovieWriter {
public:
    virtual ~MovieWriter() = default;

    /// Open the output. fps drives per-frame duration metadata.
    virtual bool open(const std::filesystem::path& path,
                      uint32_t width, uint32_t height, double fps,
                      const std::map<std::string, std::string>& meta = {}) = 0;
    /// Append one RGBA8 frame.
    virtual bool writeFrame(std::span<const uint8_t> rgba) = 0;
    /// Finalize and close the output.
    virtual bool finish() = 0;
    /// Failure detail (empty on success).
    [[nodiscard]] virtual std::string error() const { return {}; }
    /// Encoded bytes when the writer produces an in-memory stream
    /// (APNG/GIF); empty for pipe writers.
    [[nodiscard]] virtual std::span<const uint8_t> bytes() const { return {}; }
};

/// Self-contained APNG (animated PNG) writer. Requires zlib.
class ApngWriter : public MovieWriter {
public:
    ~ApngWriter() override;
    bool open(const std::filesystem::path& path, uint32_t w, uint32_t h,
              double fps,
              const std::map<std::string, std::string>& meta = {}) override;
    bool writeFrame(std::span<const uint8_t> rgba) override;
    bool finish() override;
    [[nodiscard]] std::string error() const override { return error_; }
    [[nodiscard]] std::span<const uint8_t> bytes() const override {
        return bytes_;
    }

    /// Optional hook producing PNG-filtered scanlines (`h` rows of
    /// `1 filter byte + w*4 bytes`) per frame — e.g. GpuPngEncoder's
    /// compute-shader filter. Replaces the default filter-0 CPU path.
    void setFrameFilter(
        std::function<std::vector<uint8_t>(std::span<const uint8_t>,
                                         uint32_t, uint32_t)> f) {
        frameFilter_ = std::move(f);
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
    std::filesystem::path path_;
    std::vector<uint8_t> bytes_;
    std::function<std::vector<uint8_t>(std::span<const uint8_t>,
                                     uint32_t, uint32_t)> frameFilter_;
};

/// Self-contained GIF89a writer (animated GIF, global 256-color palette
/// via uniform 6x6x6+gray quantization). matplotlib's PillowWriter
/// equivalent — writes .gif without external tools.
class GifWriter : public MovieWriter {
public:
    ~GifWriter() override;
    bool open(const std::filesystem::path& path, uint32_t w, uint32_t h,
              double fps,
              const std::map<std::string, std::string>& meta = {}) override;
    bool writeFrame(std::span<const uint8_t> rgba) override;
    bool finish() override;
    [[nodiscard]] std::string error() const override { return error_; }
    [[nodiscard]] std::span<const uint8_t> bytes() const override {
        return bytes_;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
    std::filesystem::path path_;
    std::vector<uint8_t> bytes_;
};

/// PillowWriter equivalent — animated images without external tools
/// (GIF89a). Name kept for mpl API parity.
using PillowWriter = GifWriter;

/// FFMpegWriter: pipes raw RGBA frames to an `ffmpeg` subprocess
/// (requires ffmpeg on PATH). Writes mp4/avi/gif per the extension.
class FFMpegWriter : public MovieWriter {
public:
    ~FFMpegWriter() override;
    bool open(const std::filesystem::path& path, uint32_t w, uint32_t h,
              double fps,
              const std::map<std::string, std::string>& meta = {}) override;
    bool writeFrame(std::span<const uint8_t> rgba) override;
    bool finish() override;
    [[nodiscard]] std::string error() const override { return error_; }
    /// True when an ffmpeg binary is available.
    static bool available();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

/// ImageMagickWriter: pipes raw RGBA frames to `magick convert` /
/// `convert` (requires ImageMagick on PATH).
class ImageMagickWriter : public MovieWriter {
public:
    ~ImageMagickWriter() override;
    bool open(const std::filesystem::path& path, uint32_t w, uint32_t h,
              double fps,
              const std::map<std::string, std::string>& meta = {}) override;
    bool writeFrame(std::span<const uint8_t> rgba) override;
    bool finish() override;
    [[nodiscard]] std::string error() const override { return error_; }
    static bool available();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

/// Writer factory by name (mpl `writernames`): "apng", "gif"/"pillow",
/// "ffmpeg", "imagemagick". Falls back by extension when name empty.
std::unique_ptr<MovieWriter> createMovieWriter(
    std::string_view name, const std::filesystem::path& path);

/// Names of writers that can open right now (binary present or built-in).
std::vector<std::string> availableMovieWriters();

/// HTML writer helpers (mpl to_jshtml / to_html5_video):
/// embeds base64 PNG frames into a standalone HTML page with a JS player.
std::string jsHtmlFromPngFrames(std::span<const std::vector<uint8_t>> pngFrames,
                                double fps, uint32_t width, uint32_t height);

/// `<video>` tag embedding a base64 movie (mp4) — used when a video
/// writer produced one; callers may fall back to jsHtmlFromPngFrames.
std::string html5VideoFromMovie(std::span<const uint8_t> movieBytes,
                                std::string_view mime = "video/mp4");

/// Base64 encode helper (public for tests).
std::string base64Encode(std::span<const uint8_t> data);

} // namespace volcano::encode
