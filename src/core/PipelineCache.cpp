// volcano/core/PipelineCache.cpp
#include "volcano/core/PipelineCache.hpp"

#include <fstream>
#include <vector>

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
    if (cache_ && !file_.empty()) save();
}

void PipelineCache::save() const {
    if (!cache_ || file_.empty()) return;
    auto data = device_.getPipelineCacheData(cache_.get());
    std::ofstream f(file_, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace volcano::core
