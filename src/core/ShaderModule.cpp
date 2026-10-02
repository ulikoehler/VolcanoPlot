// volcano/core/ShaderModule.cpp
#include "volcano/core/ShaderModule.hpp"

#include <cstdlib>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
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

/// FNV-1a 64-bit content hash — stable across runs, unlike std::hash.
uint64_t fnv1a(std::string_view s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

/// Disk cache dir for compiled SPIR-V: $VOLCANO_CACHE_DIR/shaders, then
/// $XDG_CACHE_HOME/volcanoplot/shaders, then ~/.cache/volcanoplot/shaders.
/// Empty = disabled (VOLCANO_NO_SHADER_CACHE=1 also disables).
std::filesystem::path shaderCacheDir() {
    if (const char* off = std::getenv("VOLCANO_NO_SHADER_CACHE");
        off && off[0] == '1')
        return {};
    std::filesystem::path dir;
    if (const char* d = std::getenv("VOLCANO_CACHE_DIR"); d && d[0]) {
        dir = d;
    } else if (const char* x = std::getenv("XDG_CACHE_HOME"); x && x[0]) {
        dir = std::filesystem::path(x) / "volcanoplot";
    } else if (const char* h = std::getenv("HOME"); h && h[0]) {
        dir = std::filesystem::path(h) / ".cache" / "volcanoplot";
    } else {
        return {};
    }
    dir /= "shaders";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? std::filesystem::path{} : dir;
}

std::optional<std::vector<uint32_t>> readSpvFile(
    const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::nullopt;
    auto size = f.tellg();
    if (size <= 0 || size % 4 != 0) return std::nullopt; // corrupt
    f.seekg(0);
    std::vector<uint32_t> buf(static_cast<size_t>(size) / sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return f ? std::optional{std::move(buf)} : std::nullopt;
}

void writeSpvFile(const std::filesystem::path& path,
                  const std::vector<uint32_t>& spirv) {
    // Write tmp + rename so concurrent processes never see a partial file.
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(spirv.data()),
                std::streamsize(spirv.size() * 4));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
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

    // Process-wide memo: GLSL→SPIR-V is deterministic and most
    // renderers compile identical sources (every plot layer owns its
    // primitive renderer), so repeats across figures are cache hits.
    static std::mutex mu;
    static std::unordered_map<std::string, std::vector<uint32_t>> cache;
    std::string key;
    key.reserve(source.size() + stage.size() + entryPoint.size() + 2);
    key += stage;
    key += '\x1f';
    key += entryPoint;
    key += '\x1f';
    key += source;
    {
        std::lock_guard lk(mu);
        if (auto it = cache.find(key); it != cache.end())
            return it->second;
    }
    // Persistent on-disk SPIR-V cache: skips shaderc entirely on every
    // run after the first. Content-keyed, so stale entries are never
    // returned; corrupt/partial files are ignored and overwritten.
    const std::filesystem::path dir = shaderCacheDir();
    const auto hash = fnv1a(key);
    if (!dir.empty()) {
        auto path = dir / std::format("{}-{:016x}.spv", stage, hash);
        if (auto spv = readSpvFile(path)) {
            std::lock_guard lk(mu);
            cache.emplace(std::move(key), *spv);
            return *spv;
        }
        auto spirv = [&] {
            shaderc::Compiler compiler;
            shaderc::CompileOptions opts;
            opts.SetOptimizationLevel(
                shaderc_optimization_level_performance);
            auto res = compiler.CompileGlslToSpv(std::string(source), kind,
                                                 "volcano_shader", opts);
            if (res.GetCompilationStatus() !=
                shaderc_compilation_status_success)
                throw std::runtime_error("shaderc compile failed: " +
                                         res.GetErrorMessage());
            return std::vector<uint32_t>{res.cbegin(), res.cend()};
        }();
        writeSpvFile(path, spirv);
        std::lock_guard lk(mu);
        cache.emplace(std::move(key), spirv);
        return spirv;
    }
    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);
    auto res = compiler.CompileGlslToSpv(std::string(source), kind, "volcano_shader", opts);
    if (res.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error("shaderc compile failed: " + res.GetErrorMessage());
    }
    std::vector<uint32_t> spirv{res.cbegin(), res.cend()};
    {
        std::lock_guard lk(mu);
        cache.emplace(std::move(key), spirv);
    }
    return spirv;
}
#else
std::vector<uint32_t> ShaderModule::compileGlsl(std::string_view, std::string_view, std::string_view) {
    throw std::runtime_error("Runtime shader compilation disabled (no shaderc)");
}
#endif

} // namespace volcano::core
