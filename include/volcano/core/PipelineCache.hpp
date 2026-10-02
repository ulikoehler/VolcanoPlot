// volcano/core/PipelineCache.hpp — pipeline cache wrapper
#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <mutex>

namespace volcano::core {

/// Mutex serializing vkCreate*Pipelines calls that share a VkPipelineCache
/// (the cache object requires external synchronization). Callers that pass
/// VK_NULL_HANDLE do not need it. Used by the parallel renderer init in
/// Renderer::prepare().
inline std::mutex& pipelineCreationMutex() {
    static std::mutex m;
    return m;
}

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
