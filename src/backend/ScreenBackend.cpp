// volcano/backend/ScreenBackend.cpp
#include "volcano/backend/ScreenBackend.hpp"

#include <volcano/core/CommandBuffer.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <stdexcept>

namespace volcano::backend {

namespace {

vk::SurfaceFormatKHR pickFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Unorm &&
            f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) return f;
    }
    return formats.front();
}

vk::PresentModeKHR pickPresentMode(const std::vector<vk::PresentModeKHR>& modes) {
    for (auto m : modes) if (m == vk::PresentModeKHR::eMailbox) return m;
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D pickExtent(const vk::SurfaceCapabilitiesKHR& caps, GLFWwindow* win) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    vk::Extent2D extent;
    extent.width = std::clamp<uint32_t>(w, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp<uint32_t>(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

} // namespace

ScreenBackend::ScreenBackend(const BackendDesc& desc) : desc_(desc) {
    glfwSetErrorCallback([](int code, const char* msg) {
        std::cerr << std::format("GLFW error {}: {}\n", code, msg);
    });
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }
    // Vulkan window — no GL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(int(desc.width), int(desc.height),
                               desc.windowTitle.c_str(), nullptr, nullptr);
    if (!window_) throw std::runtime_error("glfwCreateWindow failed");

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &ScreenBackend::cbFramebufferSize);
    glfwSetMouseButtonCallback(window_, &ScreenBackend::cbMouseButton);
    glfwSetCursorPosCallback(window_, &ScreenBackend::cbCursorPos);
    glfwSetScrollCallback(window_, &ScreenBackend::cbScroll);
    glfwSetKeyCallback(window_, &ScreenBackend::cbKey);
    glfwSetCharCallback(window_, &ScreenBackend::cbChar);
    glfwSetWindowCloseCallback(window_, &ScreenBackend::cbClose);

    // Instance
    core::InstanceDesc idesc{};
    idesc.applicationName = desc.windowTitle;
    idesc.enableValidation = desc.enableValidation;
    // GLFW reports the platform surface extensions it needs; dedup
    // against the hardcoded fallbacks below.
    {
        std::vector<std::string> exts;
        auto add = [&exts](const char* e) {
            if (std::ranges::find(exts, e) == exts.end())
                exts.emplace_back(e);
        };
        uint32_t n = 0;
        if (const char** req = glfwGetRequiredInstanceExtensions(&n))
            for (uint32_t i = 0; i < n; ++i) add(req[i]);
        add(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__linux__)
        // Fallback surface extensions in case GLFW's hint list is empty.
        add("VK_KHR_xlib_surface");
        add("VK_KHR_xcb_surface");
        add("VK_KHR_wayland_surface");
#endif
        idesc.extraExtensions = std::move(exts);
    }
    ctx_.instance = core::Instance(idesc);

    // Surface
    VkSurfaceKHR surf{};
    if (glfwCreateWindowSurface(ctx_.instance.handle(), window_,
                                nullptr, &surf) != VK_SUCCESS) {
        throw std::runtime_error("glfwCreateWindowSurface failed");
    }
    surface_ = surf;

    // Physical device + logical device
    ctx_.physical = core::PhysicalDevice(ctx_.instance.handle(), surface_);
    core::DeviceDesc ddesc{};
    ddesc.extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    ddesc.features.features.wideLines = VK_TRUE;
    ctx_.device = core::Device(ctx_.physical, ddesc);

    // Allocator
    ctx_.allocator = core::Allocator(ctx_.instance.handle(), ctx_.physical.handle(), ctx_.device.handle());

    // Command pools
    ctx_.graphicsPool = core::CommandPool(ctx_.device.handle(), ctx_.device.graphicsFamily(),
                                          vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    ctx_.computePool = core::CommandPool(ctx_.device.handle(), ctx_.device.computeFamily(),
                                         vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    // Determine MSAA sample count supported by the device.
    auto props = ctx_.physical.properties();
    auto maxSamples = props.limits.framebufferColorSampleCounts;
    samples_ = desc.samples;
    if (!(maxSamples & samples_)) {
        // Fall back to highest supported <= requested.
        for (auto s : {vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8,
                       vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2,
                       vk::SampleCountFlagBits::e1}) {
            if (maxSamples & s) { samples_ = s; break; }
        }
    }

    createSwapchain();
    createRenderPass();
    createFramebuffers();

    // Sync objects + command buffers
    vk::CommandBufferAllocateInfo ai{};
    ai.setCommandPool(ctx_.graphicsPool.handle())
       .setLevel(vk::CommandBufferLevel::ePrimary)
       .setCommandBufferCount(MAX_FRAMES_IN_FLIGHT);
    auto cbs = ctx_.device.handle().allocateCommandBuffersUnique(ai);
    commandBuffers_ = std::move(cbs);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        imageAvailableSem_.push_back(ctx_.device.handle().createSemaphoreUnique({}));
        renderFinishedSem_.push_back(ctx_.device.handle().createSemaphoreUnique({}));
        inFlightFences_.push_back(ctx_.device.handle().createFenceUnique(
            vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled)));
    }
}

