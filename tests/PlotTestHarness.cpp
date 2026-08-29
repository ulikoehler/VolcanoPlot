// tests/PlotTestHarness.cpp — implementation of the regression test harness
#include "PlotTestHarness.hpp"

#include <volcano/encode/ImageEncoder.hpp>

#include <fstream>
#include <stdexcept>

namespace volcano::test {

PlotTestHarness::PlotTestHarness(uint32_t width, uint32_t height,
                                 vk::SampleCountFlagBits samples) {
    backend::BackendDesc desc;
    desc.width = width;
    desc.height = height;
    desc.samples = samples;
    desc.enableValidation = false;
    backend_ = backend::createHeadlessBackend(desc);
    renderer_ = std::make_unique<render::Renderer>(*backend_);
}

Image PlotTestHarness::render(plot::Figure& figure) {
    renderer_->prepare(figure);
    renderer_->renderFrame(figure);
    auto px = backend_->readbackRgba8();
    return Image::fromRgba8(px, backend_->extent().width, backend_->extent().height);
}

bool Image::savePpm(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << width_ << " " << height_ << "\n255\n";
    for (size_t i = 0; i < data_.size(); i += 4) {
        f.put(char(data_[i]));
        f.put(char(data_[i+1]));
        f.put(char(data_[i+2]));
    }
    return bool(f);
}

bool Image::save(const std::string& path) const {
    // Try PNG encoder first (writes to file directly).
    try {
        auto enc = encode::createCpuEncoder(encode::ImageFormat::Png);
        if (enc->encodeToFile(raw(), width_, height_, path)) return true;
    } catch (...) {
        // fall through to PPM
    }
    // Fallback: PPM (replace .png extension with .ppm)
    auto ppmPath = path;
    if (ppmPath.ends_with(".png")) ppmPath.replace(ppmPath.size() - 4, 4, ".ppm");
    return savePpm(ppmPath);
}

} // namespace volcano::test
