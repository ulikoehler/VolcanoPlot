// volcano/encode/GpuYuvEncoders.cpp
//
// Shared GPU RGBA8 → YCbCr 4:2:0 conversion (one workgroup thread per
// 8×2 source tile, edge-replicated padding to JPEG iMCU multiples) and
// the hybrid encoders built on it:
//
//   GpuJpegEncoder — GPU colorspace+downsample → libjpeg raw-data API
//                    (jpeg_write_raw_data does DCT/quant/Huffman).
//   GpuWebpEncoder — GPU YUV420 planes → WebPPictureImportYUVA → libwebp.
#include "volcano/encode/GpuYuvEncoder.hpp"

#include "volcano/core/Buffer.hpp"
#include "volcano/core/CommandBuffer.hpp"
#include "volcano/core/ShaderModule.hpp"
#include "volcano/core/DescriptorPool.hpp"

#ifdef VOLCANO_HAS_JPEG
#include <jpeglib.h>
#endif
#ifdef VOLCANO_HAS_LIBWEBP
#include <webp/encode.h>
#endif

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace volcano::encode {

namespace {

// Each thread covers an 8×2 source tile: writes two Y words per row
// (4 px/word) and one packed 4-byte chroma word per chroma plane.
const char* kYuvGlsl = R"(
#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) readonly buffer Src { uint px[]; } src;
layout(set = 0, binding = 1) writeonly buffer DstY  { uint d[]; } dy;
layout(set = 0, binding = 2) writeonly buffer DstCb { uint d[]; } dcb;
layout(set = 0, binding = 3) writeonly buffer DstCr { uint d[]; } dcr;

layout(push_constant) uniform PC {
    uint w;   // real width
    uint h;   // real height
    uint wp;  // padded luma width  (multiple of 16)
    uint hp;  // padded luma height (multiple of 16)
} pc;

vec3 pxrgb(uint x, uint y) {
    x = min(x, pc.w - 1u);
    y = min(y, pc.h - 1u);
    uint p = src.px[y * pc.w + x];
    return vec3(float(p & 0xffu), float((p >> 8) & 0xffu),
                float((p >> 16) & 0xffu));
}

uint packY(uint y, uint x0) {
    uint w = 0u;
    for (uint i = 0u; i < 4u; ++i) {
        vec3 c = pxrgb(x0 + i, y);
        float lum = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
        w |= uint(clamp(round(lum), 0.0, 255.0)) << (i * 8u);
    }
    return w;
}

void main() {
    uint tilesX = pc.wp / 8u;
    uint id = gl_GlobalInvocationID.x;
    uint tx = id % tilesX;
    uint ty = id / tilesX;
    if (ty >= pc.hp / 2u) return;

    uint x0 = tx * 8u;
    uint y0 = ty * 2u;
    uint yWordsPerRow = pc.wp / 4u;

    for (uint j = 0u; j < 2u; ++j) {
        uint y = y0 + j;
        dy.d[y * yWordsPerRow + tx * 2u]      = packY(y, x0);
        dy.d[y * yWordsPerRow + tx * 2u + 1u] = packY(y, x0 + 4u);
    }

    // Chroma: 2×2 box average per chroma sample.
    uint cbWord = 0u, crWord = 0u;
    for (uint p = 0u; p < 4u; ++p) {
        vec3 acc = vec3(0.0);
        for (uint jj = 0u; jj < 2u; ++jj)
            for (uint ii = 0u; ii < 2u; ++ii)
                acc += pxrgb(x0 + p * 2u + ii, y0 + jj);
        acc *= 0.25;
        float cb = -0.168736 * acc.r - 0.331264 * acc.g + 0.5 * acc.b + 128.0;
        float cr =  0.5 * acc.r - 0.418688 * acc.g - 0.081312 * acc.b + 128.0;
        cbWord |= uint(clamp(round(cb), 0.0, 255.0)) << (p * 8u);
        crWord |= uint(clamp(round(cr), 0.0, 255.0)) << (p * 8u);
    }
    uint cWordsPerRow = pc.wp / 8u;
    dcb.d[ty * cWordsPerRow + tx] = cbWord;
    dcr.d[ty * cWordsPerRow + tx] = crWord;
}
)";

} // namespace

// ─── GpuYuvConverter ────────────────────────────────────────────────────

struct GpuYuvConverter::Impl {
    vk::UniqueDescriptorSetLayout descLayout;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline pipeline;
    core::ShaderModule shader;
    bool ready = false;
};

