// volcano/render/VkCmd.hpp — Vulkan Cmd wrapper
//
// Native-build-only: wraps a begun vk::CommandBuffer in the
// backend-neutral Cmd interface. Primitive impls recover the handle via
// `vkCmd(cmd)`. Included by Vulkan code only — never by volcano_plot or
// the WASM build.
#pragma once

#include <volcano/render/Cmd.hpp>

#include <vulkan/vulkan.hpp>

namespace volcano::render {

class VkCmd : public Cmd {
public:
    explicit VkCmd(vk::CommandBuffer h) : h_(h) {}
    vk::CommandBuffer h_ = {};
};

/// Recover the Vulkan command buffer from a Cmd (Vulkan backends only).
[[nodiscard]] inline vk::CommandBuffer vkCmd(Cmd& cmd) noexcept {
    return static_cast<VkCmd&>(cmd).h_;
}
[[nodiscard]] inline vk::CommandBuffer vkCmd(Cmd* cmd) noexcept {
    return static_cast<VkCmd*>(cmd)->h_;
}

} // namespace volcano::render
