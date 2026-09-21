// volcano/render/primitives/GpuLineRenderer.cpp
#include "volcano/render/primitives/GpuLineRenderer.hpp"

#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <cstring>

namespace volcano::render::primitives {

namespace {

// One invocation per element: elements [0, nSeg) are segment quads,
// elements [nSeg, nSeg+n) are per-point join/cap slots. Unused slot
// verts are written as zero-area alpha-0 triangles.
// Slot layout must match kSegVerts / kJoinVerts in the header.
const char* kTessGlsl = R"GLSL(
#version 450
layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer InPts { vec2 pts[]; };
layout(std430, binding = 1) writeonly buffer OutVerts { float v[]; };

layout(push_constant) uniform PC {
    uint n;            // point count
    uint nSeg;         // n-1
    float hwidth;      // stroke half-width px
    uint join;         // 0 miter, 1 round, 2 bevel
    uint cap;          // 0 butt, 1 round, 2 square
    float miterLimit;  // x half-width
    uint inBase;       // first point index
    uint outBase;      // first out vertex index
    vec4 color;
} pc;

const uint SEGV = 6u;
const uint JOINV = 24u;

vec2 perp(vec2 d) { return vec2(-d.y, d.x); }

bool finitePt(uint i) {
    vec2 p = pts[pc.inBase + i];
    return p.x == p.x && p.y == p.y &&
           abs(p.x) < 1e30 && abs(p.y) < 1e30;
}

void emitVert(uint idx, vec2 p) {
    uint b = (pc.outBase + idx) * 6u;
    v[b+0]=p.x; v[b+1]=p.y;
    v[b+2]=pc.color.r; v[b+3]=pc.color.g;
    v[b+4]=pc.color.b; v[b+5]=pc.color.a;
}
void emitTri(uint idx, vec2 a, vec2 b, vec2 c) {
    emitVert(idx, a); emitVert(idx+1u, b); emitVert(idx+2u, c);
}
void zeroSlot(uint idx, uint count) {
    for (uint k = 0u; k < count; ++k) {
        uint b = (pc.outBase + idx + k) * 6u;
        for (uint f = 0u; f < 6u; ++f) v[b+f] = 0.0;
    }
}

// Arc fan centered at `c` radius `h` sweeping from `from` to `to`
// through `through` (all points on/near the circle), apex `apex`.
// Writes `steps` triangles starting at slot idx.
void arcFan(uint idx, vec2 apex, vec2 c, float h, vec2 from, vec2 to,
            vec2 through, uint steps) {
    const float TAU = 6.28318530718;
    float a0 = atan(from.y - c.y, from.x - c.x);
    float a1 = atan(to.y - c.y, to.x - c.x);
    float am = atan(through.y - c.y, through.x - c.x);
    // CCW span a0->a1 in (0, TAU]; if `am` is not on that arc, sweep CW.
    float ccw = mod(a1 - a0, TAU);
    if (ccw < 1e-6) ccw = TAU;
    float mOnCcw = mod(am - a0, TAU);
    float dir = (mOnCcw <= ccw) ? 1.0 : -1.0;
    float span = dir > 0.0 ? ccw : -(TAU - ccw);
    vec2 prev = from;
    uint vi = idx;
    for (uint k = 1u; k <= steps; ++k) {
        float a = a0 + span * float(k) / float(steps);
        vec2 cur = c + vec2(cos(a), sin(a)) * h;
        emitTri(vi, apex, prev, cur);
        prev = cur;
        vi += 3u;
    }
}

void main() {
    uint e = gl_GlobalInvocationID.x;
    if (e >= pc.nSeg + pc.n) return;
    if (pc.n < 2u) return;

    if (e < pc.nSeg) {
        // --- segment quad for pts[i] -> pts[i+1] ---
        uint i = e;
        uint slot = i * SEGV;
        vec2 a = pts[pc.inBase + i];
        vec2 b = pts[pc.inBase + i + 1u];
        vec2 d = b - a;
        float len = length(d);
        if (len < 1e-6 || !finitePt(i) || !finitePt(i + 1u)) {
            zeroSlot(slot, SEGV);
            return;
        }
        vec2 n = perp(d / len) * pc.hwidth;
        emitTri(slot,     a - n, a + n, b + n);
        emitTri(slot + 3u, a - n, b + n, b - n);
        return;
    }

    // --- per-point join or cap slot ---
    uint i = e - pc.nSeg;
    uint slot = pc.nSeg * SEGV + i * JOINV;
    bool prevOk = i > 0u && finitePt(i - 1u) && finitePt(i);
    bool nextOk = i + 1u < pc.n && finitePt(i + 1u) && finitePt(i);
    vec2 p = pts[pc.inBase + i];

    if (prevOk && nextOk) {
        // join between segment i-1 and i
        vec2 pa = pts[pc.inBase + i - 1u];
        vec2 pb = pts[pc.inBase + i + 1u];
        vec2 d0 = p - pa, d1 = pb - p;
        float l0 = length(d0), l1 = length(d1);
        if (l0 < 1e-6 || l1 < 1e-6) { zeroSlot(slot, JOINV); return; }
        d0 /= l0; d1 /= l1;
        vec2 n0 = perp(d0), n1 = perp(d1);
        float cross = d0.x * d1.y - d0.y * d1.x;
        if (abs(cross) < 1e-6) { zeroSlot(slot, JOINV); return; }
        float s = cross > 0.0 ? 1.0 : -1.0;
        vec2 oA = p + n0 * (s * pc.hwidth);
        vec2 oB = p + n1 * (s * pc.hwidth);
        if (pc.join == 0u) {
            // miter: apex along the normal bisector, length h/dot(m,n0);
            // beyond miterLimit the join degrades to bevel (no fill).
            vec2 m = n0 + n1;
            float ml = length(m);
            vec2 mdir = ml > 1e-6 ? m / ml : n0;
            float dt = max(dot(mdir, n0), 1e-6);
            if (dt >= 1.0 / pc.miterLimit) {
                emitTri(slot, oA, p + mdir * (s * pc.hwidth / dt), oB);
                zeroSlot(slot + 3u, JOINV - 3u);
            } else {
                zeroSlot(slot, JOINV);
            }
        } else if (pc.join == 1u) {
            // round: fan around p from oA to oB through the outer side
            arcFan(slot, p, p, pc.hwidth, oA, oB,
                   p + (n0 + n1) * s, 8u);
        } else {
            zeroSlot(slot, JOINV);       // bevel: chord already covered
        }
        return;
    }

    if (!finitePt(i)) { zeroSlot(slot, JOINV); return; }

    // cap: run start when the previous point is missing/NaN
    bool isStart = !prevOk && nextOk;
    bool isEnd = prevOk && !nextOk;
    if (!isStart && !isEnd) { zeroSlot(slot, JOINV); return; }
    if (pc.cap == 0u) { zeroSlot(slot, JOINV); return; }

    vec2 other = isStart ? pts[pc.inBase + i + 1u]
                         : pts[pc.inBase + i - 1u];
    vec2 d = other - p;
    float len = length(d);
    if (len < 1e-6) { zeroSlot(slot, JOINV); return; }
    d /= len;
    if (isStart) d = -d;         // outward dir
    vec2 n = perp(d) * pc.hwidth;

    if (pc.cap == 2u) {
        // square: quad extending half-width outward
        vec2 o = p + d * pc.hwidth;
        emitTri(slot,     p - n, p + n, o + n);
        emitTri(slot + 3u, p - n, o + n, o - n);
        zeroSlot(slot + 6u, JOINV - 6u);
    } else {
        // round: semicircle fan centered at p spanning ±n
        arcFan(slot, p, p, pc.hwidth, p - n, p + n, p + d, 8u);
    }
}
)GLSL";

} // namespace

