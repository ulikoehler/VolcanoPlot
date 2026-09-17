// volcano/encode/ExtraEncoders.cpp — JPEG/TIFF/PDF/SVG/EPS encoders
#include "volcano/encode/ExtraEncoders.hpp"
#include "volcano/encode/PngEncoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>

#ifdef VOLCANO_HAS_JPEG
#include <jpeglib.h>
#endif
#ifdef VOLCANO_HAS_ZLIB
#include <zlib.h>
#endif

namespace volcano::encode {

namespace {

bool writeFile(const std::filesystem::path& path,
               const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return f.good();
}

std::vector<uint8_t> toRgb(std::span<const uint8_t> rgba,
                           uint32_t width, uint32_t height,
                           uint8_t bgR = 255, uint8_t bgG = 255,
                           uint8_t bgB = 255) {
    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        uint8_t a = rgba[i * 4 + 3];
        for (int c = 0; c < 3; ++c) {
            uint8_t src = rgba[i * 4 + c];
            uint8_t bg = c == 0 ? bgR : (c == 1 ? bgG : bgB);
            // Composite over the background using the pixel alpha.
            rgb[i * 3 + c] = static_cast<uint8_t>(
                (src * a + bg * (255 - a)) / 255);
        }
    }
    return rgb;
}

std::string base64(std::span<const uint8_t> data) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) n |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= uint32_t(data[i + 2]);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < data.size() ? k[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? k[n & 63] : '=';
    }
    return out;
}

/// deflate-compress (zlib stream). Empty result when zlib unavailable.
std::vector<uint8_t> deflate(std::span<const uint8_t> data) {
#ifdef VOLCANO_HAS_ZLIB
    uLongf bound = compressBound(uLongf(data.size()));
    std::vector<uint8_t> out(bound);
    if (compress2(out.data(), &bound, data.data(), uLongf(data.size()),
                  Z_BEST_COMPRESSION) != Z_OK)
        return {};
    out.resize(bound);
    return out;
#else
    (void)data;
    return {};
#endif
}

/// gzip-compress (for svgz). Empty result when zlib unavailable.
std::vector<uint8_t> gzipCompress(std::span<const uint8_t> data) {
#ifdef VOLCANO_HAS_ZLIB
    z_stream zs{};
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    std::vector<uint8_t> out(compressBound(uLongf(data.size())) + 64);
    zs.next_in = const_cast<Bytef*>(data.data());
    zs.avail_in = uInt(data.size());
    zs.next_out = out.data();
    zs.avail_out = uInt(out.size());
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) return {};
    out.resize(zs.total_out);
    return out;
#else
    (void)data;
    return {};
#endif
}

std::string xmlEscape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string psEscape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        if (c == '\n') continue;
        out += c;
    }
    return out;
}

} // namespace

// ─── JPEG ───────────────────────────────────────────────────────────────────

EncodeResult CpuJpegEncoder::encode(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height) {
#ifdef VOLCANO_HAS_JPEG
    auto rgb = toRgb(rgba, width, height);

    jpeg_compress_struct cinfo{};
    jpeg_error_mgr jerr{};
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    uint8_t* buf = nullptr;
    unsigned long bufSize = 0;
    jpeg_mem_dest(&cinfo, &buf, &bufSize);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(quality_, 1, 100), TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    // Metadata as COM markers ("key=value").
    for (auto& [k, v] : metadata_) {
        std::string s = k + "=" + v;
        jpeg_write_marker(&cinfo, JPEG_COM,
                          reinterpret_cast<const JOCTET*>(s.data()),
                          unsigned(s.size()));
    }

    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = rgb.data() + size_t(cinfo.next_scanline) * width * 3;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    EncodeResult res;
    res.success = true;
    res.bytes.assign(buf, buf + bufSize);
    free(buf);
    return res;
#else
    (void)rgba; (void)width; (void)height;
    return {false, {}, "libjpeg not available; build with VOLCANO_HAS_JPEG=1"};
#endif
}

