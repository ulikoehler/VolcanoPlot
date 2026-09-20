// volcano/plot/plots/PcolormeshPlot.cpp — pseudocolor mesh plot implementation
#include "volcano/plot/plots/PcolormeshPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <volcano/core/Buffer.hpp>
#include <volcano/core/CommandBuffer.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

/// Compute shader: expands one cell per invocation into fill triangles
/// (vec2 pos + vec4 color), matching buildGeometry()'s CPU output.
constexpr const char* kPcmTessGlsl = R"(
#version 460
layout(local_size_x = 256) in;

layout(set = 0, binding = 0) readonly buffer Xs { float v[]; } xs;
layout(set = 0, binding = 1) readonly buffer Ys { float v[]; } ys;
// Per-cell normalized t (flat shading) or per-corner t (gouraud).
layout(set = 0, binding = 2) readonly buffer Ts { float v[]; } ts;
// Colormap LUT: [0..255] = t∈[0,1], 256 = under, 257 = over, 258 = bad.
layout(set = 0, binding = 3) readonly buffer Lut { vec4 c[]; } lut;
layout(set = 0, binding = 4) writeonly buffer Pos { vec2 v[]; } pos;
layout(set = 0, binding = 5) writeonly buffer Col { vec4 v[]; } col;

layout(push_constant) uniform PC {
    uint nCols;   // cells per row (flat) or corner count (gouraud)
    uint nRows;
    uint gouraud;
    uint flags;   // bit0 = cmap.bad set, bit1 = skipNaN
} pc;

vec4 colorOf(float t) {
    if (isnan(t)) return lut.c[258];
    if (t < 0.0)  return lut.c[256];
    if (t > 1.0)  return lut.c[257];
    return lut.c[uint(t * 255.0 + 0.5)];
}

void main() {
    uint cell = gl_GlobalInvocationID.x;
    if (pc.gouraud == 0u) {
        if (cell >= pc.nCols * pc.nRows) return;
        uint i = cell % pc.nCols, j = cell / pc.nCols;
        vec4 c = colorOf(ts.v[cell]);
        vec2 bl = vec2(xs.v[i],   ys.v[j]);
        vec2 br = vec2(xs.v[i+1], ys.v[j]);
        vec2 ur = vec2(xs.v[i+1], ys.v[j+1]);
        vec2 ul = vec2(xs.v[i],   ys.v[j+1]);
        uint v = cell * 6u;
        pos.v[v]    = bl; pos.v[v+1u] = br; pos.v[v+2u] = ul;
        pos.v[v+3u] = br; pos.v[v+4u] = ur; pos.v[v+5u] = ul;
        for (uint k = 0u; k < 6u; ++k) col.v[v + k] = c;
    } else {
        // Gouraud: (nCols-1)*(nRows-1) quads, 4 tris meeting at the
        // averaged center vertex (mpl _convert_mesh_to_triangles).
        uint qw = pc.nCols - 1u;
        if (cell >= qw * (pc.nRows - 1u)) return;
        uint i = cell % qw, j = cell / qw;
        float ta = ts.v[j*pc.nCols + i],       tb = ts.v[j*pc.nCols + i+1];
        float tc = ts.v[(j+1u)*pc.nCols + i+1u], td = ts.v[(j+1u)*pc.nCols + i];
        vec4 ca = colorOf(ta), cb = colorOf(tb);
        vec4 cc = colorOf(tc), cd = colorOf(td);
        // mpl drops the quad if any corner is masked and no bad color.
        if ((pc.flags & 3u) == 2u && (isnan(ta) || isnan(tb) || isnan(tc) || isnan(td))) {
            ca = cb = cc = cd = vec4(0.0);
        }
        vec4 cCtr = (ca + cb + cc + cd) * 0.25;
        vec2 pa = vec2(xs.v[i],   ys.v[j]);
        vec2 pb = vec2(xs.v[i+1], ys.v[j]);
        vec2 pq = vec2(xs.v[i+1], ys.v[j+1]);
        vec2 pd = vec2(xs.v[i],   ys.v[j+1]);
        vec2 pCtr = (pa + pb + pq + pd) * 0.25;
        uint v = cell * 12u;
        vec2 pts[12] = vec2[12](pa, pb, pCtr, pb, pq, pCtr,
                                pq, pd, pCtr, pd, pa, pCtr);
        vec4 cls[12] = vec4[12](ca, cb, cCtr, cb, cc, cCtr,
                                cc, cd, cCtr, cd, ca, cCtr);
        for (uint k = 0u; k < 12u; ++k) {
            pos.v[v + k] = pts[k];
            col.v[v + k] = cls[k];
        }
    }
}
)";

} // namespace