ScreenBackend::~ScreenBackend() {
    ctx_.device.waitIdle();
    if (surface_) ctx_.instance.handle().destroySurfaceKHR(surface_);
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void ScreenBackend::createSurface() {
    // Created in ctor; kept for clarity.
}

void ScreenBackend::createSwapchain() {
    auto phys = ctx_.physical.handle();
    auto caps = phys.getSurfaceCapabilitiesKHR(surface_);
    auto formats = phys.getSurfaceFormatsKHR(surface_);
    auto modes = phys.getSurfacePresentModesKHR(surface_);

    auto fmt = pickFormat(formats);
    colorFormat_ = fmt.format;
    extent_ = pickExtent(caps, window_);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    vk::SwapchainCreateInfoKHR ci{};
    ci.setSurface(surface_)
       .setMinImageCount(imageCount)
       .setImageFormat(fmt.format)
       .setImageColorSpace(fmt.colorSpace)
       .setImageExtent(extent_)
       .setImageArrayLayers(1)
       .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)
       .setImageSharingMode(vk::SharingMode::eExclusive)
       .setPreTransform(caps.currentTransform)
       .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
       .setPresentMode(pickPresentMode(modes))
       .setClipped(true);

    swapchain_ = ctx_.device.handle().createSwapchainKHRUnique(ci);
    swapchainImages_ = ctx_.device.handle().getSwapchainImagesKHR(swapchain_.get());

    swapchainViews_.clear();
    for (auto img : swapchainImages_) {
        vk::ImageViewCreateInfo vci{};
        vci.setImage(img)
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(colorFormat_)
           .setComponents({})
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        swapchainViews_.push_back(ctx_.device.handle().createImageViewUnique(vci));
    }

    // MSAA color target
    if (samples_ != vk::SampleCountFlagBits::e1) {
        core::ImageDesc idesc{};
        idesc.format = colorFormat_;
        idesc.extent = extent_;
        idesc.samples = samples_;
        idesc.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment;
        msaaColor_ = core::Image(ctx_.allocator.handle(), idesc);
        vk::ImageViewCreateInfo vci{};
        vci.setImage(msaaColor_.handle())
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(colorFormat_)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        msaaView_ = ctx_.device.handle().createImageViewUnique(vci);
    }
}

void ScreenBackend::createRenderPass() {
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;
    depthFormat_ = findDepthFormat(ctx_.physical.handle());

    vk::AttachmentDescription colorAtt{};
    colorAtt.setFormat(colorFormat_)
        .setSamples(samples_)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(msaa ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentDescription resolveAtt{};
    resolveAtt.setFormat(colorFormat_)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentDescription depthAtt{};
    depthAtt.setFormat(depthFormat_)
        .setSamples(samples_)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentReference colorRef{};
    colorRef.setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference resolveRef{};
    resolveRef.setAttachment(1).setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    uint32_t depthIdx = msaa ? 2 : 1;
    vk::AttachmentReference depthRef{};
    depthRef.setAttachment(depthIdx).setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::SubpassDescription sub{};
    sub.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
       .setColorAttachments(colorRef)
       .setPDepthStencilAttachment(&depthRef);
    if (msaa) sub.setResolveAttachments(resolveRef);

    vk::SubpassDependency dep{};
    dep.setSrcSubpass(VK_SUBPASS_EXTERNAL)
       .setDstSubpass(0)
       .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                      | vk::PipelineStageFlagBits::eEarlyFragmentTests)
       .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                      | vk::PipelineStageFlagBits::eEarlyFragmentTests)
       .setSrcAccessMask(vk::AccessFlagBits::eNone)
       .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite
                       | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    std::vector<vk::AttachmentDescription> atts = { colorAtt };
    if (msaa) atts.push_back(resolveAtt);
    atts.push_back(depthAtt);

    vk::RenderPassCreateInfo ci{};
    ci.setAttachments(atts).setSubpasses(sub).setDependencies(dep);
    renderPass_ = ctx_.device.handle().createRenderPassUnique(ci);
}

