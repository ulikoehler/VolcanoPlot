// volcano/core/ShaderModule.cpp
#include "volcano/core/ShaderModule.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef VOLCANO_RUNTIME_SHADER_COMPILE
#include <shaderc/shaderc.hpp>
#endif

namespace volcano::core {

namespace {

std::vector<uint32_t> readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Failed to open SPIR-V: " + path.string());
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint32_t> buf(static_cast<size_t>(size) / sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

} // namespace

ShaderModule::ShaderModule(vk::Device device, std::span<const uint32_t> spirv) {
    vk::ShaderModuleCreateInfo ci{};
    ci.setCodeSize(spirv.size_bytes())
       .setPCode(spirv.data());
    module_ = device.createShaderModuleUnique(ci);
}

ShaderModule::ShaderModule(vk::Device device, const std::filesystem::path& spvPath)
    : ShaderModule(device, readFile(spvPath)) {}

#ifdef VOLCANO_RUNTIME_SHADER_COMPILE
std::vector<uint32_t> ShaderModule::compileGlsl(std::string_view source,
                                                std::string_view stage,
                                                std::string_view entryPoint) {
    shaderc_shader_kind kind;
    if (stage == "vert")      kind = shaderc_vertex_shader;
    else if (stage == "frag") kind = shaderc_fragment_shader;
    else if (stage == "comp") kind = shaderc_compute_shader;
    else if (stage == "geom") kind = shaderc_geometry_shader;
    else if (stage == "tesc") kind = shaderc_tess_control_shader;
    else if (stage == "tese") kind = shaderc_tess_evaluation_shader;
    else throw std::runtime_error("Unknown shader stage: " + std::string(stage));

    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);
    auto res = compiler.CompileGlslToSpv(std::string(source), kind, "volcano_shader", opts);
    if (res.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error("shaderc compile failed: " + res.GetErrorMessage());
    }
    return {res.cbegin(), res.cend()};
}
#else
std::vector<uint32_t> ShaderModule::compileGlsl(std::string_view, std::string_view, std::string_view) {
    throw std::runtime_error("Runtime shader compilation disabled (no shaderc)");
}
#endif

} // namespace volcano::core