PcolormeshPlot::PcolormeshPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> C,
                               uint32_t nCols, uint32_t nRows,
                               PcolormeshConfig config)
    : x_(std::move(x)), y_(std::move(y)), C_(std::move(C)),
      nCols_(nCols), nRows_(nRows), config_(std::move(config)) {
    if (config_.shading == PcmShading::Gouraud) {
        // mpl shading='gouraud': coordinates and C share the same (M, N)
        // corner shape — x/y are corner coords, not cell edges.
        if (x_.size() != nCols_)
            throw std::invalid_argument(
                "PcolormeshPlot (gouraud): x must have nCols elements");
        if (y_.size() != nRows_)
            throw std::invalid_argument(
                "PcolormeshPlot (gouraud): y must have nRows elements");
        if (nCols_ < 2 || nRows_ < 2)
            throw std::invalid_argument(
                "PcolormeshPlot (gouraud): grid must be at least 2x2");
    } else {
        if (x_.size() != nCols_ + 1)
            throw std::invalid_argument("PcolormeshPlot: x must have nCols+1 elements");
        if (y_.size() != nRows_ + 1)
            throw std::invalid_argument("PcolormeshPlot: y must have nRows+1 elements");
    }
    if (C_.size() != nCols_ * nRows_)
        throw std::invalid_argument("PcolormeshPlot: C must have nCols*nRows elements");
}

Color PcolormeshPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void PcolormeshPlot::computeValueRange() {
    // If a norm is set, autoscale it from the data (if vmin/vmax not set).
    if (config_.norm) {
        config_.norm->autoscale(C_);
        valueRange_ = {config_.norm->vmin(), config_.norm->vmax()};
        return;
    }
    if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
        return;
    }
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : C_) {
        if (std::isnan(v)) continue;
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
    valueRange_ = {vmin, vmax};
}

void PcolormeshPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Color from colormap: use norm if set, else linear mapping.
    // NaN t -> cmap.bad (or transparent); out-of-range -> under/over.
    auto colorOf = [&](float val) {
        float t = config_.norm ? (*config_.norm)(val)
                               : (val - valueRange_.min) / vspan;
        return cmap.sample(t);
    };

    if (config_.shading == PcmShading::Gouraud) {
        // mpl QuadMesh._convert_mesh_to_triangles: each quad is split
        // into 4 triangles meeting at the center vertex, whose position
        // and color are the average of the 4 corners. Per-corner colors
        // come from C at the corner grid points.
        for (uint32_t j = 0; j + 1 < nRows_; ++j) {
            for (uint32_t i = 0; i + 1 < nCols_; ++i) {
                float va = C_[j * nCols_ + i];         // a = (i,   j)
                float vb = C_[j * nCols_ + i + 1];     // b = (i+1, j)
                float vc = C_[(j+1) * nCols_ + i + 1]; // c = (i+1, j+1)
                float vd = C_[(j+1) * nCols_ + i];     // d = (i,   j+1)
                // mpl drops a gouraud quad when any corner is masked.
                if (config_.skipNaN && !cmap.bad &&
                    (std::isnan(va) || std::isnan(vb) ||
                     std::isnan(vc) || std::isnan(vd)))
                    continue;

                Point2D pa{x_[i],   y_[j]};
                Point2D pb{x_[i+1], y_[j]};
                Point2D pc{x_[i+1], y_[j+1]};
                Point2D pd{x_[i],   y_[j+1]};
                Point2D pCtr{(pa.x + pb.x + pc.x + pd.x) * 0.25f,
                             (pa.y + pb.y + pc.y + pd.y) * 0.25f};

                Color ca = colorOf(va), cb = colorOf(vb);
                Color cc = colorOf(vc), cd = colorOf(vd);
                Color cCtr{(ca.r + cb.r + cc.r + cd.r) * 0.25f,
                           (ca.g + cb.g + cc.g + cd.g) * 0.25f,
                           (ca.b + cb.b + cc.b + cd.b) * 0.25f,
                           (ca.a + cb.a + cc.a + cd.a) * 0.25f};

                const Point2D* pts[12] = {
                    &pa, &pb, &pCtr, &pb, &pc, &pCtr,
                    &pc, &pd, &pCtr, &pd, &pa, &pCtr};
                const Color* cls[12] = {
                    &ca, &cb, &cCtr, &cb, &cc, &cCtr,
                    &cc, &cd, &cCtr, &cd, &ca, &cCtr};
                for (int k = 0; k < 12; ++k) {
                    fillPositions_.push_back(*pts[k]);
                    fillColors_.push_back(*cls[k]);
                }
            }
        }
        return;
    }

    for (uint32_t j = 0; j < nRows_; ++j) {
        for (uint32_t i = 0; i < nCols_; ++i) {
            float val = C_[j * nCols_ + i];
            if (config_.skipNaN && std::isnan(val) && !cmap.bad) continue;

            // Cell corners.
            float x0 = x_[i], x1 = x_[i + 1];
            float y0 = y_[j], y1 = y_[j + 1];

            Color color = colorOf(val);

            // Two triangles per cell.
            Point2D bl{x0, y0}, br{x1, y0}, ul{x0, y1}, ur{x1, y1};
            // Triangle 1: bl, br, ul
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ul);
            // Triangle 2: br, ur, ul
            fillPositions_.push_back(br);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ul);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(color);
        }
    }
}

