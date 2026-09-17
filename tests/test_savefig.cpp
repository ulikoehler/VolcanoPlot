// tests/test_savefig.cpp — §10 savefig formats, metadata, dpi, tight, transparent
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/encode/ImageEncoder.hpp>
#include <volcano/encode/ExtraEncoders.hpp>
#include <volcano/encode/PngEncoder.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;
using volcano::encode::ImageFormat;
using volcano::encode::SaveOptions;

namespace {

/// Read a file into a byte vector.
std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}

/// Search for a byte substring.
bool contains(const std::vector<uint8_t>& hay, std::string_view needle) {
    return std::search(hay.begin(), hay.end(),
                       needle.begin(), needle.end()) != hay.end();
}

/// PNG IHDR dimensions (big-endian at bytes 16/20).
std::pair<uint32_t, uint32_t> pngDims(const std::vector<uint8_t>& png) {
    if (png.size() < 24) return {0, 0};
    auto be32 = [&](size_t o) {
        return (uint32_t(png[o]) << 24) | (uint32_t(png[o + 1]) << 16) |
               (uint32_t(png[o + 2]) << 8) | uint32_t(png[o + 3]);
    };
    return {be32(16), be32(20)};
}

std::vector<uint8_t> testPixels(uint32_t w = 32, uint32_t h = 32) {
    std::vector<uint8_t> px(size_t(w) * h * 4, 255); // white opaque
    // Red square in the center.
    for (uint32_t y = h / 4; y < 3 * h / 4; ++y)
        for (uint32_t x = w / 4; x < 3 * w / 4; ++x) {
            size_t i = (size_t(y) * w + x) * 4;
            px[i] = 255; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 255;
        }
    return px;
}

struct TmpFile {
    std::filesystem::path path;
    explicit TmpFile(std::string name)
        : path(std::filesystem::temp_directory_path() / name) {}
    ~TmpFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};

} // namespace

TEST(SavefigFormats, FormatFromPath) {
    using encode::formatFromPath;
    EXPECT_EQ(formatFromPath("a.png"), ImageFormat::Png);
    EXPECT_EQ(formatFromPath("a.webp"), ImageFormat::Webp);
    EXPECT_EQ(formatFromPath("a.bmp"), ImageFormat::Bmp);
    EXPECT_EQ(formatFromPath("a.raw"), ImageFormat::Raw);
    EXPECT_EQ(formatFromPath("a.jpg"), ImageFormat::Jpeg);
    EXPECT_EQ(formatFromPath("a.jpeg"), ImageFormat::Jpeg);
    EXPECT_EQ(formatFromPath("a.tif"), ImageFormat::Tiff);
    EXPECT_EQ(formatFromPath("a.tiff"), ImageFormat::Tiff);
    EXPECT_EQ(formatFromPath("a.pdf"), ImageFormat::Pdf);
    EXPECT_EQ(formatFromPath("a.svg"), ImageFormat::Svg);
    EXPECT_EQ(formatFromPath("a.svgz"), ImageFormat::Svg);
    EXPECT_EQ(formatFromPath("a.eps"), ImageFormat::Eps);
    EXPECT_EQ(formatFromPath("a.ps"), ImageFormat::Eps);
    EXPECT_EQ(formatFromPath("a.unknown"), std::nullopt);
    EXPECT_EQ(formatFromPath("noext"), std::nullopt);
}

TEST(SavefigFormats, JpegMagicBytes) {
    auto px = testPixels();
    encode::CpuJpegEncoder enc;
    auto res = enc.encode(px, 32, 32);
    if (!res.success) {
        GTEST_SKIP() << "libjpeg not available: " << res.error;
    }
    ASSERT_GE(res.bytes.size(), 4u);
    EXPECT_EQ(res.bytes[0], 0xFF);
    EXPECT_EQ(res.bytes[1], 0xD8); // SOI
    EXPECT_EQ(res.bytes[res.bytes.size() - 2], 0xFF);
    EXPECT_EQ(res.bytes[res.bytes.size() - 1], 0xD9); // EOI
}

