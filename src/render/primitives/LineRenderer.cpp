// volcano/render/primitives/LineRenderer.cpp
#include "volcano/render/primitives/LineRenderer.hpp"
#include "../VkFactory.hpp"
#include "../VulkanGpuServices.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include "../shaders/TransformGlsl.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <volcano/core/ShaderModule.hpp>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertHead = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;
    vec4 u_rect;
    vec4 u_color;
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
    float u_width;
} pc;
layout(location = 0) out vec4 v_color;
)";

constexpr const char* kVertMain = R"(
void main() {
    vec2 p = projFwd(vec2(scaleFwd(a_pos.x, pc.u_scaleX),
                          scaleFwd(a_pos.y, pc.u_scaleY)),
                     pc.u_proj.xyz);
    // Map data coords to NDC [-1,1] — viewport handles pixel mapping.
    vec2 ndc = (p - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = pc.u_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 outColor;
void main() { outColor = v_color; }
)";

} // namespace

[[nodiscard]] inline vk::Rect2D vkScissor(plot::Rect2D r) noexcept {
    return {vk::Offset2D{r.x, r.y}, vk::Extent2D{r.width, r.height}};
}

class LineRendererVk final : public LineRenderer {
public:
    explicit LineRendererVk(VulkanGpuServices& svcs) : svcs_(&svcs) {}

    void init();
    void upload(std::span<const plot::Point2D> points,
                plot::Color color, float width) override;
    void draw(Cmd& cmd, plot::Rect2D rect,
              const plot::Transform2D& transform,
              uint32_t pointCount) const override;
    [[nodiscard]] GpuBuf pointBuffer() const noexcept override {
        return externalBuf_ ? externalBuf_ : GpuBuf(VkBuffer(pointBuffer_.handle()));
    }
    [[nodiscard]] uint32_t pointCount() const noexcept override { return count_; }
    void updatePoints(std::span<const plot::Point2D> points) override;
    bool updatePointsF64(std::span<const double> xy) override;
    bool updatePointsF64Append(uint32_t drop,
                               std::span<const double> tail) override;
    void f64Ring(uint32_t& head, uint32_t& cap) const noexcept override {
        head = f64Head_; cap = capacity64_ ? capacity64_ : count_;
    }
    [[nodiscard]] GpuBuf pointBufferF64() const noexcept override {
        return GpuBuf(VkBuffer(pointBufferF64_.handle()));
    }
    [[nodiscard]] bool f64Pending() const noexcept override {
        return f64Pending_;
    }
    void clearF64Pending() noexcept override { f64Pending_ = false; }
    void setStyle(plot::Color color, float width) override {
        color_ = color; width_ = width;
    }
    void bindExternalBuffer(GpuBuf buf, uint32_t count) override {
        externalBuf_ = buf;
        count_ = count;
    }

private:
    VulkanGpuServices* svcs_;
    vk::Device device_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer pointBuffer_;
    core::Buffer pointBufferF64_;   ///< raw dvec2 upload (pre-narrow)
    GpuBuf externalBuf_ = 0;
    VmaAllocator allocator_ = nullptr;
    plot::Color color_;
    float width_ = 1.0f;
    uint32_t count_ = 0;
    uint32_t capacity_ = 0;      ///< allocated point capacity
    uint32_t capacity64_ = 0;    ///< allocated f64 point capacity
    uint32_t f64Head_ = 0;       ///< ring start of the logical window
    bool f64Pending_ = false;    ///< f64 data awaits GPU narrowing
    bool inited_ = false;
};

void LineRendererVk::init() {
    const vk::Device device = svcs_->device();
    const auto renderPass = svcs_->renderPass();
    const auto samples = svcs_->samples();
    auto& cache = svcs_->pipelineCache();
    device_ = device;
    auto vertSrc = std::string(kVertHead) + shaders::kScaleFn +
                   shaders::kProjFn + kVertMain;
    auto v = core::ShaderModule::compileGlsl(vertSrc, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 25);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bind{0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attr{0, 0, vk::Format::eR32G32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingDescriptions(bind).setVertexAttributeDescriptions(attr);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eLineStrip);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.lineWidth = 1.0f; rsci.polygonMode = vk::PolygonMode::eFill;
    rsci.cullMode = vk::CullModeFlagBits::eNone;

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(samples);

    // Depth testing disabled (2D overlay).
    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att;
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci;
    cbsci.setAttachments(att);

    std::vector<vk::DynamicState> dyn = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor,
        vk::DynamicState::eLineWidth,
    };
    vk::PipelineDynamicStateCreateInfo dsci;
    dsci.setDynamicStates(dyn);

    vk::GraphicsPipelineCreateInfo gpci;
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
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Line pipeline creation failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void LineRendererVk::upload(std::span<const plot::Point2D> points,
                          plot::Color color, float width) {
    const vk::Device device = svcs_->device();
    const vk::Queue queue = svcs_->graphicsQueue();
    const vk::CommandPool pool = svcs_->graphicsPool();
    const VmaAllocator allocator = svcs_->allocator();
    allocator_ = allocator;
    core::BufferDesc d;
    d.size = points.size_bytes();
    d.usage = core::BufferUsage::VertexStorage;
    d.hostVisible = true;   // dynamic data — in-place memcpy updates
    pointBuffer_ = core::Buffer(allocator, d);
    pointBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{points.data(), points.size()}));
    color_ = color;
    width_ = width;
    count_ = static_cast<uint32_t>(points.size());
    capacity_ = count_;
    externalBuf_ = 0;
}