GpuYuvConverter::GpuYuvConverter(vk::Device device, vk::Queue queue,
                                 vk::CommandPool pool, VmaAllocator allocator)
    : device_(device), queue_(queue), pool_(pool), allocator_(allocator),
      impl_(std::make_unique<Impl>()) {
    auto spv = core::ShaderModule::compileGlsl(kYuvGlsl, "comp");
    impl_->shader = core::ShaderModule(device, spv);

    vk::DescriptorSetLayoutBinding bindings[4];
    for (int i = 0; i < 4; ++i)
        bindings[i].setBinding(i)
                   .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                   .setDescriptorCount(1)
                   .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(bindings);
    impl_->descLayout = device.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eCompute).setOffset(0)
      .setSize(16);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(impl_->descLayout.get()).setPushConstantRanges(pc);
    impl_->pipelineLayout = device.createPipelineLayoutUnique(plci);

    vk::ComputePipelineCreateInfo ci{};
    ci.stage.setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(impl_->shader.handle()).setPName("main");
    ci.setLayout(impl_->pipelineLayout.get());
    auto res = device.createComputePipelineUnique(nullptr, ci);
    if (res.result != vk::Result::eSuccess)
        throw std::runtime_error("GpuYuvConverter pipeline failed");
    impl_->pipeline = std::move(res.value);
    impl_->ready = true;
}

GpuYuvConverter::~GpuYuvConverter() = default;

GpuYuvPlanes GpuYuvConverter::convert(std::span<const uint8_t> rgba,
                                      uint32_t w, uint32_t h) {
    GpuYuvPlanes out;
    if (!impl_ || !impl_->ready || w == 0 || h == 0) return out;
    if (rgba.size() < size_t(w) * h * 4) return out;

    out.yw = (w + 15u) & ~15u;
    out.yh = (h + 15u) & ~15u;
    out.cw = out.yw / 2;
    out.ch = out.yh / 2;

    const size_t inSize = size_t(w) * h * 4;
    const size_t yBytes = size_t(out.yw) * out.yh;
    const size_t cBytes = size_t(out.cw) * out.ch;

    core::BufferDesc inDesc{};
    inDesc.size = inSize;
    inDesc.usage = core::BufferUsage::Storage;
    inDesc.hostVisible = true;
    core::Buffer inBuf(allocator_, inDesc);
    std::memcpy(inBuf.mappedData(), rgba.data(), inSize);

    auto mkBuf = [&](size_t bytes) {
        core::BufferDesc d{};
        d.size = (bytes + 3) & ~size_t(3);
        d.usage = core::BufferUsage::Storage;
        d.hostVisible = true;
        return core::Buffer(allocator_, d);
    };
    core::Buffer yBuf = mkBuf(yBytes);
    core::Buffer cbBuf = mkBuf(cBytes);
    core::Buffer crBuf = mkBuf(cBytes);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(4);
    core::DescriptorPool descPool(device_, {ps}, 1);
    vk::DescriptorSet set = descPool.allocate(impl_->descLayout.get());

    vk::DescriptorBufferInfo infos[4];
    infos[0].setBuffer(inBuf.handle()).setOffset(0).setRange(inSize);
    infos[1].setBuffer(yBuf.handle()).setOffset(0).setRange((yBytes + 3) & ~size_t(3));
    infos[2].setBuffer(cbBuf.handle()).setOffset(0).setRange((cBytes + 3) & ~size_t(3));
    infos[3].setBuffer(crBuf.handle()).setOffset(0).setRange((cBytes + 3) & ~size_t(3));
    vk::WriteDescriptorSet writes[4];
    for (int i = 0; i < 4; ++i)
        writes[i].setDstSet(set).setDstBinding(i)
                 .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                 .setBufferInfo(infos[i]);
    device_.updateDescriptorSets(writes, {});

    {
        core::OneTimeCommands cmd(device_, pool_, queue_);
        cmd.handle().bindPipeline(vk::PipelineBindPoint::eCompute,
                                  impl_->pipeline.get());
        cmd.handle().bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                        impl_->pipelineLayout.get(), 0,
                                        set, {});
        uint32_t pcv[4] = {w, h, out.yw, out.yh};
        cmd.handle().pushConstants(impl_->pipelineLayout.get(),
                                   vk::ShaderStageFlagBits::eCompute, 0,
                                   16, pcv);
        uint32_t threads = out.yw * out.yh / 16u; // 8×2 tile per thread
        cmd.handle().dispatch((threads + 63) / 64, 1, 1);
    }

    auto pull = [](core::Buffer& b, std::vector<uint8_t>& dst, size_t bytes) {
        b.invalidate();
        dst.resize(bytes);
        std::memcpy(dst.data(), b.mappedData(), bytes);
    };
    pull(yBuf, out.y, yBytes);
    pull(cbBuf, out.cb, cBytes);
    pull(crBuf, out.cr, cBytes);
    return out;
}

// ─── GpuJpegEncoder ─────────────────────────────────────────────────────

GpuJpegEncoder::GpuJpegEncoder(vk::Device device, vk::Queue queue,
                               vk::CommandPool pool, VmaAllocator allocator) {
#ifdef VOLCANO_HAS_JPEG
    conv_ = std::make_unique<GpuYuvConverter>(device, queue, pool, allocator);
#else
    (void)device; (void)queue; (void)pool; (void)allocator;
#endif
}

