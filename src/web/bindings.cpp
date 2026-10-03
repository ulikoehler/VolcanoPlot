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
#include <volcano/plot/Interaction.hpp>
#include <volcano/plot/Events.hpp>
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
#include <volcano/plot/plots/ViolinPlot.hpp>
#include <volcano/plot/plots/StackPlot.hpp>
#include <volcano/plot/plots/FillPlot.hpp>
#include <volcano/plot/plots/SpyPlot.hpp>
#include <volcano/plot/plots/TripcolorPlot.hpp>
#include <volcano/plot/plots/StreamPlot.hpp>
#include <volcano/plot/plots/MatshowPlot.hpp>
#include <volcano/plot/plots/PcolorfastPlot.hpp>
#include <volcano/plot/plots/BrokenBarHPlot.hpp>
#include <volcano/plot/plots/TriContourPlot.hpp>
#include <volcano/plot/plots/TricontourPlot.hpp>
#include <volcano/plot/plots/NavCubePlot.hpp>
#include <volcano/plot/plots/QuiverKeyPlot.hpp>
#include <volcano/plot/plots/TriplotPlot.hpp>
#include <volcano/plot/plots/SpecgramPlot.hpp>
#include <volcano/plot/plots/SpectrumPlot.hpp>
#include <volcano/plot/plots/PsdPlot.hpp>
#include <volcano/plot/plots/CsdPlot.hpp>
#include <volcano/plot/plots/XCorrPlot.hpp>
#include <volcano/plot/plots/CoherePlot.hpp>
#include <volcano/plot/plots/WireframePlot.hpp>
#include <volcano/plot/plots/TrisurfPlot.hpp>
#include <volcano/plot/plots/Plot3D.hpp>
#include <volcano/plot/plots/Scatter3D.hpp>
#include <volcano/plot/plots/Bar3D.hpp>
#include <volcano/plot/plots/Quiver3D.hpp>
#include <volcano/plot/plots/Errorbar3D.hpp>
#include <volcano/plot/plots/Contour3D.hpp>
#include <volcano/plot/plots/VoxelsPlot.hpp>
#include <volcano/plot/plots/Text3D.hpp>
#include <volcano/plot/plots/BarbsPlot.hpp>
#include <volcano/plot/plots/GroupedBarPlot.hpp>
#include <volcano/plot/plots/FigImagePlot.hpp>
#include <volcano/plot/plots/ChirpPlot.hpp>
#include <volcano/plot/plots/MexicanHatPlot.hpp>
#include <volcano/plot/plots/BarLabelPlot.hpp>
#include <volcano/plot/plots/Axes3DPlot.hpp>
#include <volcano/plot/Colormap.hpp>
#include <algorithm>
#include <limits>
#include <unordered_map>

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

/// Enable mpl-style interaction: button-1 drag pans, scroll zooms
/// (scale-aware) about the cursor. Figure::dispatch does hit-testing,
/// widget routing and legend dragging on its own.
void setInteractive(bool on) {
    auto& nav = S().figure.nav();
    nav.scrollZoom = on;
    const bool panMode = nav.mode() == plot::Navigation::Mode::Pan;
    if (on != panMode) nav.pan();
}

/// Feed a UI event into the figure's mpl-style dispatch. Returns true
/// when the event changed the figure (caller should re-render).
bool dispatchEvent(uint32_t type, double x, double y, int32_t button,
                   double step) {
    auto& fig = S().figure;
    plot::Event e;
    e.type = static_cast<plot::Event::Type>(type);
    e.x = float(x); e.y = float(y);
    e.button = button; e.step = float(step);
    fig.dispatch(std::move(e));
    return fig.stale();
}

// Forward decls — helpers defined further down.
plot::Point3D min3(const std::vector<float>& a,
                   const std::vector<float>& b,
                   const std::vector<float>& c);
plot::Point3D max3(const std::vector<float>& a,
                   const std::vector<float>& b,
                   const std::vector<float>& c);
