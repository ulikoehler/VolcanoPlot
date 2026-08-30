// volcano/render/primitives/Grid3DRenderer.cpp — fwidth-based 3D dynamic grid
#include "volcano/render/Grid3DRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include <array>
#include <stdexcept>
#include <cmath>

namespace volcano::render {

namespace {

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos; // fullscreen triangle [-1,1]
layout(location = 0) out vec2 v_ndc;
layout(push_constant) uniform PC {
    vec4 u_rect;          // xy = offset, zw = extent
} pc;
void main() {
    v_ndc = a_pos;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
#extension GL_OES_standard_derivatives : enable
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec4 u_rect;          // xy = offset, zw = extent
    vec4 u_viewX;         // xy = x.min, z = x.span, w = unused
    vec4 u_viewY;         // xy = y.min, z = y.span, w = unused
    vec4 u_viewZ;         // xy = z.min, z = z.span, w = unused
    vec4 u_gridColor;     // rgba
    vec4 u_flags;         // x = floorXZ, y = backWallXY, z = sideWallYZ, w = step
    vec4 u_invVP0;        // row 0 of inverse VP
    vec4 u_invVP1;        // row 1
    vec4 u_invVP2;        // row 2
    vec4 u_invVP3;        // row 3
    vec4 u_eye;           // xyz = eye, w = unused
} pc;

vec3 unproject(vec3 ndc) {
    // invVP is row-major; GLSL mat4 constructor takes column-major.
    // So we need to transpose: mat4(col0, col1, col2, col3) where
    // col_i = row_i of our row-major matrix.
    mat4 invVP = mat4(pc.u_invVP0[0], pc.u_invVP1[0], pc.u_invVP2[0], pc.u_invVP3[0],
                      pc.u_invVP0[1], pc.u_invVP1[1], pc.u_invVP2[1], pc.u_invVP3[1],
                      pc.u_invVP0[2], pc.u_invVP1[2], pc.u_invVP2[2], pc.u_invVP3[2],
                      pc.u_invVP0[3], pc.u_invVP1[3], pc.u_invVP2[3], pc.u_invVP3[3]);
    vec4 world = invVP * vec4(ndc, 1.0);
    return world.xyz / world.w;
}

float gridLine(float coord, float step) {
    if (step <= 0.0) return 0.0;
    float f = abs(fract(coord / step - 0.5) - 0.5);
    float w = fwidth(coord / step);
    return 1.0 - smoothstep(0.0, w * 1.5, f);
}

void main() {
    vec3 nearPoint = unproject(vec3(v_ndc, -1.0));
    vec3 farPoint  = unproject(vec3(v_ndc,  1.0));
    vec3 rayDir = farPoint - nearPoint;

    float step = pc.u_flags.w;
    if (step <= 0.0) {
        float span = max(pc.u_viewX.z, max(pc.u_viewY.z, pc.u_viewZ.z));
        step = pow(10.0, floor(log(max(span, 1e-30)) / log(10.0)));
        if (step <= 0.0) step = 1.0;
    }

    float alpha = 0.0;

    // Floor plane: y = yMin (X-Z grid)
    if (pc.u_flags.x > 0.5 && abs(rayDir.y) > 1e-6) {
        float t = (pc.u_viewY.x - nearPoint.y) / rayDir.y;
        if (t > 0.0) {
            vec3 p = nearPoint + t * rayDir;
            if (p.x >= pc.u_viewX.x && p.x <= pc.u_viewX.x + pc.u_viewX.z &&
                p.z >= pc.u_viewZ.x && p.z <= pc.u_viewZ.x + pc.u_viewZ.z) {
                float gx = gridLine(p.x, step);
                float gz = gridLine(p.z, step);
                alpha = max(alpha, max(gx, gz));
            }
        }
    }

    // Back wall: z = zMin (X-Y grid)
    if (pc.u_flags.y > 0.5 && abs(rayDir.z) > 1e-6) {
        float t = (pc.u_viewZ.x - nearPoint.z) / rayDir.z;
        if (t > 0.0) {
            vec3 p = nearPoint + t * rayDir;
            if (p.x >= pc.u_viewX.x && p.x <= pc.u_viewX.x + pc.u_viewX.z &&
                p.y >= pc.u_viewY.x && p.y <= pc.u_viewY.x + pc.u_viewY.z) {
                float gx = gridLine(p.x, step);
                float gy = gridLine(p.y, step);
                alpha = max(alpha, max(gx, gy));
            }
        }
    }

    // Side wall: x = xMin (Y-Z grid)
    if (pc.u_flags.z > 0.5 && abs(rayDir.x) > 1e-6) {
        float t = (pc.u_viewX.x - nearPoint.x) / rayDir.x;
        if (t > 0.0) {
            vec3 p = nearPoint + t * rayDir;
            if (p.y >= pc.u_viewY.x && p.y <= pc.u_viewY.x + pc.u_viewY.z &&
                p.z >= pc.u_viewZ.x && p.z <= pc.u_viewZ.x + pc.u_viewZ.z) {
                float gy = gridLine(p.y, step);
                float gz = gridLine(p.z, step);
                alpha = max(alpha, max(gy, gz));
            }
        }
    }

    if (alpha < 0.01) discard;
    outColor = vec4(pc.u_gridColor.rgb, pc.u_gridColor.a * alpha);
}
)";

// Compute 4x4 matrix inverse (row-major) via Gauss-Jordan elimination.
std::array<float, 16> mat4Inverse(const std::array<float, 16>& m) {
    std::array<float, 32> aug;  // [m | I]
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            aug[i * 8 + j] = m[i * 4 + j];
            aug[i * 8 + 4 + j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        float maxVal = std::abs(aug[col * 8 + col]);
        for (int row = col + 1; row < 4; ++row) {
            float val = std::abs(aug[row * 8 + col]);
            if (val > maxVal) { maxVal = val; pivot = row; }
        }
        if (maxVal < 1e-30f) return {};

        if (pivot != col) {
            for (int j = 0; j < 8; ++j)
                std::swap(aug[col * 8 + j], aug[pivot * 8 + j]);
        }

        float piv = aug[col * 8 + col];
        for (int j = 0; j < 8; ++j) aug[col * 8 + j] /= piv;

        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            float factor = aug[row * 8 + col];
            for (int j = 0; j < 8; ++j)
                aug[row * 8 + j] -= factor * aug[col * 8 + j];
        }
    }

