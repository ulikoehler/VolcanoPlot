// volcano/render/primitives/PointRenderer.cpp
#include "volcano/render/primitives/PointRenderer.hpp"
#include "volcano/core/Device.hpp"
#include "volcano/core/DescriptorPool.hpp"

#include <volcano/plot/Transform.hpp>
#include "../shaders/TransformGlsl.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace volcano::render::primitives {

namespace {

// Embedded SPIR-V source (compiled at runtime via shaderc if available,
// otherwise loaded from precompiled .spv files).
constexpr const char* kVertHead = R"(
#version 460

layout(location = 0) in vec2 a_pos;       // data coords
layout(location = 1) in vec4 a_color;     // RGBA
layout(location = 2) in float a_size;     // pixel size

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;   // xy = min, zw = span
    vec4 u_rect;          // xy = offset, zw = extent (for point size scaling)
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
} pc;

layout(location = 0) out vec4 v_color;
layout(location = 1) out float v_size;
)";

constexpr const char* kVertMain = R"(
void main() {
    vec2 p = projFwd(vec2(scaleFwd(a_pos.x, pc.u_scaleX.xyz),
                          scaleFwd(a_pos.y, pc.u_scaleY.xyz)),
                     pc.u_proj.xyz);
    // Map data coords to NDC [-1,1] — viewport handles pixel mapping.
    vec2 ndc = (p - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw * 2.0 - 1.0;
    // Vulkan Y is down, flip to conventional math Y-up.
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    gl_PointSize = a_size;
    v_color = a_color;
    v_size = a_size;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 1) in float v_size;
layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;
    vec4 u_rect;
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
    vec4 u_marker;      // x=style code, y=fill, z=numsides, w=angle rad
} pc;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// iq's regular n-gon SDF. r = circumradius; rot rotates the marker.
float sdPoly(vec2 p, float r, int n, float rot) {
    float c = cos(rot), s = sin(rot);
    p = mat2(c, s, -s, c) * p;
    float an = PI / float(n);
    vec2 acs = vec2(cos(an), sin(an));
    float bn = mod(atan(p.x, p.y), 2.0 * an) - an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= r * acs;
    p.y += clamp(-p.y, 0.0, r * acs.y);
    return length(p) * sign(p.x);
}

