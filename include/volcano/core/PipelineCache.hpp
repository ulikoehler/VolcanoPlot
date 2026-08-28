// volcano/core/PipelineCache.hpp — pipeline cache wrapper
#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

namespace volcano::core {

class PipelineCache {
public:
    PipelineCache() = default;
    PipelineCache(vk::Device device, const std::filesystem::path& cacheFile = {});
    ~PipelineCache();

    PipelineCache(PipelineCache&&) noexcept = default;
    PipelineCache& operator=(PipelineCache&&) noexcept = default;
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    [[nodiscard]] vk::PipelineCache handle() const noexcept { return cache_.get(); }

    /// Save cache contents to disk.
    void save() const;

private:
    vk::UniquePipelineCache cache_;
    vk::Device device_;
    std::filesystem::path file_;
};

} // namespace volcano::core