    std::array<float, 16> inv{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            inv[i * 4 + j] = aug[i * 8 + 4 + j];
    return inv;
}

} // namespace

void Grid3DRenderer::init(vk::Device device, vk::RenderPass renderPass,
                          vk::SampleCountFlagBits samples, core::PipelineCache& cache,
                          VmaAllocator allocator, vk::Queue queue, vk::CommandPool pool) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    static const float verts[] = { -1,-1, 3,-1, -1,3 };
    core::BufferDesc bdesc{};
    bdesc.size = sizeof(verts);
    bdesc.usage = core::BufferUsage::Vertex;
    fullscreenBuffer_ = core::Buffer(allocator, bdesc);
    fullscreenBuffer_.upload(device, queue, pool,
                              std::as_bytes(std::span{verts, 3}));

    // 11 vec4 push constants = 44 floats = 176 bytes
    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 44);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bind{0, sizeof(float)*2, vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attr{0, 0, vk::Format::eR32G32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingDescriptions(bind).setVertexAttributeDescriptions(attr);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill).setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(samples);

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att;
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOne)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci;
    cbsci.setAttachments(att);

    std::vector<vk::DynamicState> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci;
    dsci.setDynamicStates(dyn);

    vk::GraphicsPipelineCreateInfo gpci;
    gpci.setStages(stages).setPVertexInputState(&visci).setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci).setPRasterizationState(&rsci).setPMultisampleState(&msci)
        .setPDepthStencilState(&depthState)
        .setPColorBlendState(&cbsci).setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get()).setRenderPass(renderPass).setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Grid3D pipeline failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void Grid3DRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                          const plot::Viewport& viewport,
                          const plot::Camera3D& camera,
                          const Grid3DStyle& style) const {
    if (!inited_) return;

    auto vp = camera.viewProjection();
    auto invVP = mat4Inverse(vp);

    // Push constant layout: 11 vec4 = 44 floats
    struct PC {
        float rectX, rectY, rectW, rectH;           // u_rect
        float xMin, xMin2, xSpan, _pad0;            // u_viewX
        float yMin, yMin2, ySpan, _pad1;            // u_viewY
        float zMin, zMin2, zSpan, _pad2;            // u_viewZ
        float gridR, gridG, gridB, gridA;           // u_gridColor
        float flagsX, flagsY, flagsZ, step;         // u_flags
        float invVP0[4];                            // u_invVP0
        float invVP1[4];                            // u_invVP1
        float invVP2[4];                            // u_invVP2
        float invVP3[4];                            // u_invVP3
        float eyeX, eyeY, eyeZ, _pad3;              // u_eye
    } pc{};

    pc.rectX = static_cast<float>(rect.offset.x);
    pc.rectY = static_cast<float>(rect.offset.y);
    pc.rectW = static_cast<float>(rect.extent.width);
    pc.rectH = static_cast<float>(rect.extent.height);

    pc.xMin = viewport.x.min;  pc.xMin2 = viewport.x.min;  pc.xSpan = viewport.x.span();
    pc.yMin = viewport.y.min;  pc.yMin2 = viewport.y.min;  pc.ySpan = viewport.y.span();
    pc.zMin = viewport.z.min;  pc.zMin2 = viewport.z.min;  pc.zSpan = viewport.z.span();

    pc.gridR = style.color.r;
    pc.gridG = style.color.g;
    pc.gridB = style.color.b;
    pc.gridA = style.color.a;

    pc.flagsX = style.floorXZ ? 1.0f : 0.0f;
    pc.flagsY = style.backWallXY ? 1.0f : 0.0f;
    pc.flagsZ = style.sideWallYZ ? 1.0f : 0.0f;
    pc.step = style.step;

    for (int i = 0; i < 4; ++i) {
        pc.invVP0[i] = invVP[i * 4 + 0];
        pc.invVP1[i] = invVP[i * 4 + 1];
        pc.invVP2[i] = invVP[i * 4 + 2];
        pc.invVP3[i] = invVP[i * 4 + 3];
    }

    pc.eyeX = camera.eye.x;
    pc.eyeY = camera.eye.y;
    pc.eyeZ = camera.eye.z;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.pushConstants(pipelineLayout_.get(),
                      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PC), &pc);
    vk::Viewport vp_dyn;
    vp_dyn.setX(static_cast<float>(rect.offset.x))
          .setY(static_cast<float>(rect.offset.y))
          .setWidth(static_cast<float>(rect.extent.width))
          .setHeight(static_cast<float>(rect.extent.height))
          .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp_dyn);
    cmd.setScissor(0, rect);

    std::array<vk::Buffer, 1> buf = { fullscreenBuffer_.handle() };
    std::array<vk::DeviceSize, 1> off = {0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(3, 1, 0, 0);
}

} // namespace volcano::render