// iq's exact 5-point star SDF. rf = inner/outer radius ratio
// (~0.382 = golden ratio for a classic star).
float sdStar5(vec2 p, float r, float rf, float rot) {
    float c = cos(rot), s = sin(rot);
    p = mat2(c, s, -s, c) * p;
    vec2 k1 = vec2(0.809016994375, -0.587785252292);
    vec2 k2 = vec2(-k1.x, k1.y);
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= r;
    vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Filled-plus ('P') or filled-x ('X', rot = PI/4) SDF.
float sdCross(vec2 p, float rot) {
    float c = cos(rot), s = sin(rot);
    p = mat2(c, s, -s, c) * p;
    return min(sdBox(p, vec2(0.8, 0.28)), sdBox(p, vec2(0.28, 0.8)));
}

// n spokes from the center (asterisk / tripod family).
float sdSpokes(vec2 p, int n, float len, float rot, float phase) {
    float d = 1e9;
    for (int i = 0; i < 12; ++i) {
        if (i >= n) break;
        float a = rot + phase + float(i) * (2.0 * PI / float(n));
        d = min(d, sdSeg(p, vec2(0.0), vec2(cos(a), sin(a)) * len));
    }
    return d;
}

// Chevron caret pointing in direction `rot` (0 = up).
float sdCaret(vec2 p, float rot) {
    float c = cos(rot), s = sin(rot);
    p = mat2(c, s, -s, c) * p;
    return min(sdSeg(p, vec2(-0.55, 0.25), vec2(0.0, -0.45)),
               sdSeg(p, vec2(0.0, -0.45), vec2(0.55, 0.25)));
}

float markerDist(vec2 c, int code, int nside, float rot) {
    switch (code) {
    case 0:  return length(c) - 0.30;                    // '.' point
    case 1:  return length(c) - 1.00;                    // 'o' circle
    case 2:  return sdBox(c, vec2(0.75));                // 's' square
    case 3:  return sdPoly(c, 0.9, 4, 0.0);              // 'D' diamond
    case 4:  return sdPoly(vec2(c.x, c.y * 0.6), 0.9, 4, 0.0); // 'd'
    case 5:  return sdPoly(c, 0.95, 3, PI);              // '^'
    case 6:  return sdPoly(c, 0.95, 3, 0.0);             // 'v'
    case 7:  return sdPoly(c, 0.95, 3, PI / 2.0);        // '<'
    case 8:  return sdPoly(c, 0.95, 3, -PI / 2.0);       // '>'
    case 9:  return sdSpokes(c, 3, 0.9, PI / 2.0, 0.0) - 0.10;      // '1'
    case 10: return sdSpokes(c, 3, 0.9, -PI / 2.0, 0.0) - 0.10;     // '2'
    case 11: return sdSpokes(c, 3, 0.9, 0.0, 0.0) - 0.10;           // '3'
    case 12: return sdSpokes(c, 3, 0.9, PI, 0.0) - 0.10;            // '4'
    case 13: return min(sdSeg(c, vec2(-0.7, 0.0), vec2(0.7, 0.0)),
                        sdSeg(c, vec2(0.0, -0.7), vec2(0.0, 0.7))) - 0.08; // '+'
    case 14: return min(sdSeg(c, vec2(-0.5, -0.5), vec2(0.5, 0.5)),
                        sdSeg(c, vec2(-0.5, 0.5), vec2(0.5, -0.5))) - 0.08; // 'x'
    case 15: return sdCross(c, 0.0);                     // 'P'
    case 16: return sdCross(c, PI / 4.0);                // 'X'
    case 17: return sdStar5(c, 0.95, 0.382, PI);         // '*'
    case 18: return sdPoly(c, 0.9, 5, PI);               // 'p'
    case 19: return sdPoly(c, 0.9, 6, 0.0);              // 'h'
    case 20: return sdPoly(c, 0.9, 6, PI / 6.0);         // 'H'
    case 21: return sdPoly(c, 0.9, 8, PI / 8.0);         // '8'
    case 22: return sdSeg(c, vec2(0.0, -0.8), vec2(0.0, 0.8)) - 0.10;   // '|'
    case 23: return sdSeg(c, vec2(-0.8, 0.0), vec2(0.8, 0.0)) - 0.10;   // '_'
    case 24: return sdSeg(c, vec2(-0.8, 0.0), vec2(0.0, 0.0)) - 0.08;   // TICKLEFT
    case 25: return sdSeg(c, vec2(0.0, 0.0), vec2(0.8, 0.0)) - 0.08;    // TICKRIGHT
    case 26: return sdSeg(c, vec2(0.0, -0.8), vec2(0.0, 0.0)) - 0.08;   // TICKUP
    case 27: return sdSeg(c, vec2(0.0, 0.0), vec2(0.0, 0.8)) - 0.08;    // TICKDOWN
    case 28: return sdCaret(c, -PI / 2.0) - 0.08;        // CARETLEFT
    case 29: return sdCaret(c, PI / 2.0) - 0.08;         // CARETRIGHT
    case 30: return sdCaret(c, 0.0) - 0.08;              // CARETUP
    case 31: return sdCaret(c, PI) - 0.08;               // CARETDOWN
    case 32: return min(sdCaret(c, -PI / 2.0),
                        sdSeg(c, vec2(0.3, 0.0), vec2(0.8, 0.0))) - 0.08;
    case 33: return min(sdCaret(c, PI / 2.0),
                        sdSeg(c, vec2(-0.8, 0.0), vec2(-0.3, 0.0))) - 0.08;
    case 34: return min(sdCaret(c, 0.0),
                        sdSeg(c, vec2(0.0, 0.3), vec2(0.0, 0.8))) - 0.08;
    case 35: return min(sdCaret(c, PI),
                        sdSeg(c, vec2(0.0, -0.8), vec2(0.0, -0.3))) - 0.08;
    case 36: return sdPoly(c, 0.9, max(nside, 3), rot);       // (n,0)
    case 37: {  // (n,1) star-like polygon
        if (nside <= 5) return sdStar5(c, 0.95, 0.382, rot + PI);
        // n>5: approximate with a regular 2n-gon (SDF n-star is
        // unreliable for large n).
        return sdPoly(c, 0.95, max(nside, 3), rot + PI);
    }
    case 38: return sdSpokes(c, max(nside, 3), 0.85, rot, 0.0) - 0.06; // (n,2)
    case 39: return length(c) - 0.85;                    // (n,3) ≈ circle
    default: return 1.0;                                 // none / unknown
    }
}

void main() {
    int code = int(pc.u_marker.x + 0.5);
    if (code == 40) discard;                             // MarkerStyle::None
    int fill = int(pc.u_marker.y + 0.5);
    int nside = int(pc.u_marker.z + 0.5);
    float rot = pc.u_marker.w;

    vec2 c = gl_PointCoord * 2.0 - 1.0;                  // Y down, [-1,1]
    float d = markerDist(c, code, nside, rot);

    // Anti-alias width ~1.2px in marker space (1 unit = size/2 px),
    // clamped so small markers keep most of their area.
    float aa = min(2.4 / max(v_size, 1.0), 0.12);
    float alpha;
    if (fill == 5) {
        // Outline only (fillstyle 'none').
        float sw = min(3.6 / max(v_size, 1.0), 0.25);
        alpha = 1.0 - smoothstep(sw - aa, sw + aa, abs(d));
    } else {
        // Half fills keep only one side of the marker.
        if ((fill == 1 && c.x > 0.0) || (fill == 2 && c.x < 0.0) ||
            (fill == 4 && c.y > 0.0) || (fill == 3 && c.y < 0.0))
            discard;
        alpha = 1.0 - smoothstep(-aa, aa, d);
    }
    if (alpha <= 0.0) discard;
    outColor = vec4(v_color.rgb, v_color.a * alpha);
}
)";

} // namespace