std::pair<plot::Point3D, plot::Point3D>
sync3DBox(uint32_t axesIdx, double elevDeg, double azimDeg,
          plot::Point3D dataMin, plot::Point3D dataMax);

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
    float vmin = std::numeric_limits<float>::max(),
          vmax = std::numeric_limits<float>::lowest();
    for (float v : g.values) {
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    auto cam = plot::Camera3D::viewInit(float(elevDeg), float(azimDeg));
    auto box = sync3DBox(axesIdx, elevDeg, azimDeg,
        {0, 0, vmin}, {float(w), float(h), vmax});
    cam.dataMin = box.first; cam.dataMax = box.second;
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

/// Violin plot: `groups` is a JS array of Float32Arrays (one per violin).
uintptr_t violin(uint32_t axesIdx, em::val groups, double width,
                 bool showBox, const std::string& color) {
    std::vector<std::vector<float>> gs;
    const size_t n = groups["length"].as<size_t>();
    gs.reserve(n);
    for (size_t i = 0; i < n; ++i) gs.push_back(f32vec(groups[i]));
    plot::ViolinConfig cfg;
    if (width > 0) cfg.width = float(width);
    cfg.showBox = showBox;
    if (auto c = plot::Color::parse(color)) {
        cfg.bodyColor = *c; cfg.edgeColor = *c;
    }
    return addPlot<plot::ViolinPlot>(axesIdx, std::move(gs),
                                     std::move(cfg));
}

/// Stacked area: `ys` is a JS array of Float32Arrays (one per layer).
uintptr_t stackplot(uint32_t axesIdx, em::val xs, em::val ys) {
    std::vector<std::vector<float>> layers;
    const size_t n = ys["length"].as<size_t>();
    layers.reserve(n);
    for (size_t i = 0; i < n; ++i) layers.push_back(f32vec(ys[i]));
    return addPlot<plot::StackPlot>(axesIdx, f32vec(xs),
                                    std::move(layers));
}

/// mpl fill(): filled polygon from x/y arrays.
uintptr_t fill(uint32_t axesIdx, em::val xs, em::val ys,
               const std::string& color) {
    auto s = seriesFrom(xs, ys);
    if (auto c = plot::Color::parse(color)) s.color = *c;
    return addPlot<plot::FillPlot>(axesIdx, std::move(s));
}

/// mpl spy(): sparsity pattern of a row-major matrix.
uintptr_t spy(uint32_t axesIdx, em::val data, uint32_t nrows,
              uint32_t ncols) {
    return addPlot<plot::SpyPlot>(axesIdx, f32vec(data), nrows, ncols,
                                  plot::SpyConfig{});
}

/// mpl tripcolor: Delaunay triangulation of x/y colored by z.
uintptr_t tripcolor(uint32_t axesIdx, em::val xs, em::val ys,
                    em::val zs) {
    return addPlot<plot::TripcolorPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                        f32vec(zs),
                                        plot::TripcolorConfig{});
}

/// mpl streamplot: vector field over a regular grid (row-major u/v).
uintptr_t streamplot(uint32_t axesIdx, em::val us, em::val vs,
                     uint32_t w, uint32_t h) {
    plot::Grid2D gu, gv;
    gu.values = f32vec(us); gv.values = f32vec(vs);
    gu.width = gv.width = w; gu.height = gv.height = h;
    gu.xRange = gv.xRange = {0, float(w)};
    gu.yRange = gv.yRange = {0, float(h)};
    return addPlot<plot::StreamPlot>(axesIdx, std::move(gu),
                                     std::move(gv), plot::StreamConfig{});
}

uintptr_t matshow(uint32_t axesIdx, em::val data, uint32_t nrows,
                  uint32_t ncols) {
    return addPlot<plot::MatshowPlot>(axesIdx, f32vec(data), nrows, ncols);
}

uintptr_t pcolorfast(uint32_t axesIdx, em::val C, uint32_t nCols,
                     uint32_t nRows, double x0, double x1, double y0,
                     double y1) {
    return addPlot<plot::PcolorfastPlot>(axesIdx, f32vec(C), nCols, nRows,
        plot::Range{float(x0), float(x1)},
        plot::Range{float(y0), float(y1)});
}

/// mpl broken_barh: flat [xStart, xWidth, yStart, yHeight]* tuples.
uintptr_t brokenBarh(uint32_t axesIdx, em::val segs) {
    std::vector<float> f = f32vec(segs);
    std::vector<plot::BarHSegment> s(f.size() / 4);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = {f[i*4], f[i*4+1], f[i*4+2], f[i*4+3]};
    return addPlot<plot::BrokenBarHPlot>(axesIdx, std::move(s));
}

uintptr_t tricontour(uint32_t axesIdx, em::val xs, em::val ys, em::val zs) {
    return addPlot<plot::TriContourPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                         f32vec(zs));
}

