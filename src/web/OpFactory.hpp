// src/web/OpFactory.hpp — op-recording impl factories (VOLCANO_WEB)
#pragma once

#include <memory>
#include <volcano/render/GpuServices.hpp>
#include <volcano/text/TextRenderer.hpp>

namespace volcano::web {

class OpGpuServices;

namespace op {
std::unique_ptr<render::primitives::SpineRenderer>
    makeSpine(OpGpuServices&);
std::unique_ptr<render::primitives::PointRenderer>
    makePoint(OpGpuServices&);
std::unique_ptr<render::primitives::LineRenderer>
    makeLine(OpGpuServices&);
std::unique_ptr<render::primitives::LineSegmentRenderer>
    makeLineSegment(OpGpuServices&);
std::unique_ptr<render::primitives::FillRenderer>
    makeFill(OpGpuServices&);
std::unique_ptr<render::primitives::BarRenderer>
    makeBar(OpGpuServices&);
std::unique_ptr<render::primitives::PieRenderer>
    makePie(OpGpuServices&);
std::unique_ptr<render::primitives::HeatmapRenderer>
    makeHeatmap(OpGpuServices&);
std::unique_ptr<render::primitives::SurfaceRenderer>
    makeSurface(OpGpuServices&);
std::unique_ptr<render::primitives::InstancedPathRenderer>
    makeInstancedPath(OpGpuServices&);
std::unique_ptr<render::primitives::GpuLineRenderer>
    makeGpuLine(OpGpuServices&);
std::unique_ptr<render::primitives::ReduceRenderer>
    makeReduce(OpGpuServices&);
std::unique_ptr<render::primitives::EvalRenderer>
    makeEval(OpGpuServices&);
std::unique_ptr<render::primitives::KdeEvalRenderer>
    makeKdeEval(OpGpuServices&);
std::unique_ptr<render::Grid3DRenderer>
    makeGrid3D(OpGpuServices&);
std::unique_ptr<text::TextRenderer>
    makeText(OpGpuServices&);
} // namespace op

} // namespace volcano::web