bool CpuJpegEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                  uint32_t w, uint32_t h,
                                  const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    return res.success && writeFile(path, res.bytes);
}

// ─── TIFF (uncompressed, little-endian) ─────────────────────────────────────

namespace {
struct TiffEntry { uint16_t tag, type; uint32_t count, value; };

void appendLE16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
}
void appendLE32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (i * 8)));
}
} // namespace

EncodeResult CpuTiffEncoder::encode(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height) {
    // ImageDescription: all metadata serialized as "key: value" lines.
    std::string desc;
    for (auto& [k, v] : metadata_)
        desc += k + ": " + v + "\n";
    static const char* kSoftware = "VolcanoPlot";

    // Layout: header(8) + IFD + extra data + pixels. Compute the entry
    // count first so every data offset is final.
    uint16_t count = 15 + (desc.empty() ? 0 : 1);
    uint32_t dataOff = 8 + 2 + count * 12 + 4;
    auto alloc = [&] (uint32_t n) { uint32_t o = dataOff; dataOff += n; return o; };
    uint32_t bpsOff = alloc(8);                       // SHORT[4]
    uint32_t xresOff = alloc(8), yresOff = alloc(8);  // RATIONAL ×2
    uint32_t descOff = desc.empty() ? 0 : alloc(uint32_t(desc.size() + 1));
    uint32_t swOff = alloc(uint32_t(std::strlen(kSoftware) + 1));
    uint32_t pixOff = alloc(width * height * 4);

    std::vector<TiffEntry> e = {
        {256, 4, 1, width},                 // ImageWidth
        {257, 4, 1, height},                // ImageLength
        {258, 3, 4, bpsOff},                // BitsPerSample 8,8,8,8
        {259, 3, 1, 1},                     // Compression: none
        {262, 3, 1, 2},                     // Photometric: RGB
        {273, 4, 1, pixOff},                // StripOffsets
        {277, 3, 1, 4},                     // SamplesPerPixel
        {278, 4, 1, height},                // RowsPerStrip
        {279, 4, 1, width * height * 4},    // StripByteCounts
        {282, 5, 1, xresOff},               // XResolution
        {283, 5, 1, yresOff},               // YResolution
        {296, 3, 1, 2},                     // ResolutionUnit: inch
        {305, 2, uint32_t(std::strlen(kSoftware) + 1), swOff},
        {338, 3, 1, 2},                     // ExtraSamples: unassociated alpha
    };
    if (!desc.empty())
        e.push_back({270, 2, uint32_t(desc.size() + 1), descOff});
    std::sort(e.begin(), e.end(),
              [](auto& a, auto& b) { return a.tag < b.tag; });

    std::vector<uint8_t> out;
    out.reserve(pixOff + width * height * 4);
    out.push_back('I'); out.push_back('I');
    appendLE16(out, 42);
    appendLE32(out, 8);
    appendLE16(out, count);
    for (auto& t : e) {
        appendLE16(out, t.tag);
        appendLE16(out, t.type);
        appendLE32(out, t.count);
        appendLE32(out, t.value);
    }
    appendLE32(out, 0); // next IFD

    // Extra data area (must be emitted in allocation order).
    for (int i = 0; i < 4; ++i) appendLE16(out, 8); // BitsPerSample
    uint32_t num = uint32_t(std::lround(dpi_ * 100.0f)); // dpi/1 as rational
    appendLE32(out, num); appendLE32(out, 100);     // XResolution
    appendLE32(out, num); appendLE32(out, 100);     // YResolution
    if (!desc.empty()) {
        out.insert(out.end(), desc.begin(), desc.end());
        out.push_back(0);
    }
    out.insert(out.end(), kSoftware, kSoftware + std::strlen(kSoftware) + 1);
    // Pixels (top-down RGBA).
    out.insert(out.end(), rgba.begin(), rgba.end());

    EncodeResult res;
    res.success = true;
    res.bytes = std::move(out);
    return res;
}