/// mpl tricontourf — filled variant; defaults to viridis like contourf.
uintptr_t tricontourf(uint32_t axesIdx, em::val xs, em::val ys,
                      em::val zs) {
    plot::TriContourConfig cfg;
    cfg.cmap = &plot::Colormap::byName("viridis");
    return addPlot<plot::TriContourfPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                          f32vec(zs), std::move(cfg));
}

uintptr_t triplot(uint32_t axesIdx, em::val xs, em::val ys) {
    return addPlot<plot::TriplotPlot>(axesIdx, f32vec(xs), f32vec(ys));
}

uintptr_t specgram(uint32_t axesIdx, em::val signal, double fs) {
    plot::SpecgramConfig c; c.sampleRate = float(fs);
    return addPlot<plot::SpecgramPlot>(axesIdx, f32vec(signal), c);
}

uintptr_t spectrum(uint32_t axesIdx, em::val signal, double fs) {
    plot::SpectrumConfig c; c.sampleRate = float(fs);
    return addPlot<plot::SpectrumPlot>(axesIdx, f32vec(signal), c);
}

uintptr_t psd(uint32_t axesIdx, em::val signal, double fs) {
    plot::PsdConfig c; c.sampleRate = float(fs);
    return addPlot<plot::PsdPlot>(axesIdx, f32vec(signal), c);
}

uintptr_t csd(uint32_t axesIdx, em::val xs, em::val ys, double fs) {
    plot::CsdConfig c; c.sampleRate = float(fs);
    return addPlot<plot::CsdPlot>(axesIdx, f32vec(xs), f32vec(ys), c);
}

uintptr_t xcorr(uint32_t axesIdx, em::val xs, em::val ys) {
    return addPlot<plot::XCorrPlot>(axesIdx, f32vec(xs), f32vec(ys));
}

uintptr_t cohere(uint32_t axesIdx, em::val xs, em::val ys, double fs) {
    plot::CohereConfig c; c.sampleRate = float(fs);
    return addPlot<plot::CoherePlot>(axesIdx, f32vec(xs), f32vec(ys), c);
}

