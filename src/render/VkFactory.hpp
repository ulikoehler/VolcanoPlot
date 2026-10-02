// src/render/VkFactory.hpp — native primitive factories
//
// NATIVE BUILDS ONLY. Each primitive's Vulkan implementation class lives
// inside its .cpp (XxxRendererVk); these factory functions construct +
// init them against a VulkanGpuServices. The WebGPU equivalents are the
// Op* classes in src/web/ (created by OpGpuServices instead).
#pragma once

#include <volcano/render/GpuServices.hpp>

namespace volcano::render {

class VulkanGpuServices;

namespace primitives {
class SpineRenderer;
class PointRenderer;
class LineRenderer;
class LineSegmentRenderer;
class FillRenderer;
class BarRenderer;
class PieRenderer;
class HeatmapRenderer;
class SurfaceRenderer;
class InstancedPathRenderer;
class GpuLineRenderer;
class ReduceRenderer;
class EvalRenderer;
class KdeEvalRenderer;

std::unique_ptr<SpineRenderer> makeSpineVk(VulkanGpuServices&);
std::unique_ptr<PointRenderer> makePointVk(VulkanGpuServices&);
std::unique_ptr<LineRenderer> makeLineVk(VulkanGpuServices&);
std::unique_ptr<LineSegmentRenderer> makeLineSegmentVk(VulkanGpuServices&);
std::unique_ptr<FillRenderer> makeFillVk(VulkanGpuServices&);
std::unique_ptr<BarRenderer> makeBarVk(VulkanGpuServices&);
std::unique_ptr<PieRenderer> makePieVk(VulkanGpuServices&);
std::unique_ptr<HeatmapRenderer> makeHeatmapVk(VulkanGpuServices&);
std::unique_ptr<SurfaceRenderer> makeSurfaceVk(VulkanGpuServices&);
std::unique_ptr<InstancedPathRenderer> makeInstancedPathVk(VulkanGpuServices&);
std::unique_ptr<GpuLineRenderer> makeGpuLineVk(VulkanGpuServices&);
std::unique_ptr<ReduceRenderer> makeReduceVk(VulkanGpuServices&);
std::unique_ptr<EvalRenderer> makeEvalVk(VulkanGpuServices&);
std::unique_ptr<KdeEvalRenderer> makeKdeEvalVk(VulkanGpuServices&);
} // namespace primitives

std::unique_ptr<Grid3DRenderer> makeGrid3DVk(VulkanGpuServices&);

} // namespace volcano::render

namespace volcano::text {
/// Constructed by VulkanGpuServices::ensureText() — impl lives in
/// src/text/TextRenderer.cpp alongside the CPU base-class methods.
std::unique_ptr<TextRenderer> makeTextVk(render::VulkanGpuServices&);
}
