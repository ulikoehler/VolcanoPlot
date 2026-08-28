// volcano/core/ShaderModule.hpp — SPIR-V shader module + runtime compile
#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace volcano::core {

class Device;

class ShaderModule {
public:
    ShaderModule() = default;
    ShaderModule(vk::Device device, std::span<const uint32_t> spirv);
    /// Load precompiled SPIR-V from a .spv file.
    ShaderModule(vk::Device device, const std::filesystem::path& spvPath);

    [[nodiscard]] vk::ShaderModule handle() const noexcept { return module_.get(); }

    /// Compile GLSL source to SPIR-V at runtime (requires shaderc).
    /// stage: e.g. "vert", "frag", "comp".
    static std::vector<uint32_t> compileGlsl(std::string_view source,
                                             std::string_view stage,
                                             std::string_view entryPoint = "main");

private:
    vk::UniqueShaderModule module_;
};

} // namespace volcano::core