/// 3D wireframe: row-major height grid + viewInit camera.
uintptr_t wireframe(uint32_t axesIdx, em::val values, uint32_t w,
                    uint32_t h, double elevDeg, double azimDeg) {
    plot::Grid2D g;
    g.values = f32vec(values);
    g.width = w; g.height = h;
    g.xRange = {0, float(w)}; g.yRange = {0, float(h)};
    float vmin = std::numeric_limits<float>::max(),
          vmax = std::numeric_limits<float>::lowest();
    for (float v : g.values) {
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    auto cam = plot::Camera3D::viewInit(float(elevDeg), float(azimDeg));
    auto box = sync3DBox(axesIdx, elevDeg, azimDeg,
        {0, 0, vmin}, {float(w), float(h), vmax});
    cam.dataMin = box.first; cam.dataMax = box.second;
    auto plot = std::make_shared<plot::WireframePlot>(std::move(g));
    plot->setCamera(cam);
    auto* ax = targetAxes(axesIdx);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

/// mpl plot_trisurf: Delaunay-triangulated scattered (x,y,z).
uintptr_t trisurf(uint32_t axesIdx, em::val xs, em::val ys, em::val zs,
                  double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    auto cam = plot::Camera3D::viewInit(float(elevDeg), float(azimDeg));
    auto box = sync3DBox(axesIdx, elevDeg, azimDeg,
                         min3(x, y, z), max3(x, y, z));
    cam.dataMin = box.first; cam.dataMax = box.second;
    auto plot = std::make_shared<plot::TrisurfPlot>(
        std::move(x), std::move(y), std::move(z));
    plot->setCamera(cam);
    auto* ax = targetAxes(axesIdx);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

/// Shared registration for 3D plots: attach a viewInit camera, add,
/// return the raw handle.
// Per-axes union of 3D data extents — mpl's Axes3D shares one
// (4,4,3) box normalization across all 3D artists, so every new 3D
// plot widens the box on every existing 3D camera.
std::unordered_map<uint32_t, std::pair<plot::Point3D, plot::Point3D>>
    axes3DBox;

void expandBox(std::pair<plot::Point3D, plot::Point3D>& b,
               plot::Point3D lo, plot::Point3D hi) {
    auto mn = [](float a, float c) { return std::min(a, c); };
    auto mx = [](float a, float c) { return std::max(a, c); };
    b.first = {mn(b.first.x, lo.x), mn(b.first.y, lo.y),
               mn(b.first.z, lo.z)};
    b.second = {mx(b.second.x, hi.x), mx(b.second.y, hi.y),
                mx(b.second.z, hi.z)};
}

/// Expand the per-axes 3D box and (re)sync every 3D camera on it.
/// Also ensures an mpl-style Axes3D box plot exists behind the data.
/// Returns the shared extents.
std::pair<plot::Point3D, plot::Point3D>
sync3DBox(uint32_t axesIdx, double elevDeg, double azimDeg,
          plot::Point3D dataMin, plot::Point3D dataMax) {
    auto* ax = targetAxes(axesIdx);
    auto& box = axes3DBox[axesIdx];
    if (box.first.x == 0.0f && box.second.x == 0.0f &&
        box.first.y == 0.0f && box.second.y == 0.0f &&
        box.first.z == 0.0f && box.second.z == 0.0f) {
        // first 3D plot on this axes
        box = {dataMin, dataMax};
    } else {
        expandBox(box, dataMin, dataMax);
    }
    // mpl Axes3D draws a 3D box (panes, edges, tick labels) behind the
    // data — auto-add one per axes, tracking the shared data box.
    plot::Viewport vr;
    vr.x = {box.first.x, box.second.x};
    vr.y = {box.first.y, box.second.y};
    vr.z = {box.first.z, box.second.z};
    plot::Axes3DPlot* box3d = nullptr;
    for (auto& p : ax->plots())
        if (auto* b = dynamic_cast<plot::Axes3DPlot*>(p.get()))
            box3d = b;
    if (!box3d) {
        auto bp = std::make_shared<plot::Axes3DPlot>(
            plot::Camera3D::viewInit(float(elevDeg), float(azimDeg)), vr);
        bp->zorder = -10;
        box3d = static_cast<plot::Axes3DPlot*>(
            ax->addPlot(std::move(bp)));
    } else {
        box3d->setRange(vr);
    }
    // mpl (4,4,3) box normalization — shared extents across artists.
    for (auto& p : ax->plots())
        if (auto* c = p->camera3D()) {
            c->dataMin = box.first;
            c->dataMax = box.second;
        }
    return box;
}

template <typename P, typename... Args>
uintptr_t add3D(uint32_t axesIdx, double elevDeg, double azimDeg,
                plot::Point3D dataMin, plot::Point3D dataMax,
                Args&&... args) {
    auto plot = std::make_shared<P>(std::forward<Args>(args)...);
    plot->setCamera(
        plot::Camera3D::viewInit(float(elevDeg), float(azimDeg)));
    auto box = sync3DBox(axesIdx, elevDeg, azimDeg, dataMin, dataMax);
    plot->camera3D()->dataMin = box.first;
    plot->camera3D()->dataMax = box.second;
    auto* ax = targetAxes(axesIdx);
    auto* raw = plot.get();
    ax->addPlot(std::move(plot));
    S().figure.markStale();
    return reinterpret_cast<uintptr_t>(raw);
}

/// Min/max of three f32 arrays → camera box extents.
plot::Point3D min3(const std::vector<float>& a,
                   const std::vector<float>& b,
                   const std::vector<float>& c) {
    auto m = [](const std::vector<float>& v) {
        return v.empty() ? 0.0f
            : *std::ranges::min_element(v); };
    return {m(a), m(b), m(c)};
}
plot::Point3D max3(const std::vector<float>& a,
                   const std::vector<float>& b,
                   const std::vector<float>& c) {
    auto m = [](const std::vector<float>& v) {
        return v.empty() ? 0.0f
            : *std::ranges::max_element(v); };
    return {m(a), m(b), m(c)};
}

/// mpl Axes3D.plot — 3D line.
uintptr_t plot3d(uint32_t axesIdx, em::val xs, em::val ys, em::val zs,
                 double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    return add3D<plot::Plot3D>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(x, y, z),
        std::move(x), std::move(y), std::move(z), plot::Plot3DConfig{});
}

/// mpl Axes3D.scatter — 3D point cloud.
uintptr_t scatter3d(uint32_t axesIdx, em::val xs, em::val ys, em::val zs,
                    double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    return add3D<plot::Scatter3D>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(x, y, z),
        std::move(x), std::move(y), std::move(z),
        plot::Scatter3DConfig{});
}

/// mpl Axes3D.bar3d — x/y bases, z heights, dx/dy/dz sizes.
uintptr_t bar3d(uint32_t axesIdx, em::val xs, em::val ys, em::val zs,
                em::val dxs, em::val dys, em::val dzs,
                double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    auto dx = f32vec(dxs), dy = f32vec(dys), dz = f32vec(dzs);
    // bar tops = z + dz
    std::vector<float> zt(z.size());
    for (size_t i = 0; i < z.size(); ++i) zt[i] = z[i] + dz[i];
    return add3D<plot::Bar3D>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(x, y, zt),
        std::move(x), std::move(y), std::move(z),
        std::move(dx), std::move(dy), std::move(dz),
        plot::Bar3DConfig{});
}

