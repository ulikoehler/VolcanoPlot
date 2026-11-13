// volcano/core/PipelineCache.cpp
#include "volcano/core/PipelineCache.hpp"

#include <atomic>
#include <fstream>
#include <vector>

#include <unistd.h>

namespace volcano::core {

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

} // namespace

PipelineCache::PipelineCache(vk::Device device, const std::filesystem::path& cacheFile)
    : device_(device), file_(cacheFile) {
    std::vector<uint8_t> initial;
    if (!cacheFile.empty()) initial = readFile(cacheFile);
    vk::PipelineCacheCreateInfo ci{};
    if (!initial.empty()) {
        ci.setInitialDataSize(initial.size())
           .setPInitialData(initial.data());
    }
    cache_ = device.createPipelineCacheUnique(ci);
}

PipelineCache::~PipelineCache() {
    // getPipelineCacheData is result-checked — an exception escaping
    // a destructor is std::terminate.
    try {
        if (cache_ && !file_.empty()) save();
    } catch (...) {
    }
}

void PipelineCache::save() const {
    if (!cache_ || file_.empty()) return;
    auto data = device_.getPipelineCacheData(cache_.get());
    // The cache file is shared by every figure in the process (and
    // across processes) — a direct truncating write can be read
    // mid-flight or interleave with a concurrent writer, producing a
    // torn blob. The Vulkan header may still validate while the body
    // is garbage — and drivers have crashed feeding a corrupt cache
    // into pipeline creation. Write-then-rename keeps every reader
    // on one complete version.
    std::error_code ec;
    static std::atomic<uint64_t> seq{0};
    auto tmp = file_;
    tmp += "." + std::to_string(::getpid()) + "." +
           std::to_string(seq.fetch_add(1)) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, file_, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

} // namespace volcano::core
