// src/web/bindings.cpp — embind API for @volcanoplot/web (VOLCANO_WEB)
//
// JS surface (mirrors the native API at the Figure level):
//   const fig = new volcano.Figure();
//   const ax = fig.addAxes(...); ax.plot(x, y, {color: ...});
//   fig.renderIfStale(); const frame = volcano.frameBytes();
//   interpreter.draw(frame)  // JS side walks the op stream
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>

#include "WebBackend.hpp"
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>

using namespace volcano;
namespace em = emscripten;

namespace {

struct Session {
    web::WebBackend backend;
    render::Renderer renderer{backend};
    plot::Figure figure{1, 1};
};

Session g_session;
Session& S() { return g_session; }

uintptr_t framePtr() { return uintptr_t(S().backend.lastFrame().first); }
size_t frameLen() { return S().backend.lastFrame().second; }

void resize(uint32_t w, uint32_t h) { S().backend.resize(w, h); }
bool renderNow() {
    // Renderer reads the canvas extent from the backend each frame.
    S().renderer.prepare(S().figure);
    S().renderer.renderFrame(S().figure);
    return true;
}
bool renderIfStale() {
    if (!S().figure.stale()) return false;
    S().renderer.prepare(S().figure);
    S().renderer.renderFrame(S().figure);
    return true;
}

/// Ensure at least one axes exists; returns the axes to target.
/// (v1: single-axes figure — axesIdx is ignored until multi-axes
/// bindings land.)
plot::Axes* targetAxes(uint32_t axesIdx) {
    (void)axesIdx;
    auto& fig = S().figure;
    auto ax = fig.allAxes();
    if (ax.empty()) return fig.addAxes(0, 0);
    return ax.front();
}

/// A Float32Array view over the WASM heap carries its linear-memory
/// address in byteOffset — directly usable as a C++ pointer.
const float* f32Span(em::val arr, size_t& n) {
    n = arr["length"].as<size_t>();
    auto off = static_cast<uintptr_t>(arr["byteOffset"].as<double>());
    return reinterpret_cast<const float*>(off);
}

plot::Series2D seriesFrom(em::val xs, em::val ys) {
    size_t nx, ny;
    const float* px = f32Span(xs, nx);
    const float* py = f32Span(ys, ny);
    const size_t n = std::min(nx, ny);
    plot::Series2D s;
    s.points.reserve(n);
    for (size_t i = 0; i < n; ++i)
        s.points.push_back({px[i], py[i]});
    return s;
}

uintptr_t line(uint32_t axesIdx, em::val xs, em::val ys,
               em::val color) {
    auto s = seriesFrom(xs, ys);
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>()))
            s.color = *c;
    }
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::LinePlot>(std::move(s));
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

uintptr_t scatter(uint32_t axesIdx, em::val xs, em::val ys,
                  em::val color) {
    auto s = seriesFrom(xs, ys);
    s.marker = plot::MarkerStyle::Circle;
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>()))
            s.color = *c;
    }
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::ScatterPlot>(std::move(s));
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

/// Allocate nbytes in the WASM heap for JS-side data staging.
/// JS writes a Float32Array over HEAPF32.subarray(ptr/4, ...) then
/// passes the view to line()/scatter() — zero copies.
uintptr_t alloc(size_t nbytes) {
    return reinterpret_cast<uintptr_t>(std::malloc(nbytes));
}
void free_(uintptr_t p) { std::free(reinterpret_cast<void*>(p)); }

/// Mailbox callback target: the JS interpreter calls this after
/// mapAsync with the 4×f32 result; routed to the waiting plot.
void mailbox(uint32_t slot, float v0, float v1, float v2, float v3) {
    (void)slot; (void)v0; (void)v1; (void)v2; (void)v3;
    // TODO(M3): OpGpuServices::deliverMailbox(slot, {v0..v3})
}

} // namespace

EMSCRIPTEN_BINDINGS(volcanoplot) {
    em::function("_vp_framePtr", &framePtr);
    em::function("_vp_frameLen", &frameLen);
    em::function("_vp_resize", &resize);
    em::function("_vp_render", &renderNow);
    em::function("_vp_renderIfStale", &renderIfStale);
    em::function("_vp_line", &line);
    em::function("_vp_scatter", &scatter);
    em::function("_vp_alloc", &alloc);
    em::function("_vp_free", &free_);
    em::function("_vp_mailbox", &mailbox);
}
#endif // __EMSCRIPTEN__