/// mpl Axes3D.quiver — 3D vector field.
uintptr_t quiver3d(uint32_t axesIdx, em::val xs, em::val ys, em::val zs,
                   em::val us, em::val vs, em::val ws,
                   double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    auto u = f32vec(us), v = f32vec(vs), w = f32vec(ws);
    std::vector<float> xe(x.size()), ye(y.size()), ze(z.size());
    for (size_t i = 0; i < x.size(); ++i) {
        xe[i] = x[i] + u[i]; ye[i] = y[i] + v[i]; ze[i] = z[i] + w[i];
    }
    return add3D<plot::Quiver3D>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(xe, ye, ze),
        std::move(x), std::move(y), std::move(z),
        std::move(u), std::move(v), std::move(w),
        plot::Quiver3DConfig{});
}

/// mpl Axes3D.errorbar — points with optional z-error bars (via config).
uintptr_t errorbar3d(uint32_t axesIdx, em::val xs, em::val ys, em::val zs,
                     em::val zerr, double elevDeg, double azimDeg) {
    plot::Errorbar3DConfig cfg;
    cfg.zerrLower = cfg.zerrUpper = f32vec(zerr);
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    std::vector<float> zt(z.size());
    for (size_t i = 0; i < z.size(); ++i)
        zt[i] = z[i] + cfg.zerrUpper[i];
    return add3D<plot::Errorbar3D>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(x, y, zt),
        std::move(x), std::move(y), std::move(z), std::move(cfg));
}