bool CpuTiffEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                  uint32_t w, uint32_t h,
                                  const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    return res.success && writeFile(path, res.bytes);
}

// ─── PDF (raster image) ─────────────────────────────────────────────────────

EncodeResult CpuPdfEncoder::encode(std::span<const uint8_t> rgba,
                                   uint32_t width, uint32_t height) {
    auto rgb = toRgb(rgba, width, height);
    auto packed = deflate(rgb);
    std::string filter = "/FlateDecode";
    std::vector<uint8_t> imgData;
    if (!packed.empty()) {
        imgData = std::move(packed);
    } else {
        // ASCIIHexDecode fallback (no zlib).
        filter = "/ASCIIHexDecode";
        static const char* hex = "0123456789ABCDEF";
        imgData.reserve(rgb.size() * 2 + 1);
        for (uint8_t b : rgb) {
            imgData.push_back(uint8_t(hex[b >> 4]));
            imgData.push_back(uint8_t(hex[b & 15]));
        }
        imgData.push_back('>');
    }

    // Page size in points (72/inch): pixels * 72 / dpi.
    float wpt = width * 72.0f / dpi_, hpt = height * 72.0f / dpi_;
    std::string contents = std::format(
        "q\n{:.2f} 0 0 {:.2f} 0 0 cm\n/Im0 Do\nQ\n", wpt, hpt);

    // Assemble objects, tracking byte offsets for the xref table.
    std::vector<uint8_t> out;
    std::vector<uint32_t> offs;
    auto emit = [&](std::string_view s) {
        out.insert(out.end(), s.begin(), s.end());
    };
    auto obj = [&](int n) {
        offs.push_back(uint32_t(out.size()));
        emit(std::format("{} 0 obj\n", n));
    };
    emit("%PDF-1.4\n");
    obj(1); emit("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    obj(2); emit("<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    obj(3); emit(std::format(
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {:.2f} {:.2f}] "
        "/Resources << /XObject << /Im0 4 0 R >> >> "
        "/Contents 5 0 R >>\nendobj\n", wpt, hpt));
    obj(4); emit(std::format(
        "<< /Type /XObject /Subtype /Image /Width {} /Height {} "
        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter {} "
        "/Length {} >>\nstream\n", width, height, filter, imgData.size()));
    out.insert(out.end(), imgData.begin(), imgData.end());
    emit("\nendstream\nendobj\n");
    obj(5); emit(std::format("<< /Length {} >>\nstream\n", contents.size()));
    emit(contents);
    emit("endstream\nendobj\n");

    int nObj = 5;
    if (!metadata_.empty()) {
        obj(6);
        std::string info = "<<";
        auto entry = [&](const char* pdfKey, const char* metaKey) {
            auto it = metadata_.find(metaKey);
            if (it != metadata_.end())
                info += std::format(" /{} ({})", pdfKey, psEscape(it->second));
        };
        entry("Title", "Title"); entry("Author", "Author");
        entry("Subject", "Subject"); entry("Keywords", "Keywords");
        entry("Creator", "Creator");
        info += " /Producer (VolcanoPlot)";
        info += " >>";
        emit(info + "\nendobj\n");
        nObj = 6;
    }

    uint32_t xrefOff = uint32_t(out.size());
    emit(std::format("xref\n0 {}\n", nObj + 1));
    emit("0000000000 65535 f \n");
    for (auto o : offs)
        emit(std::format("{:010} 00000 n \n", o));
    emit(std::format("trailer\n<< /Size {} /Root 1 0 R", nObj + 1));
    if (!metadata_.empty()) emit(" /Info 6 0 R");
    emit(std::format(" >>\nstartxref\n{}\n%%EOF\n", xrefOff));

    EncodeResult res;
    res.success = true;
    res.bytes = std::move(out);
    return res;
}