void ScreenBackend::createFramebuffers() {
    framebuffers_.clear();
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;

    // Depth image (recreated with swapchain).
    if (depthFormat_ != vk::Format::eUndefined) {
        core::ImageDesc ddesc{};
        ddesc.format = depthFormat_;
        ddesc.extent = extent_;
        ddesc.samples = samples_;
        ddesc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        depthImage_ = core::Image(ctx_.allocator.handle(), ddesc);
        vk::ImageViewCreateInfo dvi{};
        dvi.setImage(depthImage_.handle())
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(depthFormat_)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eDepth)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        depthView_ = ctx_.device.handle().createImageViewUnique(dvi);
    }

    for (auto& view : swapchainViews_) {
        std::vector<vk::ImageView> attachments;
        if (msaa) {
            attachments = { msaaView_.get(), view.get() };
        } else {
            attachments = { view.get() };
        }
        if (depthView_) attachments.push_back(depthView_.get());
        vk::FramebufferCreateInfo ci{};
        ci.setRenderPass(renderPass_.get())
           .setAttachments(attachments)
           .setWidth(extent_.width)
           .setHeight(extent_.height)
           .setLayers(1);
        framebuffers_.push_back(ctx_.device.handle().createFramebufferUnique(ci));
    }
}

void ScreenBackend::recreateSwapchain() {
    ctx_.device.waitIdle();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    resized_ = false;
}

namespace {

/// GLFW button index → mpl convention (1=left, 2=middle, 3=right,
/// 8=back, 9=forward — matching mpl's MouseButton enum).
int toMplButton(int b) {
    switch (b) {
        case GLFW_MOUSE_BUTTON_LEFT: return 1;
        case GLFW_MOUSE_BUTTON_MIDDLE: return 2;
        case GLFW_MOUSE_BUTTON_RIGHT: return 3;
        case GLFW_MOUSE_BUTTON_4: return 8;  // mpl "back"
        case GLFW_MOUSE_BUTTON_5: return 9;  // mpl "forward"
        default: return b + 1;
    }
}

/// GLFW keycode → printable ASCII. GLFW keycodes for letters are the
/// uppercase ASCII codes; digits are the ASCII digits.
char toAscii(int kc, bool shift) {
    if (kc >= GLFW_KEY_A && kc <= GLFW_KEY_Z)
        return char('a' + (kc - GLFW_KEY_A));
    if (kc >= GLFW_KEY_0 && kc <= GLFW_KEY_9) {
        static constexpr char shifted[] = ")!@#$%^&*(";
        return shift ? shifted[kc - GLFW_KEY_0] : char(kc);
    }
    switch (kc) {
        case GLFW_KEY_SPACE: return ' ';
        case GLFW_KEY_MINUS: return shift ? '_' : '-';
        case GLFW_KEY_EQUAL: return shift ? '+' : '=';
        case GLFW_KEY_LEFT_BRACKET: return shift ? '{' : '[';
        case GLFW_KEY_RIGHT_BRACKET: return shift ? '}' : ']';
        case GLFW_KEY_SEMICOLON: return shift ? ':' : ';';
        case GLFW_KEY_APOSTROPHE: return shift ? '"' : '\'';
        case GLFW_KEY_COMMA: return shift ? '<' : ',';
        case GLFW_KEY_PERIOD: return shift ? '>' : '.';
        case GLFW_KEY_SLASH: return shift ? '?' : '/';
        case GLFW_KEY_BACKSLASH: return shift ? '|' : '\\';
        case GLFW_KEY_GRAVE_ACCENT: return shift ? '~' : '`';
        default: return 0;
    }
}

/// GLFW keycode → mpl key name for non-printable keys.
const char* keyName(int kc) {
    switch (kc) {
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return "enter";
        case GLFW_KEY_ESCAPE: return "escape";
        case GLFW_KEY_BACKSPACE: return "backspace";
        case GLFW_KEY_TAB: return "tab";
        case GLFW_KEY_DELETE: return "delete";
        case GLFW_KEY_LEFT: return "left";
        case GLFW_KEY_RIGHT: return "right";
        case GLFW_KEY_UP: return "up";
        case GLFW_KEY_DOWN: return "down";
        case GLFW_KEY_HOME: return "home";
        case GLFW_KEY_END: return "end";
        case GLFW_KEY_PAGE_UP: return "pageup";
        case GLFW_KEY_PAGE_DOWN: return "pagedown";
        case GLFW_KEY_LEFT_SHIFT: case GLFW_KEY_RIGHT_SHIFT: return "shift";
        case GLFW_KEY_LEFT_CONTROL: case GLFW_KEY_RIGHT_CONTROL: return "control";
        case GLFW_KEY_LEFT_ALT: case GLFW_KEY_RIGHT_ALT: return "alt";
        default: return nullptr;
    }
}

/// Unicode codepoint → UTF-8 (GLFW char callback gives codepoints).
std::string utf8(unsigned int cp) {
    std::string s;
    if (cp < 0x80) {
        s += char(cp);
    } else if (cp < 0x800) {
        s += char(0xC0 | (cp >> 6));
        s += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    } else {
        s += char(0xF0 | (cp >> 18));
        s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
    return s;
}

} // namespace

int ScreenBackend::buttonMask() const {
    int out = 0;
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) out |= 1;
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) out |= 2;
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) out |= 4;
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_4) == GLFW_PRESS) out |= 8;
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_5) == GLFW_PRESS) out |= 16;
    return out;
}