/// mpl Axes3D.contour / contourf on a z=const projected surface.
uintptr_t contour3d(uint32_t axesIdx, em::val values, uint32_t w,
                    uint32_t h, bool filled, double elevDeg,
                    double azimDeg) {
    plot::Grid2D g;
    g.values = f32vec(values);
    g.width = w; g.height = h;
    g.xRange = {0, float(w)}; g.yRange = {0, float(h)};
    // Enable the mpl (4,4,3) box normalization: project3D collapses
    // everything to a constant point if the data sits far from the
    // orbit target. Same box extents mpl uses for a grid plot.
    float vmin = std::numeric_limits<float>::max(),
          vmax = std::numeric_limits<float>::lowest();
    for (float v : g.values) {
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    auto box = sync3DBox(axesIdx, elevDeg, azimDeg,
        {g.xRange.min, g.yRange.min, vmin},
        {g.xRange.max, g.yRange.max, vmax});
    auto mk = [&](auto P) {
        auto* ax = targetAxes(axesIdx);
        auto plot = std::make_shared<decltype(P)>(std::move(P));
        plot->setCamera(plot::Camera3D::viewInit(float(elevDeg),
                                               float(azimDeg)));
        plot->camera3D()->dataMin = box.first;
        plot->camera3D()->dataMax = box.second;
        auto* raw = plot.get();
        ax->addPlot(std::move(plot));
        S().figure.markStale();
        return reinterpret_cast<uintptr_t>(raw);
    };
    // mpl 3D contour/contourf (offset=None) draw each band/line at its
    // own level z and color by the cmap.
    plot::Contour3DConfig cfg;
    cfg.cmap = &plot::Colormap::byName("viridis");
    cfg.levelsAsZ = true;
    if (filled)
        return mk(plot::Contourf3D(std::move(g), std::move(cfg)));
    return mk(plot::Contour3D(std::move(g), std::move(cfg)));
}

/// mpl Axes3D.voxels — binary occupancy grid.
uintptr_t voxels(uint32_t axesIdx, em::val filled, uint32_t nx,
                 uint32_t ny, uint32_t nz, double elevDeg,
                 double azimDeg) {
    std::vector<uint8_t> f;
    const size_t n = filled["length"].as<size_t>();
    f.resize(n);
    em::val mem = em::val(em::memory_view<uint8_t>(n, f.data()));
    mem.call<void>("set", filled);
    return add3D<plot::VoxelsPlot>(axesIdx, elevDeg, azimDeg,
        plot::Point3D{0, 0, 0},
        plot::Point3D{float(nx), float(ny), float(nz)},
        std::move(f), nx, ny, nz, plot::VoxelsConfig{});
}

/// mpl ax.text with 3D coords (single item convenience).
uintptr_t text3d(uint32_t axesIdx, double x, double y, double z,
                 const std::string& txt, double elevDeg,
                 double azimDeg) {
    return add3D<plot::Text3D>(axesIdx, elevDeg, azimDeg,
        plot::Point3D{float(x), float(y), float(z)},
        plot::Point3D{float(x) + 1.0f, float(y) + 1.0f, float(z) + 1.0f},
        float(x), float(y), float(z), txt);
}

/// mpl barbs — wind barbs on a regular grid.
uintptr_t barbs(uint32_t axesIdx, em::val xs, em::val ys,
                em::val us, em::val vs) {
    return addPlot<plot::BarbsPlot>(axesIdx, f32vec(xs), f32vec(ys),
                                    f32vec(us), f32vec(vs),
                                    plot::BarbsConfig{});
}

/// mpl grouped bars — `heights` is a JS array of Float32Arrays
/// (one per series).
uintptr_t groupedBar(uint32_t axesIdx, em::val heights) {
    std::vector<std::vector<float>> hs;
    const size_t n = heights["length"].as<size_t>();
    hs.reserve(n);
    for (size_t i = 0; i < n; ++i) hs.push_back(f32vec(heights[i]));
    return addPlot<plot::GroupedBarPlot>(axesIdx, std::move(hs));
}

/// mpl figimage — RGBA8 pixels (uint32 per px) overlaid on the figure.
uintptr_t figimage(uint32_t axesIdx, em::val pixels, uint32_t w,
                   uint32_t h) {
    std::vector<uint32_t> px;
    const size_t n = pixels["length"].as<size_t>();
    px.resize(n);
    em::val mem = em::val(em::memory_view<uint32_t>(n, px.data()));
    mem.call<void>("set", pixels);
    return addPlot<plot::FigImagePlot>(axesIdx, std::move(px), w, h);
}

/// Chirp signal demo (f0→f1 over duration, xRange in seconds).
uintptr_t chirp(uint32_t axesIdx, double f0, double f1, double duration,
                double xMax) {
    return addPlot<plot::ChirpPlot>(axesIdx, f0, f1, duration,
                                    plot::Range{0.f, float(xMax)});
}

/// Mexican-hat (Ricker) 2D surface on a sigma-scaled grid.
uintptr_t mexicanHat(uint32_t axesIdx, double sigma, double range,
                     double elevDeg, double azimDeg) {
    return add3D<plot::MexicanHatPlot>(axesIdx, elevDeg, azimDeg,
        plot::Point3D{-float(range), -float(range), -0.25f},
        plot::Point3D{float(range), float(range), 1.0f},
        float(sigma), plot::Range{-float(range), float(range)},
        plot::Range{-float(range), float(range)});
}

/// mpl bar_label — value labels on bar tops.
uintptr_t barLabel(uint32_t axesIdx, em::val xs, em::val heights,
                   double baseline) {
    return addPlot<plot::BarLabelPlot>(axesIdx, f32vec(xs),
                                       f32vec(heights), float(baseline));
}

/// mpl Axes3D.tricontour — contour lines on scattered 3D data.
uintptr_t tricontour3d(uint32_t axesIdx, em::val xs, em::val ys,
                       em::val zs, double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    plot::TricontourConfig cfg;
    cfg.cmap = &plot::Colormap::byName("viridis");
    cfg.levelsAsZ = true;
    return add3D<plot::TricontourPlot>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(x, y, z),
        std::move(x), std::move(y), std::move(z), std::move(cfg));
}

/// mpl Axes3D.tricontourf — filled contours on scattered 3D data.
uintptr_t tricontourf3d(uint32_t axesIdx, em::val xs, em::val ys,
                        em::val zs, double elevDeg, double azimDeg) {
    auto x = f32vec(xs), y = f32vec(ys), z = f32vec(zs);
    plot::TricontourConfig cfg;
    cfg.cmap = &plot::Colormap::byName("viridis");
    cfg.levelsAsZ = true;
    return add3D<plot::TricontourfPlot>(axesIdx, elevDeg, azimDeg,
        min3(x, y, z), max3(x, y, z),
        std::move(x), std::move(y), std::move(z), std::move(cfg));
}

