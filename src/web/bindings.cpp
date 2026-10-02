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
#include <volcano/plot/plots/FunctionPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/HistPlot.hpp>
#include <volcano/plot/plots/PiePlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>
#include <volcano/plot/plots/SurfacePlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/StemPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>
#include <volcano/plot/plots/ECDFPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/BoxPlot.hpp>
#include <volcano/plot/plots/Hist2DPlot.hpp>
#include <volcano/plot/plots/HexbinPlot.hpp>
#include <volcano/plot/plots/QuiverPlot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/PcolormeshPlot.hpp>
#include <volcano/plot/plots/KDEPlot.hpp>
#include <volcano/plot/Colormap.hpp>

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
    S().backend.resetStream();
    S().renderer.prepare(S().figure);
    S().renderer.renderFrame(S().figure);
    return true;
}
bool renderIfStale() {
    if (!S().figure.stale()) return false;
    S().backend.resetStream();
    S().renderer.prepare(S().figure);
    S().renderer.renderFrame(S().figure);
    return true;
}

/// Axes are indexed by insertion order in Figure::allAxes().
plot::Axes* targetAxes(uint32_t axesIdx) {
    auto& fig = S().figure;
    auto ax = fig.allAxes();
    if (ax.empty()) return fig.addAxes(0, 0);
    if (axesIdx >= ax.size()) return ax.front();
    return ax[axesIdx];
}

/// mpl subplot(nrows, ncols, index) — resets the top grid and places an
/// axes at the 1-based cell index. Returns the axesIdx for plot calls.
uint32_t subplot(uint32_t nrows, uint32_t ncols, uint32_t index) {
    auto& fig = S().figure;
    const uint32_t i = index ? index - 1 : 0;
    plot::Axes* created = fig.subplot2grid({nrows, ncols},
        {i / ncols, i % ncols});
    auto all = fig.allAxes();
    for (uint32_t k = 0; k < all.size(); ++k)
        if (all[k] == created) return k;
    return 0;
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

uintptr_t func(uint32_t axesIdx, const std::string& body,
               double xMin, double xMax, em::val color) {
    auto* ax = targetAxes(axesIdx);
    plot::Color col = plot::Color::blue();
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>()))
            col = *c;
    }
    auto plot = std::make_shared<plot::FunctionPlot>(
        body, plot::Range{float(xMin), float(xMax)}, 1024, col);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

std::vector<float> f32vec(em::val arr) {
    size_t n; const float* p = f32Span(arr, n);
    return {p, p + n};
}
/// JS array of strings → std::vector<std::string>.
std::vector<std::string> strvec(em::val arr) {
    std::vector<std::string> v;
    const size_t n = arr["length"].as<size_t>();
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) v.push_back(arr[i].as<std::string>());
    return v;
}

uintptr_t bar(uint32_t axesIdx, em::val heights, em::val labels,
              em::val color) {
    plot::BarData d;
    d.heights = f32vec(heights);
    d.labels = strvec(labels);
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>()))
            d.colors.push_back(*c);
    }
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::BarPlot>(std::move(d));
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

uintptr_t hist(uint32_t axesIdx, em::val samples, int bins,
               em::val color) {
    plot::HistConfig cfg;
    cfg.bins = plot::HistBinMethod::Fixed;
    cfg.binCount = bins > 0 ? bins : 10;
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>()))
            cfg.color = *c;
    }
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::HistPlot>(f32vec(samples), cfg);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

uintptr_t pie(uint32_t axesIdx, em::val values, em::val labels) {
    plot::PieData d;
    d.values = f32vec(values);
    d.labels = strvec(labels);
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::PiePlot>(std::move(d));
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

uintptr_t heatmap(uint32_t axesIdx, em::val values, uint32_t w,
                  uint32_t h, const std::string& cmapName) {
    plot::Grid2D g;
    g.values = f32vec(values);
    g.width = w; g.height = h;
    g.xRange = {0, float(w)}; g.yRange = {0, float(h)};
    const plot::Colormap& cm = plot::Colormap::byName(cmapName);
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::HeatmapPlot>(std::move(g), cm);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

/// 3D surface: row-major height grid, camera via mpl viewInit angles.
uintptr_t surface(uint32_t axesIdx, em::val values, uint32_t w,
                  uint32_t h, double elevDeg, double azimDeg) {
    plot::Grid2D g;
    g.values = f32vec(values);
    g.width = w; g.height = h;
    g.xRange = {0, float(w)}; g.yRange = {0, float(h)};
    auto cam = plot::Camera3D::viewInit(float(elevDeg), float(azimDeg));
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<plot::SurfacePlot>(std::move(g), cam);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

/// Register a freshly-built plot on `axesIdx`; returns a JS handle.
template <typename P, typename... Args>
uintptr_t addPlot(uint32_t axesIdx, Args&&... args) {
    auto* ax = targetAxes(axesIdx);
    auto plot = std::make_shared<P>(std::forward<Args>(args)...);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

uintptr_t errorbar(uint32_t axesIdx, em::val xs, em::val ys,
                   em::val yerr, em::val color) {
    plot::ErrorbarConfig cfg;
    cfg.yerr = f32vec(yerr);
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>())) {
            cfg.color = *c; cfg.markerColor = *c; cfg.errorbarColor = *c;
        }
    }
    return addPlot<plot::ErrorbarPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                       std::move(cfg));
}

uintptr_t stem(uint32_t axesIdx, em::val xs, em::val ys) {
    return addPlot<plot::StemPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                   plot::StemConfig{});
}

