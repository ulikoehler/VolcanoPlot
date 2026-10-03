// volcano/render/Offload.cpp — offload policy parsing + process default
#include <volcano/render/Offload.hpp>

#include <array>
#include <cstdlib>
#include <utility>

namespace volcano::render {

namespace {

struct Entry {
    std::string_view name;
    OffloadMode OffloadConfig::* member;
};

constexpr std::array kEntries{
    Entry{"stroking", &OffloadConfig::stroking},
    Entry{"dashes", &OffloadConfig::dashes},
    Entry{"contours", &OffloadConfig::contours},
    Entry{"binning", &OffloadConfig::binning},
    Entry{"projection3d", &OffloadConfig::projection3d},
    Entry{"instancing", &OffloadConfig::instancing},
    Entry{"fft", &OffloadConfig::fft},
    Entry{"envelope", &OffloadConfig::envelope},
    Entry{"surfacemesh", &OffloadConfig::surfacemesh},
    Entry{"arrows", &OffloadConfig::arrows},
    Entry{"fillbetween", &OffloadConfig::fillbetween},
    Entry{"streamlines", &OffloadConfig::streamlines},
    Entry{"depthsort", &OffloadConfig::depthsort},
    Entry{"splatting", &OffloadConfig::splatting},
};

/// Parse "auto" | "gpu" | "cpu" | "on" | "off" | "1" | "0".
bool parseMode(std::string_view v, OffloadMode& out) {
    if (v == "gpu" || v == "on" || v == "1") { out = OffloadMode::Gpu; return true; }
    if (v == "cpu" || v == "off" || v == "0") { out = OffloadMode::Cpu; return true; }
    if (v == "auto") { out = OffloadMode::Auto; return true; }
    return false;
}

} // namespace

OffloadMode* OffloadConfig::find(std::string_view name) noexcept {
    for (auto& e : kEntries)
        if (e.name == name) return &(this->*(e.member));
    return nullptr;
}

void OffloadConfig::parse(std::string_view csv) {
    size_t i = 0;
    while (i < csv.size()) {
        size_t comma = csv.find(',', i);
        if (comma == std::string_view::npos) comma = csv.size();
        auto item = csv.substr(i, comma - i);
        i = comma + 1;
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
            item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
            item.remove_suffix(1);
        if (item.empty()) continue;
        auto eq = item.find('=');
        if (eq == std::string_view::npos) continue;
        if (auto* slot = find(item.substr(0, eq))) {
            OffloadMode m{};
            if (parseMode(item.substr(eq + 1), m)) *slot = m;
        }
    }
}

OffloadConfig& OffloadConfig::global() {
    static OffloadConfig cfg = [] {
        OffloadConfig c;
#ifdef VOLCANO_WEB
        // The browser build runs the whole engine on one main thread, so
        // a host-side binning loop blocks input and animation even when
        // it would be nominally faster. Default to the GPU there.
        c.binning = OffloadMode::Gpu;
#endif
        if (const char* v = std::getenv("VOLCANO_GPU_OFFLOAD"); v && *v)
            c.parse(v);
        // Legacy switch for the histogram binning path.
        if (const char* v = std::getenv("VOLCANO_GPU_HIST"); v && v[0] == '1')
            c.binning = OffloadMode::Gpu;
        return c;
    }();
    return cfg;
}

} // namespace volcano::render