/// Orientation indicator overlay (Blender/Paraview-style axis triad).
/// corner: 0=UL 1=UR 2=LL 3=LR; mode: 0=triad 1=cube.
uintptr_t navcube(uint32_t axesIdx, double elevDeg, double azimDeg,
                  uint32_t corner, uint32_t mode) {
    plot::NavCubeConfig cfg;
    cfg.camera = plot::Camera3D::viewInit(float(elevDeg), float(azimDeg));
    cfg.corner = plot::NavCubeCorner(corner & 3);
    cfg.mode = mode ? plot::NavCubeMode::Cube : plot::NavCubeMode::Triad;
    return addPlot<plot::NavCubePlot>(axesIdx, std::move(cfg));
}

/// mpl quiverkey — reference arrow + label for a quiver plot.
/// `quiverHandle` is the uintptr_t returned by quiver() (0 = unscaled).
uintptr_t quiverkey(uint32_t axesIdx, double x, double y, double u,
                    uintptr_t quiverHandle, const std::string& label) {
    const plot::QuiverPlot* ref = nullptr;
    if (quiverHandle)
        ref = dynamic_cast<const plot::QuiverPlot*>(
            reinterpret_cast<const plot::IPlot*>(quiverHandle));
    plot::QuiverKeyPlot::Config cfg;
    cfg.label = label;
    return addPlot<plot::QuiverKeyPlot>(axesIdx, float(x), float(y),
                                        float(u), ref, std::move(cfg));
}

// ── reference lines / spans / annotations ───────────────────────────
plot::Color colorOr(em::val v, plot::Color def) {
    if (v.typeOf().as<std::string>() == "string")
        if (auto c = plot::Color::parse(v.as<std::string>())) return *c;
    return def;
}
void axhline(uint32_t a, double y, em::val color, double width) {
    targetAxes(a)->axhline(float(y), colorOr(color, plot::Color::black()),
                           float(width));
    S().figure.markStale();
}
void axvline(uint32_t a, double x, em::val color, double width) {
    targetAxes(a)->axvline(float(x), colorOr(color, plot::Color::black()),
                           float(width));
    S().figure.markStale();
}
void axhspan(uint32_t a, double y1, double y2, em::val color) {
    targetAxes(a)->axhspan(float(y1), float(y2),
        colorOr(color, plot::Color::fromRgba8(200, 200, 200, 128)));
    S().figure.markStale();
}
void axvspan(uint32_t a, double x1, double x2, em::val color) {
    targetAxes(a)->axvspan(float(x1), float(x2),
        colorOr(color, plot::Color::fromRgba8(200, 200, 200, 128)));
    S().figure.markStale();
}
void hlines(uint32_t a, em::val ys, double xMin, double xMax,
            em::val color, double width) {
    targetAxes(a)->hlines(f32vec(ys), float(xMin), float(xMax),
                          colorOr(color, plot::Color::black()),
                          float(width));
    S().figure.markStale();
}
void vlines(uint32_t a, em::val xs, double yMin, double yMax,
            em::val color, double width) {
    targetAxes(a)->vlines(f32vec(xs), float(yMin), float(yMax),
                          colorOr(color, plot::Color::black()),
                          float(width));
    S().figure.markStale();
}
/// mpl ax.legend(loc) — enables the legend box.
void legend(uint32_t a, const std::string& loc) {
    auto& l = targetAxes(a)->legend();
    if (!loc.empty()) l.location = loc;
    S().figure.markStale();
}
/// mpl fig.colorbar() — enables the per-axes colorbar strip.
void colorbar(uint32_t a) {
    targetAxes(a)->style().colorbar.visible = true;
    S().figure.markStale();
}
/// mpl ax.text — coords: 0=data, 1=axes-frac, 2=figure-frac.
void axesText(uint32_t a, double x, double y, const std::string& txt,
          uint32_t coords) {
    static constexpr plot::CoordSystem cs[] = {
        plot::CoordSystem::Data, plot::CoordSystem::Axes,
        plot::CoordSystem::Figure, plot::CoordSystem::Display};
    targetAxes(a)->text(float(x), float(y), txt,
                        cs[std::min(coords, 3u)]);
    S().figure.markStale();
}