uintptr_t step(uint32_t axesIdx, em::val xs, em::val ys,
               const std::string& where) {
    auto w = where == "post" ? plot::StepWhere::Post
           : where == "mid"  ? plot::StepWhere::Mid
                             : plot::StepWhere::Pre;
    return addPlot<plot::StepPlot>(axesIdx, f32vec(xs), f32vec(ys), w);
}

uintptr_t ecdf(uint32_t axesIdx, em::val samples) {
    return addPlot<plot::ECDFPlot>(axesIdx, f32vec(samples));
}

uintptr_t fillBetween(uint32_t axesIdx, em::val xs, em::val y1,
                      em::val y2, em::val color) {
    plot::Color col = plot::Color::fromRgba8(31, 119, 180, 128);
    if (color.typeOf().as<std::string>() == "string") {
        if (auto c = plot::Color::parse(color.as<std::string>()))
            col = *c;
    }
    return addPlot<plot::FillBetweenPlot>(axesIdx, f32vec(xs), f32vec(y1),
                                          f32vec(y2), col);
}

uintptr_t boxplot(uint32_t axesIdx, em::val groups) {
    std::vector<std::vector<float>> gs;
    const size_t n = groups["length"].as<size_t>();
    gs.reserve(n);
    for (size_t i = 0; i < n; ++i) gs.push_back(f32vec(groups[i]));
    return addPlot<plot::BoxPlot>(axesIdx, std::move(gs));
}

uintptr_t hist2d(uint32_t axesIdx, em::val xs, em::val ys,
                 uint32_t bins, const std::string& cmapName) {
    plot::Hist2DConfig cfg;
    cfg.bins = plot::Hist2DBinMethod::Fixed;
    cfg.nBinsX = cfg.nBinsY = int(bins ? bins : 10);
    cfg.cmap = &plot::Colormap::byName(cmapName);
    return addPlot<plot::Hist2DPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                     std::move(cfg));
}

uintptr_t hexbin(uint32_t axesIdx, em::val xs, em::val ys) {
    return addPlot<plot::HexbinPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                     plot::HexbinConfig{});
}

uintptr_t quiver(uint32_t axesIdx, em::val xs, em::val ys,
                 em::val us, em::val vs) {
    return addPlot<plot::QuiverPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                     f32vec(us), f32vec(vs),
                                     plot::QuiverConfig{});
}

uintptr_t contour(uint32_t axesIdx, em::val values, uint32_t w,
                  uint32_t h, uint32_t levels, const std::string& cmapName) {
    plot::Grid2D g;
    g.values = f32vec(values);
    g.width = w; g.height = h;
    g.xRange = {0, float(w)}; g.yRange = {0, float(h)};
    plot::ContourConfig cfg;
    cfg.numLevels = int(levels ? levels : 10);
    cfg.cmap = &plot::Colormap::byName(cmapName);
    return addPlot<plot::ContourPlot>(axesIdx, std::move(g), std::move(cfg));
}

uintptr_t pcolormesh(uint32_t axesIdx, em::val xs, em::val ys,
                     em::val cs, uint32_t nCols, uint32_t nRows) {
    return addPlot<plot::PcolormeshPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                         f32vec(cs), nCols, nRows,
                                         plot::PcolormeshConfig{});
}

uintptr_t kde(uint32_t axesIdx, em::val xs, em::val ys,
              const std::string& cmapName) {
    size_t nx = 0, ny = 0;
    const float* px = f32Span(xs, nx);
    const float* py = f32Span(ys, ny);
    const size_t n = std::min(nx, ny);
    std::vector<plot::Point2D> pts(n);
    for (size_t i = 0; i < n; ++i) pts[i] = {px[i], py[i]};
    return addPlot<plot::KDEPlot>(axesIdx, std::move(pts), 256, 256, 0.0f,
                                  plot::Colormap::byName(cmapName));
}

/// In-place data update for a plot handle returned by line()/scatter()
/// (the oscilloscope/ring-buffer path — no plot reallocation).
void setData(uintptr_t handle, em::val xs, em::val ys) {
    size_t nx, ny;
    const float* px = f32Span(xs, nx);
    const float* py = f32Span(ys, ny);
    const size_t n = std::min(nx, ny);
    std::vector<float> x(px, px + n), y(py, py + n);
    auto* p = reinterpret_cast<plot::IPlot*>(handle);
    if (auto* lp = dynamic_cast<plot::LinePlot*>(p)) lp->setData(std::move(x), std::move(y));
    else if (auto* sp = dynamic_cast<plot::ScatterPlot*>(p)) sp->setData(std::move(x), std::move(y));
    S().figure.markStale();
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
    static_cast<web::OpGpuServices&>(S().backend.gpu())
        .deliverMailbox(slot, v0, v1, v2, v3);
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
    em::function("_vp_setData", &setData);
    em::function("_vp_function", &func);
    em::function("_vp_bar", &bar);
    em::function("_vp_hist", &hist);
    em::function("_vp_pie", &pie);
    em::function("_vp_heatmap", &heatmap);
    em::function("_vp_surface", &surface);
    em::function("_vp_errorbar", &errorbar);
    em::function("_vp_stem", &stem);
    em::function("_vp_step", &step);
    em::function("_vp_ecdf", &ecdf);
    em::function("_vp_fillBetween", &fillBetween);
    em::function("_vp_boxplot", &boxplot);
    em::function("_vp_hist2d", &hist2d);
    em::function("_vp_hexbin", &hexbin);
    em::function("_vp_quiver", &quiver);
    em::function("_vp_contour", &contour);
    em::function("_vp_pcolormesh", &pcolormesh);
    em::function("_vp_kde", &kde);
    em::function("_vp_subplot", &subplot);
}
#endif // __EMSCRIPTEN__
