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

void resize(uint32_t w, uint32_t h) {
    S().backend.resize(w, h);
    S().figure.setExtent(plot::Extent2D{w, h});
}
bool render() {
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

/// Line plot: xs/ys as flat f32 spans — embind passes typed arrays.
uintptr_t line(uint32_t axesIdx, em::val xs, em::val ys,
               em::val color) {
    (void)axesIdx;
    // TODO(M1+): full Series2D/plot API surface — v1 skeleton takes
    // Float32Arrays and registers a LinePlot.
    return 0;
}

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
    em::function("_vp_render", &render);
    em::function("_vp_renderIfStale", &renderIfStale);
    em::function("_vp_mailbox", &mailbox);
}
#endif // __EMSCRIPTEN__
