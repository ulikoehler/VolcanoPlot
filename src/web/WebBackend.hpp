// src/web/WebBackend.hpp — IBackend for the WebGPU/WASM build
//
// No swapchain, no frames-in-flight: beginFrame resets the session
// OpStream and hands out an OpCmd; endFrame finishes the stream — the
// embind layer then exposes (ptr,len) to JS.
#pragma once

#include "OpGpuServices.hpp"
#include "OpCmd.hpp"
#include <volcano/backend/IBackend.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace volcano::web {

class WebBackend final : public backend::IBackend {
public:
    explicit WebBackend(uint32_t width = 0, uint32_t height = 0) {
        extent_ = {width, height};
        svc_.setExtent(extent_);
    }

    // ── IBackend ─────────────────────────────────────────────────────
    bool pollEvents() override { return true; }

    std::unique_ptr<render::Cmd> beginFrame() override {
        svc_.resetFrame();
        ++frameSeq_;
        loadFrame_ = false;
        return std::make_unique<OpCmd>(svc_.stream());
    }
    std::unique_ptr<render::Cmd> beginFrameLoad() override {
        svc_.resetFrame();
        ++frameSeq_;
        loadFrame_ = true;
        return std::make_unique<OpCmd>(svc_.stream());
    }
    void endFrame() override {
        lastFrame_ = svc_.stream().finish(
            frameSeq_, extent_.width, extent_.height, loadFrame_,
            uint32_t(clearColor_[0] * 255)
                | (uint32_t(clearColor_[1] * 255) << 8)
                | (uint32_t(clearColor_[2] * 255) << 16)
                | (uint32_t(clearColor_[3] * 255) << 24));
    }

    /// Reset the session stream — call at the start of each render
    /// cycle (bindings::_vp_render*), before prepare-phase uploads.
    void resetStream() { svc_.stream().reset(); }

    [[nodiscard]] render::GpuServices& gpu() override { return svc_; }
    [[nodiscard]] plot::Extent2D extent() const noexcept override {
        return extent_;
    }

    void resize(uint32_t width, uint32_t height) override {
        extent_ = {width, height};
        svc_.setExtent(extent_);
    }

    /// Events queued from JS via embind.
    void pushEvent(backend::InputEvent e) { events_.push_back(e); }
    std::vector<backend::InputEvent> takeEvents() override {
        std::vector<backend::InputEvent> out;
        events_.swap(out);
        return out;
    }

    // ── web-specific ─────────────────────────────────────────────────
    /// Last finished frame region — pointer into WASM memory, valid
    /// until the next endFrame().
    [[nodiscard]] std::pair<const uint8_t*, size_t> lastFrame() const {
        return lastFrame_;
    }
    OpGpuServices& opGpu() { return svc_; }

private:
    plot::Extent2D extent_{};
    OpGpuServices svc_;
    uint64_t frameSeq_ = 0;
    bool loadFrame_ = false;
    std::pair<const uint8_t*, size_t> lastFrame_{nullptr, 0};
    std::vector<backend::InputEvent> events_;
};

} // namespace volcano::web