void GpuLineRenderer::init(vk::Device device, VmaAllocator allocator,
                           core::DescriptorPool& descPool,
                           core::PipelineCache& cache) {
    if (inited_) return;
    device_ = device;
    allocator_ = allocator;
    descPool_ = &descPool;

    auto spv = core::ShaderModule::compileGlsl(kTessGlsl, "comp");
    core::ShaderModule shader(device, spv);

    vk::DescriptorSetLayoutBinding bindings[2];
    for (uint32_t b = 0; b < 2; ++b)
        bindings[b].setBinding(b)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(bindings);
    descLayout_ = device.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pcr{};
    pcr.setStageFlags(vk::ShaderStageFlagBits::eCompute)
       .setOffset(0).setSize(48);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descLayout_.get()).setPushConstantRanges(pcr);
    pipeLayout_ = device.createPipelineLayoutUnique(plci);

    vk::ComputePipelineCreateInfo ci{};
    ci.stage.setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(shader.handle()).setPName("main");
    ci.setLayout(pipeLayout_.get());
    auto res = device.createComputePipelineUnique(cache.handle(), ci);
    if (res.result != vk::Result::eSuccess)
        throw std::runtime_error("GpuLineRenderer pipeline creation failed");
    pipe_ = std::move(res.value);

    dset_ = descPool.allocate(descLayout_.get());
    inited_ = true;
}