bool CpuPdfEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                 uint32_t w, uint32_t h,
                                 const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    return res.success && writeFile(path, res.bytes);
}

// ─── SVG (PNG-embedded raster) ──────────────────────────────────────────────

EncodeResult CpuSvgEncoder::encode(std::span<const uint8_t> rgba,
                                   uint32_t width, uint32_t height) {
    CpuPngEncoder png;
    auto pngRes = png.encode(rgba, width, height);
    if (!pngRes.success)
        return {false, {}, "PNG embedding failed: " + pngRes.error};

    std::string svg = std::format(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "width=\"{}\" height=\"{}\" viewBox=\"0 0 {} {}\">\n",
        width, height, width, height);

    auto title = metadata_.find("Title");
    if (title != metadata_.end())
        svg += "<title>" + xmlEscape(title->second) + "</title>\n";
    auto desc = metadata_.find("Description");
    if (desc == metadata_.end()) desc = metadata_.find("Subject");
    if (desc != metadata_.end())
        svg += "<desc>" + xmlEscape(desc->second) + "</desc>\n";
    if (!metadata_.empty()) {
        svg += "<metadata>\n";
        for (auto& [k, v] : metadata_)
            svg += "  <dc:" + xmlEscape(k) + ">" + xmlEscape(v) +
                   "</dc:" + xmlEscape(k) + ">\n";
        svg += "</metadata>\n";
    }
    svg += std::format(
        "<image width=\"{}\" height=\"{}\" "
        "xlink:href=\"data:image/png;base64,{}\"/>\n</svg>\n",
        width, height, base64(pngRes.bytes));

    std::vector<uint8_t> bytes(svg.begin(), svg.end());
    if (compress) {
        auto gz = gzipCompress(bytes);
        if (gz.empty())
            return {false, {}, "svgz requires zlib (VOLCANO_HAS_ZLIB)"};
        bytes = std::move(gz);
    }
    return {true, std::move(bytes), {}};
}

bool CpuSvgEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                 uint32_t w, uint32_t h,
                                 const std::filesystem::path& path) {
    if (path.extension() == ".svgz") compress = true;
    auto res = encode(rgba, w, h);
    return res.success && writeFile(path, res.bytes);
}

// ─── EPS / PS (hex RGB image) ───────────────────────────────────────────────

EncodeResult CpuEpsEncoder::encode(std::span<const uint8_t> rgba,
                                   uint32_t width, uint32_t height) {
    auto rgb = toRgb(rgba, width, height);

    std::string ps = "%!PS-Adobe-3.0 EPSF-3.0\n";
    ps += std::format("%%BoundingBox: 0 0 {} {}\n", width, height);
    ps += "%%Creator: VolcanoPlot\n";
    auto title = metadata_.find("Title");
    if (title != metadata_.end())
        ps += "%%Title: " + psEscape(title->second) + "\n";
    ps += "%%EndComments\n";
    ps += std::format(
        "/picstr {} 3 mul string def\n"
        "{} {} scale\n"
        "{} {} 8 [{} 0 0 -{} 0 {}]\n"
        "{{ currentfile picstr readhexstring pop }}\n"
        "false 3 colorimage\n", width * 3, width, height,
        width, height, width, height, height);
    static const char* hex = "0123456789ABCDEF";
    size_t col = 0;
    for (uint8_t b : rgb) {
        ps += hex[b >> 4]; ps += hex[b & 15];
        if (++col == 36) { ps += '\n'; col = 0; }
    }
    if (col) ps += '\n';
    ps += "showpage\n%%EOF\n";

    return {true, {ps.begin(), ps.end()}, {}};
}

bool CpuEpsEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                 uint32_t w, uint32_t h,
                                 const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    return res.success && writeFile(path, res.bytes);
}

} // namespace volcano::encode