void PointRenderer::init(vk::Device device, vk::RenderPass renderPass,
                         vk::SampleCountFlagBits samples, core::DescriptorPool& /*descPool*/,
                         core::PipelineCache& cache) {
    device_ = device;
    auto vertSrc = std::string(kVertHead) + shaders::kScaleFn +
                   shaders::kProjFn + kVertMain;
    auto vertSpv = core::ShaderModule::compileGlsl(vertSrc, "vert");
    auto fragSpv = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, vertSpv);
    frag_ = core::ShaderModule(device, fragSpv);

    vk::PipelineLayoutCreateInfo plci{};
    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex |
                     vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 24);
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription binding{};
    binding.setBinding(0).setStride(sizeof(plot::Point2D)).setInputRate(vk::VertexInputRate::eVertex);
    vk::VertexInputAttributeDescription attrs[3];
    attrs[0].setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    attrs[1].setLocation(1).setBinding(1).setFormat(vk::Format::eR32G32B32A32Sfloat).setOffset(0);
    attrs[2].setLocation(2).setBinding(2).setFormat(vk::Format::eR32Sfloat).setOffset(0);

    vk::VertexInputBindingDescription bindings[3] = {
        {0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex},
        {1, sizeof(plot::Color),   vk::VertexInputRate::eVertex},
        {2, sizeof(float),         vk::VertexInputRate::eVertex},
    };

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(bindings).setVertexAttributeDescriptions(attrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::ePointList);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples)
        .setSampleShadingEnable(false);

    // Depth testing disabled (2D overlay).
    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att{};
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorBlendOp(vk::BlendOp::eAdd)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setAlphaBlendOp(vk::BlendOp::eAdd)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendStateCreateInfo cbsci{};
    cbsci.setLogicOpEnable(false).setAttachments(att);

    std::vector<vk::DynamicState> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci{};
    dsci.setDynamicStates(dyn);

    vk::GraphicsPipelineCreateInfo gpci{};
    gpci.setStages(stages)
        .setPVertexInputState(&visci)
        .setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci)
        .setPRasterizationState(&rsci)
        .setPMultisampleState(&msci)
        .setPDepthStencilState(&depthState)
        .setPColorBlendState(&cbsci)
        .setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get())
        .setRenderPass(renderPass)
        .setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Failed to create point pipeline");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void PointRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                           VmaAllocator allocator, std::span<const plot::Point2D> points,
                           std::span<const plot::Color> colors, std::span<const float> sizes) {
    allocator_ = allocator;
    core::BufferDesc pdesc{};
    pdesc.size = points.size_bytes();
    pdesc.usage = core::BufferUsage::VertexStorage;
    pdesc.hostVisible = true;   // dynamic data — in-place memcpy updates
    pointBuffer_ = core::Buffer(allocator, pdesc);
    pointBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{points.data(), points.size()}));
    count_ = static_cast<uint32_t>(points.size());
    capacity_ = count_;

    core::BufferDesc cdesc{};
    cdesc.size = colors.size_bytes();
    cdesc.usage = core::BufferUsage::Vertex;
    cdesc.hostVisible = true;
    colorBuffer_ = core::Buffer(allocator, cdesc);
    colorBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{colors.data(), colors.size()}));

    core::BufferDesc sdesc{};
    sdesc.size = sizes.size_bytes();
    sdesc.usage = core::BufferUsage::Vertex;
    sdesc.hostVisible = true;
    sizeBuffer_ = core::Buffer(allocator, sdesc);
    sizeBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{sizes.data(), sizes.size()}));
}

