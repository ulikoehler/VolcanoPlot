// volcano/render/Cmd.hpp — backend-neutral per-frame command context
//
// IPlot::draw/preDraw and all primitive renderers take `Cmd&` instead of
// a GPU-API command buffer. The Vulkan build wraps a begun
// vk::CommandBuffer in VkCmd (see vk/VkCmd.hpp); the WebGPU/WASM build
// wraps an OpStream in web::OpCmd (see src/web/OpCmd.hpp). Primitive
// implementations downcast to their backend's type at method entry.
//
// GpuBuf/GpuTex are opaque resource tokens — the meaning is
// backend-defined: Vulkan impls pack the VkBuffer/VkImage handle, the web
// impls pack a u32 op-stream handle. Plot code must never inspect them
// beyond `!= 0`.
#pragma once

#include <cstdint>

namespace volcano::render {

class Cmd {
public:
    virtual ~Cmd() = default;
};

/// Opaque GPU buffer token (backend-defined).
using GpuBuf = uint64_t;
/// Opaque GPU texture token (backend-defined).
using GpuTex = uint64_t;

} // namespace volcano::render