// ── axes/figure styling ─────────────────────────────────────────────
void setXlim(uint32_t a, double lo, double hi) {
    targetAxes(a)->setXlim(float(lo), float(hi)); S().figure.markStale();
}
void setYlim(uint32_t a, double lo, double hi) {
    targetAxes(a)->setYlim(float(lo), float(hi)); S().figure.markStale();
}
void setXscale(uint32_t a, const std::string& name) {
    targetAxes(a)->setXscale(name); S().figure.markStale();
}
void setYscale(uint32_t a, const std::string& name) {
    targetAxes(a)->setYscale(name); S().figure.markStale();
}
void setTitle(uint32_t a, const std::string& t) {
    targetAxes(a)->setTitle(t); S().figure.markStale();
}
void setXlabel(uint32_t a, const std::string& t) {
    targetAxes(a)->style().xAxis.label = t; S().figure.markStale();
}
void setYlabel(uint32_t a, const std::string& t) {
    targetAxes(a)->style().yAxis.label = t; S().figure.markStale();
}
void setGrid(uint32_t a, bool on) {
    auto& st = targetAxes(a)->style();
    st.xAxis.grid = st.yAxis.grid = on; S().figure.markStale();
}
void suptitle(const std::string& t) { S().figure.suptitle(t); }

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
    em::function("_vp_setInteractive", &setInteractive);
    em::function("_vp_dispatch", &dispatchEvent);
    em::function("_vp_violin", &violin);
    em::function("_vp_stackplot", &stackplot);
    em::function("_vp_fill", &fill);
    em::function("_vp_spy", &spy);
    em::function("_vp_tripcolor", &tripcolor);
    em::function("_vp_streamplot", &streamplot);
    em::function("_vp_xlim", &setXlim);
    em::function("_vp_ylim", &setYlim);
    em::function("_vp_xscale", &setXscale);
    em::function("_vp_yscale", &setYscale);
    em::function("_vp_title", &setTitle);
    em::function("_vp_xlabel", &setXlabel);
    em::function("_vp_ylabel", &setYlabel);
    em::function("_vp_grid", &setGrid);
    em::function("_vp_suptitle", &suptitle);
    em::function("_vp_matshow", &matshow);
    em::function("_vp_pcolorfast", &pcolorfast);
    em::function("_vp_brokenBarh", &brokenBarh);
    em::function("_vp_tricontour", &tricontour);
    em::function("_vp_tricontourf", &tricontourf);
    em::function("_vp_triplot", &triplot);
    em::function("_vp_specgram", &specgram);
    em::function("_vp_spectrum", &spectrum);
    em::function("_vp_psd", &psd);
    em::function("_vp_csd", &csd);
    em::function("_vp_xcorr", &xcorr);
    em::function("_vp_cohere", &cohere);
    em::function("_vp_wireframe", &wireframe);
    em::function("_vp_trisurf", &trisurf);
    em::function("_vp_axhline", &axhline);
    em::function("_vp_axvline", &axvline);
    em::function("_vp_axhspan", &axhspan);
    em::function("_vp_axvspan", &axvspan);
    em::function("_vp_hlines", &hlines);
    em::function("_vp_vlines", &vlines);
    em::function("_vp_legend", &legend);
    em::function("_vp_colorbar", &colorbar);
    em::function("_vp_text", &axesText);
    em::function("_vp_plot3d", &plot3d);
    em::function("_vp_scatter3d", &scatter3d);
    em::function("_vp_bar3d", &bar3d);
    em::function("_vp_quiver3d", &quiver3d);
    em::function("_vp_errorbar3d", &errorbar3d);
    em::function("_vp_contour3d", &contour3d);
    em::function("_vp_voxels", &voxels);
    em::function("_vp_text3d", &text3d);
    em::function("_vp_barbs", &barbs);
    em::function("_vp_groupedBar", &groupedBar);
    em::function("_vp_figimage", &figimage);
    em::function("_vp_chirp", &chirp);
    em::function("_vp_mexicanHat", &mexicanHat);
    em::function("_vp_barLabel", &barLabel);
    em::function("_vp_tricontour3d", &tricontour3d);
    em::function("_vp_tricontourf3d", &tricontourf3d);
    em::function("_vp_navcube", &navcube);
    em::function("_vp_quiverkey", &quiverkey);
    em::function("_vp_mailboxDest",
        +[](uint32_t slot, uint32_t bytes) -> uintptr_t {
            return S().backend.opGpu().mailboxDest(slot, bytes);
        });
    em::function("_vp_mailboxDone",
        +[](uint32_t slot) { S().backend.opGpu().mailboxDone(slot); });
}
#endif // __EMSCRIPTEN__