void LineRendererVk::updatePoints(std::span<const plot::Point2D> points) {
    if (points.size() <= capacity_) {
        std::memcpy(pointBuffer_.mappedData(), points.data(),
                    points.size_bytes());
        count_ = static_cast<uint32_t>(points.size());
        return;
    }
    core::BufferDesc d;
    d.size = points.size_bytes();
    d.usage = core::BufferUsage::VertexStorage;
    d.hostVisible = true;
    pointBuffer_ = core::Buffer(allocator_, d);
    std::memcpy(pointBuffer_.mappedData(), points.data(),
                points.size_bytes());
    count_ = static_cast<uint32_t>(points.size());
    capacity_ = count_;
}

bool LineRendererVk::updatePointsF64(std::span<const double> xy) {
    const uint32_t n = static_cast<uint32_t>(xy.size() / 2);
    if (n == 0) {
        count_ = 0;
        f64Pending_ = false;
        return true;
    }
    if (!allocator_) allocator_ = svcs_->allocator();
    if (n > capacity64_) {
        core::BufferDesc d;
        d.size = vk::DeviceSize(n) * 16;
        d.usage = core::BufferUsage::Storage;
        d.hostVisible = true;   // in-place memcpy updates, GPU reads
        pointBufferF64_ = core::Buffer(allocator_, d);
        capacity64_ = n;
        f64Head_ = 0;
    }
    std::memcpy(pointBufferF64_.mappedData(), xy.data(),
                vk::DeviceSize(n) * 16);
    f64Head_ = 0;   // full upload resets the ring to linear layout
    // The f32 buffer is written by the GPU narrowing pass — grow it
    // here so convertF64ToF32 always has a destination.
    if (n > capacity_) {
        core::BufferDesc d;
        d.size = vk::DeviceSize(n) * 8;
        d.usage = core::BufferUsage::VertexStorage;
        d.hostVisible = true;
        pointBuffer_ = core::Buffer(allocator_, d);
        capacity_ = n;
    }
    count_ = n;
    f64Pending_ = true;
    return true;
}

bool LineRendererVk::updatePointsF64Append(uint32_t drop,
                                           std::span<const double> tail) {
    const uint32_t tailN = static_cast<uint32_t>(tail.size() / 2);
    const uint32_t kept = count_ > drop ? count_ - drop : 0;
    if (!tailN || !pointBufferF64_.handle() || !capacity64_ ||
        kept + tailN > capacity64_)
        return false;
    // Append at the slot right after the current logical tail —
    // (head + count) % cap — then move the head past the dropped
    // points. Two memcpy chunks when the tail wraps the buffer end.
    const uint32_t cap = capacity64_;
    const uint32_t writeHead = (f64Head_ + count_) % cap;
    auto* base = static_cast<char*>(pointBufferF64_.mappedData());
    const uint32_t first = std::min(tailN, cap - writeHead);
    std::memcpy(base + size_t(writeHead) * 16, tail.data(),
                size_t(first) * 16);
    if (first < tailN)
        std::memcpy(base, tail.data() + size_t(first) * 2,
                    size_t(tailN - first) * 16);
    f64Head_ = (f64Head_ + drop) % cap;
    count_ = kept + tailN;
    f64Pending_ = true;
    return true;
}

void LineRendererVk::draw(Cmd& cmdRef, plot::Rect2D rect,
                        const plot::Transform2D& transform, uint32_t pointCount) const {
    const auto cmd = vkCmd(cmdRef);
    if (!inited_ || pointCount < 2) return;
    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;  // u_viewMinSpan
        float rectX, rectY, rectW, rectH;                // u_rect
        float r, g, b, a;                                 // u_color
        float sxCode, sxP1, sxP2, sxPad;                  // u_scaleX
        float syCode, syP1, syP2, syPad;                  // u_scaleY
        float prCode, thetaOff, thetaDir, prPad;          // u_proj
        float width;                                      // u_width
    } pc{};
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.rectX = static_cast<float>(rect.x);
    pc.rectY = static_cast<float>(rect.y);
    pc.rectW = static_cast<float>(rect.width);
    pc.rectH = static_cast<float>(rect.height);
    pc.r = color_.r; pc.g = color_.g; pc.b = color_.b; pc.a = color_.a;
    pc.sxCode = static_cast<float>(static_cast<int>(transform.codeX()));
    pc.sxP1 = transform.scaleX.param1;
    pc.sxP2 = transform.scaleX.param2;
    pc.sxPad = transform.scaleX.param3;
    pc.syCode = static_cast<float>(static_cast<int>(transform.codeY()));
    pc.syP1 = transform.scaleY.param1;
    pc.syP2 = transform.scaleY.param2;
    pc.syPad = transform.scaleY.param3;
    pc.prCode = static_cast<float>(static_cast<int>(transform.projection.kind));
    pc.thetaOff = transform.projection.thetaOffset;
    pc.thetaDir = transform.projection.thetaDir;
    pc.width = width_;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.setLineWidth(std::max(width_, 1.0f));
    cmd.pushConstants(pipelineLayout_.get(),
                      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PC), &pc);
    vk::Viewport vp;
    vp.setX(static_cast<float>(rect.x))
       .setY(static_cast<float>(rect.y))
       .setWidth(static_cast<float>(rect.width))
       .setHeight(static_cast<float>(rect.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vkScissor(rect));

    std::array<vk::Buffer, 1> buf = { svcs_->vkBufferOf(
        externalBuf_ ? externalBuf_
                     : GpuBuf(VkBuffer(pointBuffer_.handle()))) };
    std::array<vk::DeviceSize, 1> off = {0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(pointCount, 1, 0, 0);
}

std::unique_ptr<LineRenderer> makeLineVk(VulkanGpuServices& svcs) {
    auto p = std::make_unique<LineRendererVk>(svcs);
    p->init();
    return p;
}

} // namespace volcano::render::primitives
