// volcano/core/CommandPool.hpp — command pool ownership
#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>

namespace volcano::core {

class Device;

/// Owns a vk::CommandPool; share() produces a handle kept alive by
/// shared ownership — the last CommandPool destroys the pool.
class CommandPool {
public:
    CommandPool() = default;
    CommandPool(vk::Device device, uint32_t queueFamily,
                vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    ~CommandPool() = default;

    CommandPool(CommandPool&&) noexcept = default;
    CommandPool& operator=(CommandPool&&) noexcept = default;
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    /// A second CommandPool sharing this one's Vulkan pool; the
    /// underlying object dies with the last holder.
    [[nodiscard]] CommandPool share() const {
        CommandPool s;
        s.pool_ = pool_;
        return s;
    }

    [[nodiscard]] vk::CommandPool handle() const noexcept {
        return pool_ ? *pool_ : vk::CommandPool{};
    }

private:
    std::shared_ptr<vk::CommandPool> pool_;
};

} // namespace volcano::core