void PointRenderer::updatePoints(std::span<const plot::Point2D> points,
                                 std::span<const plot::Color> colors,
                                 std::span<const float> sizes) {
    if (points.size() <= capacity_) {
        std::memcpy(pointBuffer_.mappedData(), points.data(),
                    points.size_bytes());
        std::memcpy(colorBuffer_.mappedData(), colors.data(),
                    colors.size_bytes());
        std::memcpy(sizeBuffer_.mappedData(), sizes.data(),
                    sizes.size_bytes());
        count_ = static_cast<uint32_t>(points.size());
        return;
    }
    auto reallocUpload = [&](core::Buffer& buf, core::BufferUsage usage,
                             std::span<const std::byte> bytes) {
        core::BufferDesc d{};
        d.size = bytes.size();
        d.usage = usage;
        d.hostVisible = true;
        buf = core::Buffer(allocator_, d);
        std::memcpy(buf.mappedData(), bytes.data(), bytes.size());
    };
    reallocUpload(pointBuffer_, core::BufferUsage::VertexStorage,
                  std::as_bytes(points));
    reallocUpload(colorBuffer_, core::BufferUsage::Vertex,
                  std::as_bytes(colors));
    reallocUpload(sizeBuffer_, core::BufferUsage::Vertex,
                  std::as_bytes(sizes));
    count_ = static_cast<uint32_t>(points.size());
    capacity_ = count_;
}

void PointRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                         const plot::Transform2D& transform, uint32_t pointCount,
                         MarkerParams marker) const {
    if (!inited_ || pointCount == 0) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float rectX, rectY, rectW, rectH;
        float sxCode, sxP1, sxP2, sxPad;
        float syCode, syP1, syP2, syPad;
        float prCode, thetaOff, thetaDir, prPad;
        float mCode, mFill, mSides, mAngle;
    } pc;
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.rectX = static_cast<float>(rect.offset.x);
    pc.rectY = static_cast<float>(rect.offset.y);
    pc.rectW = static_cast<float>(rect.extent.width);
    pc.rectH = static_cast<float>(rect.extent.height);
    pc.sxCode = static_cast<float>(static_cast<int>(transform.codeX()));
    pc.sxP1 = transform.scaleX.param1;
    pc.sxP2 = transform.scaleX.param2;
    pc.syCode = static_cast<float>(static_cast<int>(transform.codeY()));
    pc.syP1 = transform.scaleY.param1;
    pc.syP2 = transform.scaleY.param2;
    pc.prCode = static_cast<float>(static_cast<int>(transform.projection.kind));
    pc.thetaOff = transform.projection.thetaOffset;
    pc.thetaDir = transform.projection.thetaDir;
    pc.mCode = marker.code;
    pc.mFill = marker.fill;
    pc.mSides = marker.numsides;
    pc.mAngle = marker.angle;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.pushConstants(pipelineLayout_.get(),
                      vk::ShaderStageFlagBits::eVertex |
                      vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PC), &pc);

    vk::Viewport vp;
    vp.setX(static_cast<float>(rect.offset.x))
       .setY(static_cast<float>(rect.offset.y))
       .setWidth(static_cast<float>(rect.extent.width))
       .setHeight(static_cast<float>(rect.extent.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp);
    cmd.setScissor(0, rect);

    std::array<vk::Buffer, 3> buffers = { pointBuffer_.handle(), colorBuffer_.handle(), sizeBuffer_.handle() };
    std::array<vk::DeviceSize, 3> offsets = {0,0,0};
    cmd.bindVertexBuffers(0, buffers, offsets);
    cmd.draw(pointCount, 1, 0, 0);
}

} // namespace volcano::render::primitives
