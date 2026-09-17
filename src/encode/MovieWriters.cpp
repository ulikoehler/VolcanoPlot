// volcano/encode/MovieWriters.cpp — animation writers
#include "volcano/encode/MovieWriter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <format>
#include <unordered_map>

#ifdef VOLCANO_HAS_ZLIB
#include <zlib.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#define VOLCANO_HAS_POPEN 1
#include <cstdlib>
#endif

namespace volcano::encode {

namespace {

void putU32be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void putU16be(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void putU16le(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}

void putU32le(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

} // namespace

// =============================================================== APNG
// PNG signature + IHDR + acTL + fcTL/IDAT + fcTL/fdAT... + IEND.

struct ApngWriter::Impl {
    std::vector<uint8_t> header; // PNG sig + IHDR
    uint32_t w = 0, h = 0;
    double fps = 10.0;
    std::vector<std::vector<uint8_t>> frames; // deflated scanline streams
};

namespace {

#ifdef VOLCANO_HAS_ZLIB
uint32_t pngCrc(std::string_view type, std::span<const uint8_t> data) {
    uLong c = crc32(0, Z_NULL, 0);
    c = crc32(c, reinterpret_cast<const Bytef*>(type.data()), 4);
    if (!data.empty())
        c = crc32(c, reinterpret_cast<const Bytef*>(data.data()),
                  uInt(data.size()));
    return uint32_t(c);
}

void pngChunk(std::vector<uint8_t>& out, const char* type,
              std::span<const uint8_t> data) {
    putU32be(out, uint32_t(data.size()));
    size_t t = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putU32be(out, pngCrc(std::string_view(
        reinterpret_cast<const char*>(out.data() + t), 4), data));
}

std::vector<uint8_t> deflateRgba(std::span<const uint8_t> rgba,
                                 uint32_t w, uint32_t h) {
    // Scanlines: filter byte 0 + RGBA row.
    size_t raw = size_t(h) * (1 + w * 4);
    std::vector<uint8_t> scan(raw);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = scan.data() + size_t(y) * (1 + w * 4);
        row[0] = 0;
        std::memcpy(row + 1, rgba.data() + size_t(y) * w * 4, w * 4);
    }
    uLongf bound = compressBound(uLong(raw));
    std::vector<uint8_t> z(bound);
    if (compress2(z.data(), &bound, scan.data(), uLong(raw),
                  Z_BEST_SPEED) != Z_OK)
        return {};
    z.resize(bound);
    return z;
}

void apngFctl(std::vector<uint8_t>& out, uint32_t seq, uint32_t w,
              uint32_t h, double fps) {
    std::vector<uint8_t> d;
    putU32be(d, seq);
    putU32be(d, w);
    putU32be(d, h);
    putU32be(d, 0); putU32be(d, 0);        // x/y offset
    putU16be(d, uint16_t(1000.0 / fps));   // delay_num (ms)
    putU16be(d, 1000);                     // delay_den
    d.push_back(0);                        // dispose_op = none
    d.push_back(0);                        // blend_op = source
    pngChunk(out, "fcTL", d);
}
#endif

} // namespace

ApngWriter::~ApngWriter() = default;

bool ApngWriter::open(const std::filesystem::path& path, uint32_t w,
                      uint32_t h, double fps,
                      const std::map<std::string, std::string>&) {
    path_ = path;
    impl_ = std::make_unique<Impl>();
    impl_->w = w;
    impl_->h = h;
    impl_->fps = fps > 0 ? fps : 10.0;
#ifndef VOLCANO_HAS_ZLIB
    error_ = "APNG requires zlib (VOLCANO_HAS_ZLIB)";
    return false;
#else
    impl_->header = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    putU32be(ihdr, w);
    putU32be(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
    pngChunk(impl_->header, "IHDR", ihdr);
    return true;
#endif
}

bool ApngWriter::writeFrame(std::span<const uint8_t> rgba) {
#ifndef VOLCANO_HAS_ZLIB
    (void)rgba;
    return false;
#else
    if (!impl_ || rgba.size() < size_t(impl_->w) * impl_->h * 4) {
        error_ = "bad frame";
        return false;
    }
    auto z = deflateRgba(rgba, impl_->w, impl_->h);
    if (z.empty()) { error_ = "deflate failed"; return false; }
    impl_->frames.push_back(std::move(z));
    return true;
#endif
}

bool ApngWriter::finish() {
#ifndef VOLCANO_HAS_ZLIB
    return false;
#else
    if (!impl_ || impl_->frames.empty()) { error_ = "no frames"; return false; }
    auto& o = *impl_;
    auto out = o.header;

    // acTL: frame count + infinite looping.
    std::vector<uint8_t> actl;
    putU32be(actl, uint32_t(o.frames.size()));
    putU32be(actl, 0);
    pngChunk(out, "acTL", actl);

    uint32_t seq = 0;
    for (size_t i = 0; i < o.frames.size(); ++i) {
        apngFctl(out, seq++, o.w, o.h, o.fps);
        if (i == 0) {
            pngChunk(out, "IDAT", o.frames[0]);
        } else {
            std::vector<uint8_t> fdat;
            putU32be(fdat, seq++);
            fdat.insert(fdat.end(), o.frames[i].begin(), o.frames[i].end());
            pngChunk(out, "fdAT", fdat);
        }
    }
    pngChunk(out, "IEND", {});

    if (!path_.empty()) {
        FILE* f = std::fopen(path_.c_str(), "wb");
        if (!f) { error_ = "open failed"; return false; }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
    bytes_ = std::move(out);
    return true;
#endif
}

// =============================================================== GIF89a

struct GifWriter::Impl {
    std::vector<uint8_t> out;
    uint32_t w = 0, h = 0;
    uint16_t delayCs = 10;
};

namespace {

/// Global palette: 6×6×6 RGB cube (216) + 40 grayscale levels.
std::vector<uint8_t> gifPalette() {
    std::vector<uint8_t> p;
    p.reserve(256 * 3);
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b) {
                p.push_back(uint8_t(r * 51));
                p.push_back(uint8_t(g * 51));
                p.push_back(uint8_t(b * 51));
            }
    for (int i = 0; i < 40; ++i) {
        uint8_t v = uint8_t(i * 255 / 39);
        p.push_back(v); p.push_back(v); p.push_back(v);
    }
    return p;
}

uint8_t gifIndex(const uint8_t* px) {
    uint8_t r = px[0], g = px[1], b = px[2];
    // Near-gray → grayscale ramp.
    if (std::abs(int(r) - int(g)) < 8 && std::abs(int(g) - int(b)) < 8) {
        int lum = (int(r) + int(g) + int(b)) / 3;
        return uint8_t(216 + lum * 39 / 255);
    }
    return uint8_t((r / 51) * 36 + (g / 51) * 6 + (b / 51));
}

/// GIF LZW compression of 8-bit indices, emitting a code stream.
std::vector<uint8_t> gifLzw(std::span<const uint8_t> indices) {
    constexpr int kMinCode = 8;
    constexpr int kClear = 1 << kMinCode;   // 256
    constexpr int kEnd = kClear + 1;        // 257
    constexpr int kMaxCode = 4096;

    std::vector<uint8_t> out;
    uint32_t bits = 0;
    int nbits = 0;
    int codeSize = kMinCode + 1;
    auto emit = [&](int code) {
        bits |= uint32_t(code) << nbits;
        nbits += codeSize;
        while (nbits >= 8) {
            out.push_back(uint8_t(bits & 0xFF));
            bits >>= 8;
            nbits -= 8;
        }
    };

    std::unordered_map<uint32_t, int> dict; // (prefix<<8)|k → code
    int next = kEnd + 1;
    emit(kClear);
    int prefix = -1;
    for (uint8_t k : indices) {
        if (prefix < 0) { prefix = k; continue; }
        uint32_t key = (uint32_t(prefix) << 8) | k;
        auto it = dict.find(key);
        if (it != dict.end()) {
            prefix = it->second;
        } else {
            emit(prefix);
            dict[key] = next++;
            if (next == kMaxCode) {
                emit(kClear);
                dict.clear();
                next = kEnd + 1;
                codeSize = kMinCode + 1;
            } else if (next == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
            prefix = k;
        }
    }
    if (prefix >= 0) emit(prefix);
    emit(kEnd);
    if (nbits > 0) out.push_back(uint8_t(bits & 0xFF));
    return out;
}

} // namespace

GifWriter::~GifWriter() = default;

bool GifWriter::open(const std::filesystem::path& path, uint32_t w,
                     uint32_t h, double fps,
                     const std::map<std::string, std::string>&) {
    path_ = path;
    impl_ = std::make_unique<Impl>();
    impl_->w = w;
    impl_->h = h;
    impl_->delayCs = uint16_t(std::max(1.0, 100.0 / (fps > 0 ? fps : 10.0)));

    auto& o = impl_->out;
    o.insert(o.end(), {'G', 'I', 'F', '8', '9', 'a'});
    putU16le(o, uint16_t(w));
    putU16le(o, uint16_t(h));
    o.push_back(0xF7); // GCT, 8-bit color, 256 entries
    o.push_back(0);    // background
    o.push_back(0);    // aspect
    auto pal = gifPalette();
    o.insert(o.end(), pal.begin(), pal.end());
    // NETSCAPE looping extension (repeat forever).
    o.insert(o.end(), {0x21, 0xFF, 0x0B});
    o.insert(o.end(), {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E',
                       '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00});
    return true;
}

bool GifWriter::writeFrame(std::span<const uint8_t> rgba) {
    if (!impl_ || rgba.size() < size_t(impl_->w) * impl_->h * 4) {
        error_ = "bad frame";
        return false;
    }
    auto& o = impl_->out;
    size_t n = size_t(impl_->w) * impl_->h;
    std::vector<uint8_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = gifIndex(rgba.data() + i * 4);

    // Graphic Control Extension (delay, disposal=none).
    o.insert(o.end(), {0x21, 0xF9, 0x04, 0x04});
    putU16le(o, impl_->delayCs);
    o.insert(o.end(), {0x00, 0x00});
    // Image Descriptor.
    o.push_back(0x2C);
    putU16le(o, 0); putU16le(o, 0);
    putU16le(o, uint16_t(impl_->w));
    putU16le(o, uint16_t(impl_->h));
    o.push_back(0); // no local color table
    // LZW data in ≤255-byte sub-blocks.
    o.push_back(8); // min code size
    auto lzw = gifLzw(idx);
    for (size_t off = 0; off < lzw.size(); off += 255) {
        size_t len = std::min<size_t>(255, lzw.size() - off);
        o.push_back(uint8_t(len));
        o.insert(o.end(), lzw.begin() + off, lzw.begin() + off + len);
    }
    o.push_back(0); // block terminator
    return true;
}

bool GifWriter::finish() {
    if (!impl_) { error_ = "not open"; return false; }
    impl_->out.push_back(0x3B); // trailer
    bytes_ = std::move(impl_->out);
    if (!path_.empty()) {
        FILE* f = std::fopen(path_.c_str(), "wb");
        if (!f) { error_ = "open failed"; return false; }
        std::fwrite(bytes_.data(), 1, bytes_.size(), f);
        std::fclose(f);
    }
    return true;
}

// =============================================================== Pipe writers

#if VOLCANO_HAS_POPEN

namespace {

struct Pipe {
    FILE* f = nullptr;
    ~Pipe() { if (f) pclose(f); }
    bool write(std::span<const uint8_t> d) {
        return f && std::fwrite(d.data(), 1, d.size(), f) == d.size();
    }
};

bool cmdExists(const char* name) {
    return std::system(
        (std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
}

} // namespace

struct FFMpegWriter::Impl { Pipe pipe; std::filesystem::path path; };

FFMpegWriter::~FFMpegWriter() = default;
bool FFMpegWriter::available() { return cmdExists("ffmpeg"); }

bool FFMpegWriter::open(const std::filesystem::path& path, uint32_t w,
                        uint32_t h, double fps,
                        const std::map<std::string, std::string>&) {
    if (!available()) { error_ = "ffmpeg not found"; return false; }
    impl_ = std::make_unique<Impl>();
    impl_->path = path;
    auto cmd = std::format(
        "ffmpeg -y -f rawvideo -pix_fmt rgba -s {}x{} -r {:.3f} -i - "
        "-pix_fmt yuv420p -loglevel error '{}' 2>/dev/null",
        w, h, fps > 0 ? fps : 10.0, path.string());
    impl_->pipe.f = popen(cmd.c_str(), "w");
    if (!impl_->pipe.f) { error_ = "popen failed"; return false; }
    return true;
}

bool FFMpegWriter::writeFrame(std::span<const uint8_t> rgba) {
    if (!impl_ || !impl_->pipe.write(rgba)) { error_ = "pipe write failed"; return false; }
    return true;
}

bool FFMpegWriter::finish() {
    if (!impl_ || !impl_->pipe.f) { error_ = "not open"; return false; }
    int rc = pclose(impl_->pipe.f);
    impl_->pipe.f = nullptr;
    return rc == 0 && std::filesystem::exists(impl_->path);
}

struct ImageMagickWriter::Impl { Pipe pipe; std::filesystem::path path; };

ImageMagickWriter::~ImageMagickWriter() = default;
bool ImageMagickWriter::available() {
    return cmdExists("magick") || cmdExists("convert");
}

bool ImageMagickWriter::open(const std::filesystem::path& path, uint32_t w,
                             uint32_t h, double fps,
                             const std::map<std::string, std::string>&) {
    if (!available()) { error_ = "imagemagick not found"; return false; }
    impl_ = std::make_unique<Impl>();
    impl_->path = path;
    const char* exe = cmdExists("magick") ? "magick" : "convert";
    int delay = int(std::max(1.0, 100.0 / (fps > 0 ? fps : 10.0)));
    auto cmd = std::format(
        "{} -size {}x{} -depth 8 -delay {} rgba:- '{}' 2>/dev/null",
        exe, w, h, delay, path.string());
    impl_->pipe.f = popen(cmd.c_str(), "w");
    if (!impl_->pipe.f) { error_ = "popen failed"; return false; }
    return true;
}

bool ImageMagickWriter::writeFrame(std::span<const uint8_t> rgba) {
    if (!impl_ || !impl_->pipe.write(rgba)) { error_ = "pipe write failed"; return false; }
    return true;
}

bool ImageMagickWriter::finish() {
    if (!impl_ || !impl_->pipe.f) { error_ = "not open"; return false; }
    int rc = pclose(impl_->pipe.f);
    impl_->pipe.f = nullptr;
    return rc == 0 && std::filesystem::exists(impl_->path);
}

#else // !VOLCANO_HAS_POPEN

struct FFMpegWriter::Impl {};
FFMpegWriter::~FFMpegWriter() = default;
bool FFMpegWriter::available() { return false; }
bool FFMpegWriter::open(const std::filesystem::path&, uint32_t, uint32_t,
                        double, const std::map<std::string, std::string>&) {
    error_ = "ffmpeg writer requires POSIX popen"; return false; }
bool FFMpegWriter::writeFrame(std::span<const uint8_t>) { return false; }
bool FFMpegWriter::finish() { return false; }

struct ImageMagickWriter::Impl {};
ImageMagickWriter::~ImageMagickWriter() = default;
bool ImageMagickWriter::available() { return false; }
bool ImageMagickWriter::open(const std::filesystem::path&, uint32_t, uint32_t,
                             double, const std::map<std::string, std::string>&) {
    error_ = "imagemagick writer requires POSIX popen"; return false; }
bool ImageMagickWriter::writeFrame(std::span<const uint8_t>) { return false; }
bool ImageMagickWriter::finish() { return false; }

#endif

// =============================================================== factory

std::unique_ptr<MovieWriter> createMovieWriter(
    std::string_view name, const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& c : ext) c = char(std::tolower(c));
    if (name == "apng" || (name.empty() && ext == ".apng"))
        return std::make_unique<ApngWriter>();
    if (name == "gif" || name == "pillow" ||
        (name.empty() && ext == ".gif"))
        return std::make_unique<GifWriter>();
    if (name == "ffmpeg" ||
        (name.empty() && (ext == ".mp4" || ext == ".avi" ||
                          ext == ".mkv" || ext == ".webm")))
        return std::make_unique<FFMpegWriter>();
    if (name == "imagemagick")
        return std::make_unique<ImageMagickWriter>();
    // PNG extension with multiple frames → APNG.
    if (name.empty() && ext == ".png")
        return std::make_unique<ApngWriter>();
    return nullptr;
}

std::vector<std::string> availableMovieWriters() {
    std::vector<std::string> out = {"pillow", "apng"};
    if (FFMpegWriter::available()) out.push_back("ffmpeg");
    if (ImageMagickWriter::available()) out.push_back("imagemagick");
    return out;
}

// =============================================================== HTML helpers

std::string base64Encode(std::span<const uint8_t> data) {
    static constexpr char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < data.size()) v |= uint32_t(data[i + 2]);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += i + 1 < data.size() ? tbl[(v >> 6) & 63] : '=';
        out += i + 2 < data.size() ? tbl[v & 63] : '=';
    }
    return out;
}

std::string jsHtmlFromPngFrames(
    std::span<const std::vector<uint8_t>> pngFrames, double fps,
    uint32_t width, uint32_t height) {
    std::string html = R"HTML(<!DOCTYPE html>
<html><body>
<div id="fig"><img id="anim" width="W" height="H"></div>
<script>
var frames = [
)HTML";
    for (auto& f : pngFrames) {
        html += "  \"data:image/png;base64,";
        html += base64Encode(f);
        html += "\",\n";
    }
    html += R"HTML(];
var i = 0, el = document.getElementById('anim');
el.src = frames[0];
setInterval(function(){ i = (i + 1) % frames.length; el.src = frames[i]; },
            DELAY);
</script></body></html>
)HTML";
    // Substitute W/H/DELAY placeholders.
    auto replace = [&](const std::string& k, const std::string& v) {
        size_t pos = 0;
        while ((pos = html.find(k, pos)) != std::string::npos) {
            html.replace(pos, k.size(), v);
            pos += v.size();
        }
    };
    replace("W", std::to_string(width));
    replace("H", std::to_string(height));
    replace("DELAY", std::to_string(int(1000.0 / (fps > 0 ? fps : 10.0))));
    return html;
}

std::string html5VideoFromMovie(std::span<const uint8_t> movieBytes,
                                std::string_view mime) {
    return "<video controls autoplay loop><source src=\"data:" +
           std::string(mime) + ";base64," + base64Encode(movieBytes) +
           "\"></video>";
}

} // namespace volcano::encode