TEST(SavefigFormats, TiffStructure) {
    auto px = testPixels();
    encode::CpuTiffEncoder enc;
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    // Little-endian TIFF magic: "II*\0".
    EXPECT_EQ(res.bytes[0], 'I');
    EXPECT_EQ(res.bytes[1], 'I');
    EXPECT_EQ(res.bytes[2], 42);
    EXPECT_EQ(res.bytes[3], 0);
    // Total size ≥ header + pixels.
    EXPECT_GT(res.bytes.size(), size_t(32 * 32 * 4));
}

TEST(SavefigFormats, PdfStructure) {
    auto px = testPixels();
    encode::CpuPdfEncoder enc;
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "%PDF-1.4"));
    EXPECT_TRUE(contains(res.bytes, "%%EOF"));
    EXPECT_TRUE(contains(res.bytes, "/Subtype /Image"));
    EXPECT_TRUE(contains(res.bytes, "startxref"));
}

TEST(SavefigFormats, SvgEmbedsPng) {
    auto px = testPixels();
    encode::CpuSvgEncoder enc;
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "<svg"));
    EXPECT_TRUE(contains(res.bytes, "data:image/png;base64,"));
    EXPECT_TRUE(contains(res.bytes, "</svg>"));
}

TEST(SavefigFormats, SvgzIsGzipped) {
    auto px = testPixels();
    encode::CpuSvgEncoder enc;
    enc.compress = true;
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    ASSERT_GE(res.bytes.size(), 2u);
    EXPECT_EQ(res.bytes[0], 0x1F); // gzip magic
    EXPECT_EQ(res.bytes[1], 0x8B);
}

TEST(SavefigFormats, EpsStructure) {
    auto px = testPixels();
    encode::CpuEpsEncoder enc;
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "%!PS-Adobe-3.0 EPSF-3.0"));
    EXPECT_TRUE(contains(res.bytes, "%%BoundingBox: 0 0 32 32"));
    EXPECT_TRUE(contains(res.bytes, "colorimage"));
    EXPECT_TRUE(contains(res.bytes, "showpage"));
}

TEST(SavefigFormats, MetadataPng) {
    auto px = testPixels();
    encode::CpuPngEncoder enc;
    enc.setMetadata({{"Title", "TestPlot"}, {"Author", "Devin"}});
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "Title"));
    EXPECT_TRUE(contains(res.bytes, "TestPlot"));
    EXPECT_TRUE(contains(res.bytes, "tEXt"));
}

TEST(SavefigFormats, MetadataPdf) {
    auto px = testPixels();
    encode::CpuPdfEncoder enc;
    enc.setMetadata({{"Title", "DocTitle"}, {"Author", "Devin"}});
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "/Title (DocTitle)"));
    EXPECT_TRUE(contains(res.bytes, "/Author (Devin)"));
    EXPECT_TRUE(contains(res.bytes, "/Info 6 0 R"));
}

TEST(SavefigFormats, MetadataEps) {
    auto px = testPixels();
    encode::CpuEpsEncoder enc;
    enc.setMetadata({{"Title", "EPS Doc"}});
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "%%Title: EPS Doc"));
}

TEST(SavefigFormats, MetadataSvg) {
    auto px = testPixels();
    encode::CpuSvgEncoder enc;
    enc.setMetadata({{"Title", "SVG Doc"}});
    auto res = enc.encode(px, 32, 32);
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_TRUE(contains(res.bytes, "<title>SVG Doc</title>"));
}

