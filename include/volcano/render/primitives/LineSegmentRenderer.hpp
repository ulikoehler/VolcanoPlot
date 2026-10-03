// volcano/render/primitives/LineSegmentRenderer.hpp — segment list renderer
//
// Backend-neutral interface (Vulkan impl: LineSegmentRendererVk).
// Instances come from GpuServices::createLineSegmentRenderer().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>
#include <volcano/render/Offload.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace volcano::render::primitives {

/// Draws independent line segments (two vertices each) with per-draw
/// line type. On Vulkan this uses the line-list topology; on WebGPU the
/// interpreter tessellates since line width is fixed at 1 px there.
class LineSegmentRenderer {
public:
    virtual ~LineSegmentRenderer() = default;

    virtual void upload(std::span<const plot::Point2D> points,
                        plot::Color color, float width) = 0;
    /// Upload raw 3-D segment endpoints plus the row-major
    /// view-projection. When the `projection3d` offload switch allows
    /// and the backend implements upload3DDevice, the vertices stay 3-D
    /// and projection happens in the vertex shader; otherwise they are
    /// projected on the CPU — identical output either way.
    void upload3D(std::span<const plot::Point3D> points,
                  plot::Color color, float width,
                  const std::array<float, 16>& vp) {
        if (OffloadConfig::allowGpu(
                OffloadConfig::global().projection3d) &&
            upload3DDevice(points, color, width, vp)) return;
        upload3DCpu(points, color, width, vp);
    }
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform,
                      uint32_t vertexCount) const = 0;

    [[nodiscard]] virtual GpuBuf pointBuffer() const noexcept = 0;
    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;

protected:
    /// Backend hook for upload3D: return true when the raw 3-D data was
    /// consumed (draw() must then emit the projected-vertex-shader op).
    virtual bool upload3DDevice(std::span<const plot::Point3D>,
                                plot::Color, float,
                                const std::array<float, 16>&) {
        return false;
    }
    /// CPU fallback for upload3D — projects via projectPoint3D and feeds
    /// upload(). The projected points are y-up NDC (the same convention
    /// 2-D segment shaders expect, i.e. they own the y-flip).
    virtual void upload3DCpu(std::span<const plot::Point3D> points,
                             plot::Color color, float width,
                             const std::array<float, 16>& vp) {
        cpuScratch_.clear();
        cpuScratch_.reserve(points.size());
        for (const auto& p : points)
            cpuScratch_.push_back(plot::projectPoint3D(vp, p));
        upload(cpuScratch_, color, width);
    }

private:
    std::vector<plot::Point2D> cpuScratch_;
};

} // namespace volcano::render::primitives