void ScreenBackend::fillMods(InputEvent& ev, int mods) {
    ev.shift = (mods & GLFW_MOD_SHIFT) != 0;
    ev.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    ev.alt = (mods & GLFW_MOD_ALT) != 0;
}

void ScreenBackend::onFramebufferSize(int w, int h) {
    resized_ = true;
    InputEvent ev{};
    ev.type = InputEvent::Type::Resize;
    ev.width = uint32_t(w);
    ev.height = uint32_t(h);
    pendingEvents_.push_back(ev);
}

void ScreenBackend::onMouseButton(int button, int action, int mods) {
    double cx, cy;
    glfwGetCursorPos(window_, &cx, &cy);
    InputEvent ev{};
    ev.type = action == GLFW_PRESS ? InputEvent::Type::ButtonPress
                                   : InputEvent::Type::ButtonRelease;
    ev.x = float(cx);
    ev.y = float(cy);
    ev.button = toMplButton(button);
    ev.buttons = buttonMask();
    // GLFW reports no click counts — detect double clicks by timing.
    if (action == GLFW_PRESS) {
        double t = glfwGetTime();
        if (button == lastClickButton_ &&
            t - lastClickTime_ < 0.3 &&
            std::abs(float(cx) - lastClickX_) < 5.0f &&
            std::abs(float(cy) - lastClickY_) < 5.0f)
            ev.dblclick = true;
        lastClickTime_ = t;
        lastClickButton_ = button;
        lastClickX_ = float(cx);
        lastClickY_ = float(cy);
    }
    fillMods(ev, mods);
    pendingEvents_.push_back(ev);
}

void ScreenBackend::onCursorPos(double x, double y) {
    InputEvent ev{};
    ev.type = InputEvent::Type::Motion;
    ev.x = float(x);
    ev.y = float(y);
    ev.buttons = buttonMask();
    int mods = 0;
    if (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        mods |= GLFW_MOD_SHIFT;
    if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        mods |= GLFW_MOD_CONTROL;
    if (glfwGetKey(window_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
        mods |= GLFW_MOD_ALT;
    fillMods(ev, mods);
    pendingEvents_.push_back(ev);
}

void ScreenBackend::onScroll(double /*xoff*/, double yoff) {
    double cx, cy;
    glfwGetCursorPos(window_, &cx, &cy);
    InputEvent ev{};
    ev.type = InputEvent::Type::Scroll;
    ev.step = float(yoff);
    ev.x = float(cx);
    ev.y = float(cy);
    ev.buttons = buttonMask();
    int mods = 0;
    if (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        mods |= GLFW_MOD_SHIFT;
    if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        mods |= GLFW_MOD_CONTROL;
    if (glfwGetKey(window_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
        mods |= GLFW_MOD_ALT;
    fillMods(ev, mods);
    pendingEvents_.push_back(ev);
}

void ScreenBackend::onKey(int key, int /*scancode*/, int action,
                          int mods) {
    InputEvent ev{};
    ev.type = action == GLFW_RELEASE ? InputEvent::Type::KeyRelease
                                     : InputEvent::Type::KeyPress;
    fillMods(ev, mods);
    ev.keycode = uint32_t(key);
    ev.key = toAscii(key, ev.shift);
    if (ev.key == 0)
        if (const char* n = keyName(key)) ev.text = n;
    double cx, cy;
    glfwGetCursorPos(window_, &cx, &cy);
    ev.x = float(cx);
    ev.y = float(cy);
    ev.buttons = buttonMask();
    pendingEvents_.push_back(ev);
}

void ScreenBackend::onChar(unsigned int codepoint) {
    InputEvent ev{};
    ev.type = InputEvent::Type::TextInput;
    ev.text = utf8(codepoint);
    pendingEvents_.push_back(ev);
}

void ScreenBackend::cbFramebufferSize(GLFWwindow* w, int width,
                                      int height) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->onFramebufferSize(width, height);
}
void ScreenBackend::cbMouseButton(GLFWwindow* w, int button, int action,
                                  int mods) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->onMouseButton(button, action, mods);
}
void ScreenBackend::cbCursorPos(GLFWwindow* w, double x, double y) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->onCursorPos(x, y);
}
void ScreenBackend::cbScroll(GLFWwindow* w, double xoff, double yoff) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->onScroll(xoff, yoff);
}
void ScreenBackend::cbKey(GLFWwindow* w, int key, int scancode,
                          int action, int mods) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->onKey(key, scancode, action, mods);
}
void ScreenBackend::cbChar(GLFWwindow* w, unsigned int cp) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->onChar(cp);
}
void ScreenBackend::cbClose(GLFWwindow* w) {
    static_cast<ScreenBackend*>(glfwGetWindowUserPointer(w))
        ->closeRequested_ = true;
}