GpuJpegEncoder::~GpuJpegEncoder() = default;

EncodeResult GpuJpegEncoder::encode(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height) {
#ifdef VOLCANO_HAS_JPEG
    EncodeResult res;
    if (!conv_) { res.error = "no GPU YUV converter"; return res; }
    auto pl = conv_->convert(rgba, width, height);
    if (pl.y.empty()) { res.error = "GPU YUV conversion failed"; return res; }

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
    cinfo.in_color_space = JCS_YCbCr;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(quality_, 1, 100), TRUE);
    cinfo.raw_data_in = TRUE;
    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;
    if (dpi_ > 0.0f) {
        cinfo.density_unit = 1; // dots/inch
        cinfo.X_density = cinfo.Y_density =
            UINT16(std::clamp(std::lround(dpi_), 1l, 65535l));
    }
    jpeg_start_compress(&cinfo, TRUE);

    for (auto& [k, v] : metadata_) {
        std::string s = k + "=" + v;
        jpeg_write_marker(&cinfo, JPEG_COM,
                          reinterpret_cast<const JOCTET*>(s.data()),
                          unsigned(s.size()));
    }

    // Planes are padded to full iMCU rows: feed 16 luma + 8 chroma rows.
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW yrows[16], cbrows[8], crrows[8];
        for (int i = 0; i < 16; ++i)
            yrows[i] = pl.y.data() +
                       size_t(cinfo.next_scanline + i) * pl.yw;
        for (int i = 0; i < 8; ++i) {
            cbrows[i] = pl.cb.data() +
                        size_t(cinfo.next_scanline / 2 + i) * pl.cw;
            crrows[i] = pl.cr.data() +
                        size_t(cinfo.next_scanline / 2 + i) * pl.cw;
        }
        JSAMPARRAY planes[3] = {yrows, cbrows, crrows};
        jpeg_write_raw_data(&cinfo, planes, 16);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    res.success = true;
    res.bytes.assign(buf, buf + bufSize);
    free(buf);
    return res;
#else
    (void)rgba; (void)width; (void)height;
    return {false, {}, "libjpeg not available; build with VOLCANO_HAS_JPEG=1"};
#endif
}

bool GpuJpegEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                  uint32_t w, uint32_t h,
                                  const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(res.bytes.data(), 1, res.bytes.size(), f);
    std::fclose(f);
    return true;
}

// ─── GpuWebpEncoder ─────────────────────────────────────────────────────

GpuWebpEncoder::GpuWebpEncoder(vk::Device device, vk::Queue queue,
                               vk::CommandPool pool, VmaAllocator allocator) {
#ifdef VOLCANO_HAS_LIBWEBP
    conv_ = std::make_unique<GpuYuvConverter>(device, queue, pool, allocator);
#else
    (void)device; (void)queue; (void)pool; (void)allocator;
#endif
}

GpuWebpEncoder::~GpuWebpEncoder() = default;

EncodeResult GpuWebpEncoder::encode(std::span<const uint8_t> rgba,
                                    uint32_t width, uint32_t height) {
#ifdef VOLCANO_HAS_LIBWEBP
    EncodeResult res;
    if (!conv_) { res.error = "no GPU YUV converter"; return res; }
    auto pl = conv_->convert(rgba, width, height);
    if (pl.y.empty()) { res.error = "GPU YUV conversion failed"; return res; }

    WebPConfig cfg;
    WebPConfigInit(&cfg);
    cfg.quality = float(std::clamp(quality_, 0, 100));

    WebPPicture pic;
    WebPPictureInit(&pic);
    pic.width = int(width);
    pic.height = int(height);
    pic.use_argb = 0;
    pic.colorspace = WEBP_YUV420;
    // Hand the GPU planes to libwebp directly (padded strides; the
    // encoder reads only `width × height`, edges are already replicated).
    pic.y = pl.y.data();
    pic.u = pl.cb.data();
    pic.v = pl.cr.data();
    pic.y_stride = int(pl.yw);
    pic.uv_stride = int(pl.cw);

    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    if (!WebPEncode(&cfg, &pic)) {
        res.error = "WebPEncode failed";
        WebPPictureFree(&pic);
        WebPMemoryWriterClear(&writer);
        return res;
    }
    res.success = true;
    res.bytes.assign(writer.mem, writer.mem + writer.size);
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&writer);
    return res;
#else
    (void)rgba; (void)width; (void)height;
    return {false, {}, "libwebp not available; build with VOLCANO_HAS_LIBWEBP=1"};
#endif
}

bool GpuWebpEncoder::encodeToFile(std::span<const uint8_t> rgba,
                                  uint32_t w, uint32_t h,
                                  const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(res.bytes.data(), 1, res.bytes.size(), f);
    std::fclose(f);
    return true;
}

} // namespace volcano::encode