TEST(SavefigFormats, SaveImageDispatchesByExtension) {
    auto px = testPixels();
    {
        TmpFile f("volcano_savefig_test.png");
        SaveOptions opts;
        ASSERT_TRUE(encode::saveImage(px, 32, 32, f.path, opts));
        auto bytes = readFile(f.path);
        ASSERT_GE(bytes.size(), 8u);
        EXPECT_EQ(bytes[1], 'P'); EXPECT_EQ(bytes[2], 'N');
        EXPECT_EQ(bytes[3], 'G');
        auto [w, h] = pngDims(bytes);
        EXPECT_EQ(w, 32u);
        EXPECT_EQ(h, 32u);
    }
    {
        TmpFile f("volcano_savefig_test.tiff");
        SaveOptions opts;
        ASSERT_TRUE(encode::saveImage(px, 32, 32, f.path, opts));
        auto bytes = readFile(f.path);
        EXPECT_EQ(bytes[0], 'I');
        EXPECT_EQ(bytes[2], 42);
    }
    {
        TmpFile f("volcano_savefig_test.pdf");
        SaveOptions opts;
        ASSERT_TRUE(encode::saveImage(px, 32, 32, f.path, opts));
        EXPECT_TRUE(contains(readFile(f.path), "%PDF-1.4"));
    }
}

TEST(SavefigFormats, DpiScalesOutput) {
    auto px = testPixels();
    TmpFile f("volcano_savefig_dpi.png");
    SaveOptions opts;
    opts.dpi = 200.0f;
    ASSERT_TRUE(encode::saveImage(px, 32, 32, f.path, opts));
    auto [w, h] = pngDims(readFile(f.path));
    EXPECT_EQ(w, 64u); // 32 * 200/100
    EXPECT_EQ(h, 64u);
}

TEST(SavefigFormats, TightBboxCrops) {
    auto px = testPixels(); // red square occupies the center half
    TmpFile f("volcano_savefig_tight.png");
    SaveOptions opts;
    opts.tight = true;
    opts.padInches = 0.05f; // 5px at 100dpi
    ASSERT_TRUE(encode::saveImage(px, 32, 32, f.path, opts));
    auto [w, h] = pngDims(readFile(f.path));
    // Content is 16×16 px + 2×5px pad = 26.
    EXPECT_EQ(w, 26u);
    EXPECT_EQ(h, 26u);
}

TEST(SavefigFormats, FormatOverrideIgnoresExtension) {
    auto px = testPixels();
    TmpFile f("volcano_savefig_override.dat");
    SaveOptions opts;
    opts.format = ImageFormat::Bmp;
    ASSERT_TRUE(encode::saveImage(px, 32, 32, f.path, opts));
    auto bytes = readFile(f.path);
    EXPECT_EQ(bytes[0], 'B');
    EXPECT_EQ(bytes[1], 'M');
}

TEST(SavefigFormats, RendererSavefigEndToEnd) {
    PlotTestHarness harness(128, 128, vk::SampleCountFlagBits::e1);
    Figure figure{1, 1};
    auto* axes = figure.addAxes(0, 0);
    Series2D s;
    s.points = {{0, 0}, {1, 1}};
    s.color = Color::red();
    axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));

    TmpFile f("volcano_savefig_e2e.png");
    ASSERT_TRUE(harness.renderer().savefig(figure, f.path));
    auto bytes = readFile(f.path);
    ASSERT_GE(bytes.size(), 24u);
    EXPECT_EQ(bytes[1], 'P');
    auto [w, h] = pngDims(bytes);
    EXPECT_EQ(w, 128u);
    EXPECT_EQ(h, 128u);
}

TEST(SavefigFormats, TransparentExport) {
    PlotTestHarness harness(64, 64, vk::SampleCountFlagBits::e1);
    Figure figure{1, 1};
    auto* axes = figure.addAxes(0, 0);
    axes->setStyle(flatTestStyle());
    Series2D s;
    s.points = {{0.5f, 0.5f}};
    s.color = Color::red();
    axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));

    harness.backend().setClearColor(0, 0, 0, 0);
    harness.renderer().prepare(figure);
    harness.renderer().renderFrame(figure);
    auto px = harness.backend().readbackRgba8();

    // Background pixels are transparent; the marker is opaque.
    Image img = Image::fromRgba8(px, 64, 64);
    EXPECT_EQ(img.get(0, 0).a, 0) << "corner should be transparent";
    auto c = img.centroid(Pixel::red(), 40);
    EXPECT_GT(c.count, 0u);
    EXPECT_EQ(img.get(uint32_t(c.x), uint32_t(c.y)).a, 255);
    harness.backend().setClearColor(1, 1, 1, 1); // restore
}