bool ScreenBackend::pollEvents() {
    glfwPollEvents();
    return !closeRequested_ && !glfwWindowShouldClose(window_);
}

std::vector<InputEvent> ScreenBackend::takeEvents() {
    auto out = std::move(pendingEvents_);
    pendingEvents_.clear();
    return out;
}

void ScreenBackend::setWindowTitle(std::string_view title) {
    glfwSetWindowTitle(window_, std::string(title).c_str());
}

void ScreenBackend::toggleFullscreen() {
    if (!fullscreen_) {
        glfwGetWindowPos(window_, &savedX_, &savedY_);
        glfwGetWindowSize(window_, &savedW_, &savedH_);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(window_, mon, 0, 0, mode->width,
                             mode->height, mode->refreshRate);
        fullscreen_ = true;
    } else {
        glfwSetWindowMonitor(window_, nullptr, savedX_, savedY_,
                             savedW_, savedH_, GLFW_DONT_CARE);
        fullscreen_ = false;
    }
}

vk::CommandBuffer ScreenBackend::beginFrame() {
    if (resized_) recreateSwapchain();

    auto dev = ctx_.device.handle();
    dev.waitForFences(inFlightFences_[currentFrame_].get(), true, UINT64_MAX);
    auto result = dev.acquireNextImageKHR(swapchain_.get(), UINT64_MAX,
                                          imageAvailableSem_[currentFrame_].get(),
                                          VK_NULL_HANDLE, &imageIndex_);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return beginFrame();
    }
    dev.resetFences(inFlightFences_[currentFrame_].get());

    auto cb = commandBuffers_[currentFrame_].get();
    cb.reset();
    vk::CommandBufferBeginInfo bi{};
    bi.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cb.begin(bi);

    // MSAA adds a resolve attachment at index 1, pushing depth to 2.
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;
    std::array<vk::ClearValue, 3> clears{};
    clears[0].color.setFloat32({clearColor_[0], clearColor_[1],
                                clearColor_[2], clearColor_[3]});
    clears[msaa ? 2 : 1].depthStencil.setDepth(1.0f).setStencil(0);
    vk::RenderPassBeginInfo rpi{};
    rpi.setRenderPass(renderPass_.get())
       .setFramebuffer(framebuffers_[imageIndex_].get())
       .setRenderArea(vk::Rect2D{}.setOffset({0,0}).setExtent(extent_))
       .setClearValues(clears);
    cb.beginRenderPass(rpi, vk::SubpassContents::eInline);
    return cb;
}

void ScreenBackend::endFrame() {
    auto cb = commandBuffers_[currentFrame_].get();
    cb.endRenderPass();
    cb.end();

    vk::SubmitInfo si{};
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    si.setWaitSemaphores(imageAvailableSem_[currentFrame_].get())
       .setWaitDstStageMask(waitStage)
       .setCommandBuffers(cb)
       .setSignalSemaphores(renderFinishedSem_[currentFrame_].get());
    ctx_.device.graphicsQueue().submit(si, inFlightFences_[currentFrame_].get());

    vk::PresentInfoKHR pi{};
    pi.setWaitSemaphores(renderFinishedSem_[currentFrame_].get())
       .setSwapchains(swapchain_.get())
       .setImageIndices(imageIndex_);
    auto result = ctx_.device.graphicsQueue().presentKHR(pi);
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || resized_) {
        recreateSwapchain();
    }
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace volcano::backend