void GpuLineRenderer::resetScratch() {
    inOff_ = 0;
    outOff_ = 0;
    retiredIn_.clear();
    retiredOut_.clear();
}

void GpuLineRenderer::ensureIn(size_t points) {
    if (inOff_ + points <= inBuf_.size() / sizeof(float) / 2) return;
    retiredIn_.push_back(std::move(inBuf_));
    size_t cap = std::max<size_t>(points + inOff_, 4096) * 2;
    core::BufferDesc d{};
    d.size = cap * sizeof(float) * 2;
    d.usage = core::BufferUsage::Storage;
    d.hostVisible = true;
    inBuf_ = core::Buffer(allocator_, d);
    rebind();
}

void GpuLineRenderer::ensureOut(size_t verts) {
    if (outOff_ + verts <= outBuf_.size() / (sizeof(float) * 6)) return;
    retiredOut_.push_back(std::move(outBuf_));
    size_t cap = std::max<size_t>(verts + outOff_, 16384) * 2;
    core::BufferDesc d{};
    d.size = cap * sizeof(float) * 6;
    d.usage = core::BufferUsage::VertexStorage;
    outBuf_ = core::Buffer(allocator_, d);
    rebind();
}

void GpuLineRenderer::rebind() {
    if (!inBuf_.handle() || !outBuf_.handle()) return;
    vk::DescriptorBufferInfo infos[2];
    infos[0].setBuffer(inBuf_.handle()).setOffset(0).setRange(inBuf_.size());
    infos[1].setBuffer(outBuf_.handle()).setOffset(0).setRange(outBuf_.size());
    vk::WriteDescriptorSet writes[2];
    for (uint32_t b = 0; b < 2; ++b)
        writes[b].setDstSet(dset_).setDstBinding(b)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(infos[b]);
    device_.updateDescriptorSets(writes, {});
}

GpuLineRenderer::Mesh
GpuLineRenderer::tessellate(vk::CommandBuffer cmd,
                            std::span<const plot::Point2D> px,
                            const plot::StrokeParams& sp,
                            plot::Color color) {
    const uint32_t n = uint32_t(px.size());
    if (n < 2) return {};
    const uint32_t nSeg = n - 1;
    const size_t outVerts = size_t(nSeg) * kSegVerts + size_t(n) * kJoinVerts;

    ensureIn(n);
    ensureOut(outVerts);

    // Upload points into the host-visible input buffer at the bump offset.
    auto* dst = static_cast<char*>(inBuf_.mappedData()) +
                inOff_ * 2 * sizeof(float);
    std::memcpy(dst, px.data(), n * sizeof(plot::Point2D));

    struct PC {
        uint32_t n, nSeg;
        float hwidth;
        uint32_t join, cap;
        float miterLimit;
        uint32_t inBase, outBase;
        float r, g, b, a;
    } pc{n, nSeg, sp.width * 0.5f,
         uint32_t(sp.join), uint32_t(sp.cap), sp.miterLimit,
         uint32_t(inOff_), uint32_t(outOff_),
         color.r, color.g, color.b, color.a};

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipe_.get());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           pipeLayout_.get(), 0, dset_, {});
    cmd.pushConstants(pipeLayout_.get(), vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(PC), &pc);
    cmd.dispatch((n + nSeg + 255) / 256, 1, 1);

    vk::BufferMemoryBarrier bar{};
    bar.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
       .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead)
       .setBuffer(outBuf_.handle()).setOffset(0).setSize(outBuf_.size());
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eVertexInput,
                        {}, {}, bar, {});

    Mesh m;
    m.buffer = outBuf_.handle();
    m.firstVertex = uint32_t(outOff_);
    m.vertexCount = uint32_t(outVerts);
    inOff_ += n;
    outOff_ += outVerts;
    return m;
}

} // namespace volcano::render::primitives