bool PcolormeshPlot::buildGeometryGpu(render::Renderer& r) {
    auto& ctx = r.backend().context();
    vk::Device dev = ctx.device.handle();
    vk::Queue queue = ctx.device.graphicsQueue();
    vk::CommandPool pool = ctx.graphicsPool.handle();
    VmaAllocator alloc = ctx.allocator.handle();

    const bool gouraud = config_.shading == PcmShading::Gouraud;
    const uint32_t cells = gouraud ? (nCols_ - 1) * (nRows_ - 1)
                                   : nCols_ * nRows_;
    const uint32_t vertsPerCell = gouraud ? 12 : 6;
    const uint64_t nVerts = uint64_t(cells) * vertsPerCell;
    if (cells == 0 || nVerts > (1ull << 31)) return false;

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Per-cell / per-corner normalized t on the CPU — one evaluation per
    // value, so arbitrary polymorphic norms stay supported.
    std::vector<float> tvals(C_.size());
    for (size_t i = 0; i < C_.size(); ++i) {
        float v = C_[i];
        tvals[i] = std::isnan(v) ? std::numeric_limits<float>::quiet_NaN()
                   : config_.norm ? (*config_.norm)(v)
                                  : (v - valueRange_.min) / vspan;
    }

    // 259-entry LUT: [0..255] regular, 256 under, 257 over, 258 bad.
    std::vector<Color> lut(259);
    for (int i = 0; i < 256; ++i)
        lut[i] = cmap.sample(float(i) / 255.0f);
    lut[256] = cmap.under.value_or(cmap.sample(0.0f));
    lut[257] = cmap.over.value_or(cmap.sample(1.0f));
    lut[258] = cmap.bad.value_or(Color::transparent());

    // --- pipeline (built per prepare; cheap relative to big meshes) ---
    auto spv = core::ShaderModule::compileGlsl(kPcmTessGlsl, "comp");
    core::ShaderModule shader(dev, spv);
    vk::DescriptorSetLayoutBinding bindings[6];
    for (uint32_t b = 0; b < 6; ++b)
        bindings[b].setBinding(b)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(bindings);
    auto descLayout = dev.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pcr{};
    pcr.setStageFlags(vk::ShaderStageFlagBits::eCompute).setOffset(0).setSize(16);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descLayout.get()).setPushConstantRanges(pcr);
    auto pipeLayout = dev.createPipelineLayoutUnique(plci);

    vk::ComputePipelineCreateInfo ci{};
    ci.stage.setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(shader.handle()).setPName("main");
    ci.setLayout(pipeLayout.get());
    auto res = dev.createComputePipelineUnique(nullptr, ci);
    if (res.result != vk::Result::eSuccess) return false;
    auto pipe = std::move(res.value);

    // --- buffers ---
    auto mkStorage = [&](const void* data, size_t bytes) {
        core::BufferDesc d{};
        d.size = bytes;
        d.usage = core::BufferUsage::Storage;
        core::Buffer buf(alloc, d);
        buf.upload(dev, queue, pool,
                   std::span{static_cast<const std::byte*>(data), bytes});
        return buf;
    };
    core::Buffer xBuf = mkStorage(x_.data(), x_.size() * 4);
    core::Buffer yBuf = mkStorage(y_.data(), y_.size() * 4);
    core::Buffer tBuf = mkStorage(tvals.data(), tvals.size() * 4);
    core::Buffer lutBuf = mkStorage(lut.data(), lut.size() * sizeof(Color));

    core::BufferDesc pdesc{};
    pdesc.size = nVerts * sizeof(Point2D);
    pdesc.usage = core::BufferUsage::VertexStorage;
    core::Buffer posBuf(alloc, pdesc);
    core::BufferDesc cdesc{};
    cdesc.size = nVerts * sizeof(Color);
    cdesc.usage = core::BufferUsage::VertexStorage;
    core::Buffer colBuf(alloc, cdesc);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(6);
    core::DescriptorPool descPool(dev, {ps}, 1);
    vk::DescriptorSet dset = descPool.allocate(descLayout.get());

    vk::DescriptorBufferInfo infos[6];
    std::array<std::pair<vk::Buffer, vk::DeviceSize>, 6> bufs{{
        {xBuf.handle(), xBuf.size()}, {yBuf.handle(), yBuf.size()},
        {tBuf.handle(), tBuf.size()}, {lutBuf.handle(), lutBuf.size()},
        {posBuf.handle(), posBuf.size()}, {colBuf.handle(), colBuf.size()}}};
    vk::WriteDescriptorSet writes[6];
    for (uint32_t b = 0; b < 6; ++b) {
        infos[b].setBuffer(bufs[b].first).setOffset(0).setRange(bufs[b].second);
        writes[b].setDstSet(dset).setDstBinding(b)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(infos[b]);
    }
    dev.updateDescriptorSets(writes, {});

    {
        core::OneTimeCommands cmd(dev, pool, queue);
        auto c = cmd.handle();
        c.bindPipeline(vk::PipelineBindPoint::eCompute, pipe.get());
        c.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             pipeLayout.get(), 0, dset, {});
        uint32_t pcv[4] = {nCols_, nRows_, gouraud ? 1u : 0u,
                           (cmap.bad ? 1u : 0u) | (config_.skipNaN ? 2u : 0u)};
        c.pushConstants(pipeLayout.get(), vk::ShaderStageFlagBits::eCompute,
                        0, 16, pcv);
        c.dispatch((cells + 255) / 256, 1, 1);
        // Compute writes -> vertex attribute reads.
        vk::BufferMemoryBarrier barriers[2];
        for (uint32_t b = 0; b < 2; ++b) {
            barriers[b].setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead)
                .setBuffer(bufs[4 + b].first).setOffset(0)
                .setSize(bufs[4 + b].second);
        }
        c.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eVertexInput,
                          {}, {}, barriers, {});
    }

    fillRenderer_.adoptBuffers(std::move(posBuf), std::move(colBuf),
                             uint32_t(nVerts));
    return true;
}

void PcolormeshPlot::prepare(render::Renderer& r) {
    computeValueRange();
    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    const bool gouraud = config_.shading == PcmShading::Gouraud;
    const uint64_t cells = gouraud ? uint64_t(nCols_ - 1) * (nRows_ - 1)
                                   : uint64_t(nCols_) * nRows_;
    bool gpu = config_.gpuTessellate > 0 ||
               (config_.gpuTessellate < 0 && cells >= 16384);
    if (gpu && !buildGeometryGpu(r))
        gpu = false;
    if (!gpu) {
        buildGeometry();
        if (!fillPositions_.empty()) {
            fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                                 ctx.graphicsPool.handle(), ctx.allocator.handle(),
                                 std::span{fillPositions_}, std::span{fillColors_});
        }
    }
    prepared_ = true;
}

void PcolormeshPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillRenderer_.pointCount() == 0) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void PcolormeshPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    for (float yv : y_) {
        v.y.min = std::min(v.y.min, yv);
        v.y.max = std::max(v.y.max, yv);
    }
}

} // namespace volcano::plot
