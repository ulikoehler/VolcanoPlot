// volcano/core/DescriptorPool.hpp — descriptor pool
#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

namespace volcano::core {

class DescriptorPool {
public:
    DescriptorPool() = default;
    DescriptorPool(vk::Device device, const std::vector<vk::DescriptorPoolSize>& sizes,
                   uint32_t maxSets, vk::DescriptorPoolCreateFlags flags = {});
    ~DescriptorPool();

    DescriptorPool(DescriptorPool&&) noexcept = default;
    DescriptorPool& operator=(DescriptorPool&&) noexcept = default;
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    [[nodiscard]] vk::DescriptorPool handle() const noexcept { return pool_.get(); }

    vk::DescriptorSet allocate(vk::DescriptorSetLayout layout);

private:
    vk::UniqueDescriptorPool pool_;
    vk::Device device_;
};

} // namespace volcano::core
