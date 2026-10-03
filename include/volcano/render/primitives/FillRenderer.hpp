// volcano/render/primitives/FillRenderer.hpp — filled polygon renderer
//
// Backend-neutral interface (Vulkan impl: FillRendererVk).
// Instances come from GpuServices::createFillRenderer().
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

/// One axis-aligned 3-D box for the instanced `DrawBoxes3D` path
/// (OffloadConfig::instancing). 48 B — matches the WGSL `BoxInst`
/// record { vec3f o; vec3f s; vec4f c } (vec3 is 16 B aligned).
struct Box3DInstance {
    float ox, oy, oz, pad0;
    float sx, sy, sz, pad1;
    plot::Color color;
};
static_assert(sizeof(Box3DInstance) == 48);

/// Draws a filled polygon / triangle fan with per-vertex colors.
/// Used by fill_between, histograms, KDE areas, box bodies, etc.
class FillRenderer {
public:
    virtual ~FillRenderer() = default;

    virtual void upload(std::span<const plot::Point2D> positions,
                        std::span<const plot::Color> colors) = 0;

    /// Raw 3-D triangle soup + row-major view-projection; the backend
    /// projects in the vertex shader when the `projection3d` offload
    /// switch allows, else the points are projected on the CPU —
    /// identical output either way. `colors` is per-vertex (empty =
    /// flat white, matching upload()).
    void upload3D(std::span<const plot::Point3D> positions,
                  std::span<const plot::Color> colors,
                  const std::array<float, 16>& vp) {
        if (OffloadConfig::allowGpu(
                OffloadConfig::global().projection3d) &&
            upload3DDevice(positions, colors, vp)) return;
        cpuPos_.clear();
        cpuPos_.reserve(positions.size());
        for (const auto& p : positions)
            cpuPos_.push_back(plot::projectPoint3D(vp, p));
        upload(cpuPos_, colors);
    }

    /// Try the instanced-box path (`DrawBoxes3D`): one 48 B record per
    /// box, the vertex shader expands the unit cube and depth-testing
    /// provides occlusion — no CPU face expansion or painter's sort.
    /// Only engaged when the `instancing` offload switch allows *and*
    /// the backend implements uploadBoxes3DDevice; returns false
    /// otherwise, in which case the caller keeps its expanded path.
    /// Face shading (Bar3D/Voxels table) is applied by the shader.
    bool uploadBoxes3D(std::span<const Box3DInstance> boxes,
                       const std::array<float, 16>& vp) {
        if (!OffloadConfig::allowGpu(
                OffloadConfig::global().instancing)) return false;
        return uploadBoxes3DDevice(boxes, vp);
    }

    /// Adopt buffers produced by another service (e.g. the pcolormesh
    /// tessellator). The resources are owned by GpuServices; the tokens
    /// stay valid for the services' lifetime.
    virtual void adoptBuffers(GpuBuf positions, GpuBuf colors,
                              uint32_t count) = 0;

    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform) const = 0;

    [[nodiscard]] virtual GpuBuf pointBuffer() const noexcept = 0;
    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;

protected:
    /// Backend hook for upload3D (see LineSegmentRenderer).
    virtual bool upload3DDevice(std::span<const plot::Point3D>,
                                std::span<const plot::Color>,
                                const std::array<float, 16>&) {
        return false;
    }
    /// Backend hook for uploadBoxes3D (instanced unit-cube expansion).
    virtual bool uploadBoxes3DDevice(std::span<const Box3DInstance>,
                                     const std::array<float, 16>&) {
        return false;
    }

private:
    std::vector<plot::Point2D> cpuPos_;
};

} // namespace volcano::render::primitives
