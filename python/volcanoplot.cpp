// python/volcanoplot.cpp — pybind11 bindings for VolcanoPlot
//
// Minimal matplotlib-style surface:
//   import volcanoplot as vp
//   fig = vp.Figure(800, 600, dpi=100)
//   ax = fig.add_axes()
//   ax.plot([0,1,2], [0,1,0], color="C0", label="line")
//   ax.scatter([0,1], [1,0], s=20)
//   fig.savefig("out.png")
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Animation.hpp>
#include <volcano/plot/Collections.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Rc.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>
#include <volcano/plot/plots/HistPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/StemPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/BoxPlot.hpp>
#include <volcano/plot/plots/PcolormeshPlot.hpp>
#include <volcano/plot/plots/PiePlot.hpp>
#include <volcano/plot/plots/StackPlot.hpp>
#include <volcano/plot/plots/HexbinPlot.hpp>
#include <volcano/plot/plots/QuiverPlot.hpp>
#include <volcano/plot/plots/StreamPlot.hpp>
#include <volcano/plot/plots/ViolinPlot.hpp>
#include <volcano/plot/plots/Hist2DPlot.hpp>
#include <volcano/plot/plots/SpyPlot.hpp>
#include <volcano/plot/plots/MatshowPlot.hpp>
#include <volcano/plot/plots/PsdPlot.hpp>
#include <volcano/plot/plots/CsdPlot.hpp>
#include <volcano/plot/plots/SpecgramPlot.hpp>
#include <volcano/plot/plots/CoherePlot.hpp>
#include <volcano/plot/plots/XCorrPlot.hpp>
#include <volcano/plot/plots/ECDFPlot.hpp>
#include <volcano/plot/plots/FillPlot.hpp>
#include <volcano/plot/plots/TriplotPlot.hpp>
#include <volcano/plot/plots/TripcolorPlot.hpp>
#include <volcano/plot/plots/TriContourPlot.hpp>
#include <volcano/plot/plots/SpectrumPlot.hpp>
#include <volcano/plot/plots/ReferenceLines.hpp>
#include <volcano/plot/Triangulation.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/plots/Plot3D.hpp>
#include <volcano/plot/plots/Scatter3D.hpp>
#include <volcano/plot/plots/SurfacePlot.hpp>
#include <volcano/plot/plots/WireframePlot.hpp>
#include <volcano/plot/plots/Bar3D.hpp>
#include <volcano/plot/plots/VoxelsPlot.hpp>
#include <volcano/plot/plots/TrisurfPlot.hpp>
#include <volcano/plot/plots/Quiver3D.hpp>
#include <volcano/plot/plots/Axes3DPlot.hpp>
#include <volcano/plot/plots/Contour3D.hpp>
#include <volcano/plot/plots/BarbsPlot.hpp>
#include <volcano/plot/plots/BrokenBarHPlot.hpp>
#include <volcano/plot/plots/BarLabelPlot.hpp>
#include <volcano/plot/plots/FigImagePlot.hpp>
#include <volcano/plot/plots/PcolorfastPlot.hpp>
#include <volcano/plot/plots/Errorbar3D.hpp>
#include <volcano/plot/plots/Text3D.hpp>
#include <volcano/plot/plots/NavCubePlot.hpp>
#include <volcano/plot/Specialized.hpp>
#include <volcano/plot/Dates.hpp>
#include <volcano/plot/Widgets.hpp>
#include <volcano/plot/Interaction.hpp>
#include <volcano/plot/Events.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <numeric>
#include <optional>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace py = pybind11;
using namespace volcano;

namespace {

plot::Color parseColor(const py::object& c) {
    if (c.is_none()) return plot::Series2D::autoColor();
    if (py::isinstance<py::str>(c)) {
        auto s = c.cast<std::string>();
        if (auto p = plot::Color::parse(s)) return *p;
        throw std::invalid_argument("unknown color '" + s + "'");
    }
    if (py::isinstance<py::sequence>(c)) {
        auto seq = c.cast<std::vector<float>>();
        if (seq.size() < 3 || seq.size() > 4)
            throw std::invalid_argument("color tuple must be (r,g,b[,a])");
        auto to8 = [](float v) {
            return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        return plot::Color::fromRgba8(to8(seq[0]), to8(seq[1]), to8(seq[2]),
                                      seq.size() == 4 ? to8(seq[3]) : 255);
    }
    throw std::invalid_argument("color must be a string or (r,g,b[,a])");
}

/// Convert any array-like (list, tuple, numpy array, pandas Series/Index,
/// xarray DataArray) to a float vector. datetime64/timedelta64 numpy
/// dtypes are converted to days-since-epoch floats (mpl date2num) and
/// `*isDate` is set so the caller can install the date converter.
std::vector<float> toFloats(const py::object& obj, bool* isDate = nullptr) {
    if (isDate) *isDate = false;
    if (py::isinstance<py::array>(obj)) {
        auto arr = py::reinterpret_borrow<py::array>(obj);
        char kind = arr.dtype().kind();
        if (kind == 'M' || kind == 'm') {  // datetime64 / timedelta64
            // Normalize to int64 counts of the stored unit, then scale to
            // days. Units: as=1e-18 … D=86400 s.
            static const std::unordered_map<std::string, double> kToDays = {
                {"D", 1.0},  {"h", 1.0/24},  {"m", 1.0/1440},
                {"s", 1.0/86400}, {"ms", 1e-3/86400}, {"us", 1e-6/86400},
                {"ns", 1e-9/86400}, {"ps", 1e-12/86400},
            };
            std::string unit = py::str(arr.dtype()).cast<std::string>();
            // "datetime64[ns]" / "timedelta64[ms]" → unit inside brackets
            auto lb = unit.find('['), rb = unit.find(']');
            std::string u = (lb != std::string::npos && rb != std::string::npos)
                                ? unit.substr(lb + 1, rb - lb - 1)
                                : "s";
            auto it = kToDays.find(u);
            double scale = it != kToDays.end() ? it->second : 1.0 / 86400;
            py::array_t<int64_t> raw =
                arr.attr("view")(py::str("int64")).cast<py::array_t<int64_t>>();
            auto r = raw.unchecked<1>();
            std::vector<float> out(size_t(r.shape(0)));
            for (py::ssize_t i = 0; i < r.shape(0); ++i)
                out[size_t(i)] = float(double(r(i)) * scale);
            if (isDate) *isDate = (kind == 'M');
            return out;
        }
        // Numeric ndarray — forcecast handles int/float dtypes and
        // non-contiguous layouts.
        py::array_t<float, py::array::forcecast | py::array::c_style> f =
            py::array_t<float, py::array::forcecast | py::array::c_style>::ensure(obj);
        if (!f) throw std::invalid_argument("array is not numeric");
        auto r = f.unchecked<1>();
        std::vector<float> out(size_t(r.shape(0)));
        for (py::ssize_t i = 0; i < r.shape(0); ++i) out[size_t(i)] = r(i);
        return out;
    }
    // pandas/xarray expose .values (numpy) — prefer the fast path even
    // when the object itself is also a sequence (DatetimeIndex, Series).
    if (py::hasattr(obj, "values")) {
        py::object vals = obj.attr("values");
        if (py::isinstance<py::array>(vals)) return toFloats(vals, isDate);
    }
    return obj.cast<std::vector<float>>();
}

/// toFloats that also accepts a bare scalar (mpl hlines/vlines y/x).
std::vector<float> toFloatList(const py::object& obj) {
    if (py::isinstance<py::float_>(obj) || py::isinstance<py::int_>(obj))
        return {obj.cast<float>()};
    return toFloats(obj);
}

/// parseColor + mpl default: None → black (ref-line helpers don't
/// consume the prop cycle in volcano).
plot::Color lineColor(const py::object& c, plot::Color fallback) {
    auto col = parseColor(c);
    return col.a > 0 ? col : fallback;
}

/// 2D array-like → (row-major data, (nrows, ncols)). 1D input → 1×N.
std::pair<std::vector<float>, std::pair<uint32_t, uint32_t>>
flat2d(const py::object& obj) {
    if (py::isinstance<py::array>(obj)) {
        auto arr = py::reinterpret_borrow<py::array>(obj);
        if (arr.ndim() == 2) {
            py::array_t<float, py::array::forcecast | py::array::c_style> f =
                py::array_t<float,
                            py::array::forcecast | py::array::c_style>::ensure(
                    obj);
            auto r = f.unchecked<2>();
            uint32_t rows = uint32_t(r.shape(0)), cols = uint32_t(r.shape(1));
            std::vector<float> out(size_t(rows) * cols);
            for (py::ssize_t j = 0; j < r.shape(0); ++j)
                for (py::ssize_t i = 0; i < r.shape(1); ++i)
                    out[size_t(j) * cols + size_t(i)] = r(j, i);
            return {std::move(out), {rows, cols}};
        }
    }
    py::sequence s = obj.cast<py::sequence>();
    if (s.size() > 0 &&
        (py::isinstance<py::sequence>(s[0]) ||
         py::isinstance<py::array>(s[0]))) {
        std::vector<float> out;
        uint32_t cols = 0;
        for (auto row : s) {
            auto rv = toFloats(py::reinterpret_borrow<py::object>(row));
            if (cols == 0) cols = uint32_t(rv.size());
            if (rv.size() != cols)
                throw std::invalid_argument("ragged 2D array");
            out.insert(out.end(), rv.begin(), rv.end());
        }
        return {std::move(out), {uint32_t(s.size()), cols}};
    }
    auto v = toFloats(obj);
    return {std::move(v), {1, uint32_t(v.size())}};
}

/// Sequence of color specs (or None) → Color vector.
std::vector<plot::Color> toColors(const py::object& obj,
                                  plot::Color fallback) {
    std::vector<plot::Color> out;
    if (obj.is_none()) return out;
    for (auto c : obj)
        out.push_back(
            lineColor(py::reinterpret_borrow<py::object>(c), fallback));
    return out;
}

/// List-of-lists → rows; flat sequence → single row (mpl accepts both).
std::vector<std::vector<float>> toRows(const py::object& obj) {
    py::sequence s = obj.cast<py::sequence>();
    if (s.size() > 0 &&
        (py::isinstance<py::sequence>(s[0]) ||
         py::isinstance<py::array>(s[0]))) {
        std::vector<std::vector<float>> rows;
        for (auto r : s)
            rows.push_back(toFloats(py::reinterpret_borrow<py::object>(r)));
        return rows;
    }
    return {toFloats(obj)};
}

/// Sequence of (i, j, k) index triples → Triangle vector.
std::vector<plot::Triangle> toTriangles(const py::object& obj) {
    std::vector<plot::Triangle> out;
    if (obj.is_none()) return out;
    for (auto t : obj) {
        auto tri =
            py::reinterpret_borrow<py::object>(t)
                .cast<std::tuple<uint32_t, uint32_t, uint32_t>>();
        out.push_back({std::get<0>(tri), std::get<1>(tri), std::get<2>(tri)});
    }
    return out;
}

/// FFT window name → config Window enum (shared names across configs).
template <class W>
W windowByName(const std::string& n) {
    if (n == "hamming") return W::Hamming;
    if (n == "blackman" || n == "blackmanharris") return W::Blackman;
    if (n == "rectangular" || n == "none" || n == "boxcar")
        return W::Rectangular;
    return W::Hann;
}

/// Categorical x: true when the input is a sequence of strings.
bool isStringSeq(const py::object& obj) {
    if (!py::isinstance<py::sequence>(obj) || py::isinstance<py::str>(obj))
        return false;
    py::sequence seq = py::reinterpret_borrow<py::sequence>(obj);
    if (seq.size() == 0) return false;
    return py::isinstance<py::str>(seq[0]);
}

/// 2D array-like (nested sequence or numpy 2D) → Grid2D.
plot::Grid2D toGrid2D(const py::object& obj) {
    if (py::isinstance<py::array>(obj)) {
        py::array_t<float, py::array::forcecast | py::array::c_style> f =
            py::array_t<float, py::array::forcecast | py::array::c_style>::ensure(obj);
        if (!f || f.ndim() != 2)
            throw std::invalid_argument("expected a 2D array");
        auto r = f.unchecked<2>();
        plot::Grid2D g;
        g.height = uint32_t(r.shape(0));
        g.width = uint32_t(r.shape(1));
        g.xRange = {0.0f, float(g.width)};
        g.yRange = {0.0f, float(g.height)};
        g.values.resize(size_t(g.width) * g.height);
        for (py::ssize_t j = 0; j < r.shape(0); ++j)
            for (py::ssize_t i = 0; i < r.shape(1); ++i)
                g.values[size_t(j) * g.width + size_t(i)] = r(j, i);
        return g;
    }
    auto rows = obj.cast<std::vector<std::vector<float>>>();
    if (rows.empty() || rows[0].empty())
        throw std::invalid_argument("expected a non-empty 2D array");
    plot::Grid2D g;
    g.height = uint32_t(rows.size());
    g.width = uint32_t(rows[0].size());
    g.xRange = {0.0f, float(g.width)};
    g.yRange = {0.0f, float(g.height)};
    g.values.reserve(size_t(g.width) * g.height);
    for (auto& row : rows) {
        if (row.size() != g.width)
            throw std::invalid_argument("ragged 2D array");
        g.values.insert(g.values.end(), row.begin(), row.end());
    }
    return g;
}

const plot::Colormap& cmapByName(const std::string& name) {
    try { return plot::Colormap::byName(name); }
    catch (...) { return plot::colormaps::viridis(); }
}

/// mpl figimage input: (H,W) scalars → cmap-mapped RGBA8, or
/// (H,W,3|4) uint8/float RGB(A) → RGBA8 packed 0xAABBGGRR.
struct RgbaImage { std::vector<uint32_t> px; uint32_t w, h; };
RgbaImage toRgbaImage(const py::object& obj, const std::string& cmap,
                      const py::object& vmin, const py::object& vmax) {
    py::array_t<float, py::array::forcecast | py::array::c_style> arr =
        py::array_t<float,
                    py::array::forcecast | py::array::c_style>::ensure(obj);
    if (!arr)
        throw std::invalid_argument("figimage expects an array-like");
    RgbaImage out;
    if (arr.ndim() == 2) {
        // Scalar field → colormap with mpl vmin/vmax normalization.
        auto r = arr.unchecked<2>();
        out.h = uint32_t(r.shape(0)); out.w = uint32_t(r.shape(1));
        float lo = std::numeric_limits<float>::max(),
              hi = std::numeric_limits<float>::lowest();
        for (py::ssize_t j = 0; j < r.shape(0); ++j)
            for (py::ssize_t i = 0; i < r.shape(1); ++i) {
                float v = r(j, i);
                if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
        if (!vmin.is_none()) lo = vmin.cast<float>();
        if (!vmax.is_none()) hi = vmax.cast<float>();
        float span = hi - lo; if (span == 0.0f) span = 1.0f;
        const auto& cm = cmapByName(cmap);
        out.px.resize(size_t(out.w) * out.h);
        auto to8 = [](float v) {
            return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        for (py::ssize_t j = 0; j < r.shape(0); ++j)
            for (py::ssize_t i = 0; i < r.shape(1); ++i) {
                float t = (r(j, i) - lo) / span;
                auto c = cm.sample(std::clamp(t, 0.0f, 1.0f));
                out.px[size_t(j) * out.w + size_t(i)] =
                    uint32_t(to8(c.r)) | (uint32_t(to8(c.g)) << 8) |
                    (uint32_t(to8(c.b)) << 16) | (uint32_t(to8(c.a)) << 24);
            }
        return out;
    }
    if (arr.ndim() == 3) {
        auto ch = arr.shape(2);
        if (ch != 3 && ch != 4)
            throw std::invalid_argument("image must be (H,W) or (H,W,3|4)");
        auto r = arr.unchecked<3>();
        // uint8 inputs arrive as 0..255 after forcecast; mpl floats are
        // 0..1 — detect by the source dtype kind.
        bool bytes = py::isinstance<py::array>(obj) &&
                     (obj.cast<py::array>().dtype().kind() == 'u' ||
                      obj.cast<py::array>().dtype().kind() == 'i');
        float k = bytes ? 1.0f / 255.0f : 1.0f;
        out.h = uint32_t(r.shape(0)); out.w = uint32_t(r.shape(1));
        out.px.resize(size_t(out.w) * out.h);
        auto ch8 = [&](float v) {
            return uint8_t(std::clamp(v * k, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        for (py::ssize_t j = 0; j < r.shape(0); ++j)
            for (py::ssize_t i = 0; i < r.shape(1); ++i) {
                uint8_t R = ch8(r(j, i, 0)), G = ch8(r(j, i, 1)),
                        B = ch8(r(j, i, 2));
                uint8_t A = ch == 4 ? ch8(r(j, i, 3)) : 255;
                out.px[size_t(j) * out.w + size_t(i)] =
                    uint32_t(R) | (uint32_t(G) << 8) |
                    (uint32_t(B) << 16) | (uint32_t(A) << 24);
            }
        return out;
    }
    throw std::invalid_argument("figimage expects a 2D or 3D array");
}

plot::Series2D makeSeries(const std::vector<float>& x,
                          const std::vector<float>& y) {
    if (x.size() != y.size())
        throw std::invalid_argument("x and y must have the same length");
    plot::Series2D s;
    s.points.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++i) s.points.push_back({x[i], y[i]});
    return s;
}

/// mpl fmt string "[marker][line][color]" in any order ("ro--", "g^",
/// "b:"). Linestyle tokens are extracted longest-first; every remaining
/// character must be a marker or a single-letter color (each at most
/// once in mpl — here last wins). Throws on unrecognized characters.
void applyFmt(plot::Series2D& s, const std::string& fmt) {
    static const std::array<std::string_view, 4> kLineTokens = {
        "--", "-.", "-", ":"};
    std::string rest = fmt;
    bool hasLine = false, hasMarker = false;
    for (auto tok : kLineTokens) {
        if (auto p = rest.find(tok); p != std::string::npos) {
            s.lineStyle = *plot::lineStyleFromString(tok);
            hasLine = true;
            rest.erase(p, tok.size());
            break;
        }
    }
    for (char c : rest) {
        if (auto m = plot::markerFromChar(c)) {
            s.marker = *m; hasMarker = true; continue;
        }
        char cs[2] = {c, '\0'};
        if (auto col = plot::Color::parse(cs)) { s.color = *col; continue; }
        throw std::invalid_argument(
            std::format("unrecognized format string '{}'", fmt));
    }
    // mpl: a marker in fmt without a line token means markers-only
    // (plot(x, y, 'o') draws no connecting line).
    if (hasMarker && !hasLine)
        s.lineStyle = plot::LineStyle::None;
}

/// mpl text coordinate names → CoordSystem ("data", "axes fraction",
/// "figure fraction", "offset points", "display").
plot::CoordSystem toCoordSystem(const std::string& name) {
    if (name == "data") return plot::CoordSystem::Data;
    if (name == "axes fraction" || name == "axes points")
        return plot::CoordSystem::Axes;
    if (name == "figure fraction" || name == "figure points")
        return plot::CoordSystem::Figure;
    if (name == "offset points")
        return plot::CoordSystem::OffsetPoints;
    if (name == "display" || name == "figure pixels")
        return plot::CoordSystem::Display;
    throw std::invalid_argument(
        std::format("unknown coordinate system '{}'", name));
}

/// Min/max of three parallel arrays → 3D data Viewport.
plot::Viewport range3(const std::vector<float>& x,
                      const std::vector<float>& y,
                      const std::vector<float>& z) {
    plot::Viewport v;
    v.x = {std::numeric_limits<float>::max(),
           std::numeric_limits<float>::lowest()};
    v.y = v.z = v.x;
    auto acc = [](plot::Range& r, const std::vector<float>& a) {
        for (float f : a) {
            if (!std::isfinite(f)) continue;
            r.min = std::min(r.min, f); r.max = std::max(r.max, f);
        }
    };
    acc(v.x, x); acc(v.y, y); acc(v.z, z);
    for (auto* r : {&v.x, &v.y, &v.z})
        if (r->min > r->max) *r = {0, 1};
    return v;
}

/// Min/max over a Grid2D (x/y ranges + z from values).
plot::Viewport gridRange3(const plot::Grid2D& g) {
    plot::Viewport v;
    v.x = g.xRange; v.y = g.yRange;
    v.z = {std::numeric_limits<float>::max(),
           std::numeric_limits<float>::lowest()};
    for (float f : g.values) {
        if (!std::isfinite(f)) continue;
        v.z.min = std::min(v.z.min, f); v.z.max = std::max(v.z.max, f);
    }
    if (v.z.min > v.z.max) v.z = {0, 1};
    return v;
}

/// Owns the headless backend + renderer + figure.
class PyFigure {
public:
    PyFigure(uint32_t w, uint32_t h, float dpi) {
        backend::BackendDesc desc;
        desc.width = w;
        desc.height = h;
        desc.samples = vk::SampleCountFlagBits::e4;
        backend_ = backend::createHeadlessBackend(desc);
        renderer_ = std::make_unique<render::Renderer>(*backend_);
        figure_.style().dpi = dpi;
    }

    plot::Axes* addAxes() { return figure_.addAxes(0, 0); }
    plot::Axes* addAxesFraction(float l, float b, float w, float h) {
        return figure_.addAxesFraction(l, b, w, h);
    }

    bool savefig(const std::string& path) {
        return renderer_->savefig(figure_, path);
    }

    // Declaration order = destruction order (reversed): renderer dies
    // first, then the figure (plots hold GPU buffers), then the device.
    std::unique_ptr<backend::IBackend> backend_;
    plot::Figure figure_;
    std::unique_ptr<render::Renderer> renderer_;
};

/// Non-owning view of an Axes inside a PyFigure.
struct PyAxes {
    std::shared_ptr<PyFigure> owner;
    plot::Axes* ax;
};

/// mpl Line2D handle — keeps the figure alive; `set_data`/`set_xdata`/
/// `set_ydata` update the GPU buffer in place (memcpy when the new data
/// fits the existing allocation) and mark the figure stale.
struct PyLine2D {
    std::shared_ptr<PyFigure> owner;
    plot::LinePlot* line;
};

/// mpl PathCollection handle for scatter (set_offsets).
struct PyCollection {
    std::shared_ptr<PyFigure> owner;
    plot::ScatterPlot* scatter;
};

/// mpl ax.xaxis / ax.yaxis proxy — locator/formatter accessors.
struct PyAxis {
    std::shared_ptr<PyFigure> owner;
    plot::Axes* ax;
    bool isX;
};

/// mpl Patch handle — `patches.Rectangle(...)` builds `spec` detached;
/// `ax.add_patch` copies it into a PatchCollection and returns a live
/// handle (`live` points into the collection — single-element vector,
/// stable address). `coll`/`ax` enable label + touch on mutation.
struct PyPatch {
    std::shared_ptr<PyFigure> owner;
    plot::Axes* ax = nullptr;
    plot::Collection* coll = nullptr;
    plot::Patch spec;
    plot::Patch* live = nullptr;
    plot::Patch& tgt() { return live ? *live : spec; }
    void touch() { if (ax) ax->touch(); }
};

/// mpl Collection handle — factory-built unique_ptr until `add_collection`
/// moves it into the axes; afterwards `live` aliases the owned object.
struct PyColl {
    std::shared_ptr<PyFigure> owner;
    plot::Axes* ax = nullptr;
    std::unique_ptr<plot::Collection> owned;
    plot::Collection* live = nullptr;
    plot::Collection& tgt() { return live ? *live : *owned; }
    void touch() { if (ax) ax->touch(); }
};

/// mpl Widget handle — the figure owns the C++ widget; this struct keeps
/// the owning figure alive and retains registered Python callbacks.
struct PyWidget {
    std::shared_ptr<PyFigure> owner;
    plot::Widget* w = nullptr;
    std::vector<py::function> callbacks;
    PyAxes ax;  ///< axes the widget covers (selectors need it live)
    std::vector<std::string> labels;  ///< CheckButtons/RadioButtons
};

/// mpl fig.canvas proxy — mpl_connect/mpl_disconnect plus a headless
/// `dispatch` for driving widgets/events without a window.
struct PyCanvas {
    std::shared_ptr<PyFigure> owner;
};

/// mpl BarContainer — returned by ax.bar/barh, consumed by ax.bar_label.
/// Bars are positioned at integer indices (category-style).
struct PyBarContainer {
    std::shared_ptr<PyFigure> owner;
    std::vector<float> x, heights;
    float baseline = 0.0f;
    bool horizontal = false;
};

/// mpl ax.spines['top'] proxy — single spine visibility.
struct PySpine {
    std::shared_ptr<PyFigure> owner;
    plot::Axes* ax;
    std::string side;
};

/// mpl cycler(): build a Cycler from keyword sequences —
/// cycler(color=['r','b'], lw=[1,2]) is the outer product, mpl-style.
plot::Cycler makeCycler(const py::args& args, const py::kwargs& kw) {
    plot::Cycler out;
    auto combine = [&](const plot::Cycler& c) {
        out = out.empty() ? c : out * c;
    };
    auto handleKey = [&](const char* key, const py::object& seq) {
        std::string k(key);
        if (k == "color" || k == "c" || k == "facecolor" || k == "fc") {
            std::vector<plot::Color> cols;
            for (auto v : py::cast<py::sequence>(seq))
                cols.push_back(parseColor(v.cast<py::object>()));
            combine(plot::Cycler::ofColors(std::move(cols)));
        } else if (k == "linestyle" || k == "ls") {
            std::vector<plot::LineStyle> lss;
            for (auto v : py::cast<py::sequence>(seq)) {
                auto ls = plot::lineStyleFromString(
                    v.cast<std::string>());
                if (!ls)
                    throw std::invalid_argument(
                        "unrecognized linestyle in cycler");
                lss.push_back(*ls);
            }
            combine(plot::Cycler::ofLineStyles(std::move(lss)));
        } else if (k == "linewidth" || k == "lw") {
            std::vector<float> lws;
            for (auto v : py::cast<py::sequence>(seq))
                lws.push_back(v.cast<float>());
            combine(plot::Cycler::ofLineWidths(std::move(lws)));
        } else if (k == "marker") {
            std::vector<plot::MarkerStyle> ms;
            for (auto v : py::cast<py::sequence>(seq)) {
                auto s = v.cast<std::string>();
                auto m = s.size() == 1
                             ? plot::markerFromChar(s[0])
                             : std::nullopt;
                if (!m)
                    throw std::invalid_argument(
                        "unrecognized marker in cycler");
                ms.push_back(*m);
            }
            combine(plot::Cycler::ofMarkers(std::move(ms)));
        } else {
            throw std::invalid_argument(std::format(
                "cycler: unsupported key '{}'", k));
        }
    };
    for (auto item : kw) handleKey(item.first.cast<std::string>().c_str(),
                                   item.second.cast<py::object>());
    for (auto a : args) {
        if (py::isinstance<plot::Cycler>(a)) {
            combine(a.cast<plot::Cycler>());
            continue;
        }
        if (py::isinstance<py::tuple>(a) && a.cast<py::tuple>().size() == 2) {
            auto t = a.cast<py::tuple>();
            handleKey(t[0].cast<std::string>().c_str(),
                      t[1].cast<py::object>());
            continue;
        }
        throw std::invalid_argument(
            "cycler: expected cycler or (key, values) pair");
    }
    return out;
}

/// Figure extent in pixels (backend extent → plot Extent2D).
plot::Extent2D figExtentPx(const std::shared_ptr<PyFigure>& f) {
    auto e = f->backend_->extent();
    return {e.width, e.height};
}

/// Canvas pixel rect of an axes (running layout if needed).
plot::Rect2D axesRectPx(PyAxes& a) {
    a.owner->figure_.layout(figExtentPx(a.owner));
    return a.ax->rect;
}

/// mpl patch kwargs: color (face+edge), facecolor/fc, edgecolor/ec,
/// linewidth/lw, linestyle/ls, alpha, hatch, fill, label.
void applyPatchStyle(plot::Patch& p, const py::kwargs& kw) {
    auto get = [&](const char* k, const char* alt = nullptr)
        -> py::object {
        if (kw.contains(k)) return kw[k].cast<py::object>();
        if (alt && kw.contains(alt)) return kw[alt].cast<py::object>();
        return py::none();
    };
    auto& s = p.style;
    if (auto c = get("color"); !c.is_none())
        s.face = s.edge = parseColor(c);
    if (auto c = get("facecolor", "fc"); !c.is_none())
        s.face = parseColor(c);
    if (auto c = get("edgecolor", "ec"); !c.is_none())
        s.edge = parseColor(c);
    if (auto v = get("linewidth", "lw"); !v.is_none())
        s.lineWidth = v.cast<float>();
    if (auto v = get("linestyle", "ls"); !v.is_none()) {
        auto ls = plot::lineStyleFromString(v.cast<std::string>());
        if (!ls) throw std::invalid_argument("unrecognized linestyle spec");
        s.lineStyle = *ls;
    }
    if (auto v = get("hatch"); !v.is_none())
        s.hatch = v.cast<std::string>();
    if (auto v = get("alpha"); !v.is_none()) {
        float al = v.cast<float>();
        s.face.a *= al; s.edge.a *= al;
    }
    if (auto v = get("fill"); !v.is_none() && !v.cast<bool>())
        s.face.a = 0;
}

/// mpl Collection kwargs: facecolors/fc, edgecolors/ec, linewidths/lw,
/// linestyle/ls, hatch, label.
void applyCollStyle(plot::Collection& c, const py::kwargs& kw) {
    auto get = [&](const char* k, const char* alt = nullptr)
        -> py::object {
        if (kw.contains(k)) return kw[k].cast<py::object>();
        if (alt && kw.contains(alt)) return kw[alt].cast<py::object>();
        return py::none();
    };
    if (auto v = get("facecolors", "fc"); !v.is_none()) {
        if (py::isinstance<py::str>(v)) c.faceColors = {parseColor(v)};
        else for (auto item : v.cast<py::sequence>())
            c.faceColors.push_back(parseColor(item.cast<py::object>()));
    }
    if (auto v = get("edgecolors", "ec"); !v.is_none()) {
        if (py::isinstance<py::str>(v)) c.edgeColors = {parseColor(v)};
        else for (auto item : v.cast<py::sequence>())
            c.edgeColors.push_back(parseColor(item.cast<py::object>()));
    }
    if (auto v = get("linewidths", "lw"); !v.is_none()) {
        if (py::isinstance<py::sequence>(v))
            c.lineWidths = v.cast<std::vector<float>>();
        else c.lineWidths = {v.cast<float>()};
    }
    if (auto v = get("linestyle", "ls"); !v.is_none()) {
        auto ls = plot::lineStyleFromString(v.cast<std::string>());
        if (!ls) throw std::invalid_argument("unrecognized linestyle spec");
        c.lineStyle = *ls;
    }
    if (auto v = get("hatch"); !v.is_none())
        c.hatch = v.cast<std::string>();
    if (auto v = get("label"); !v.is_none())
        c.label_ = v.cast<std::string>();
}

PyAxes wrapAxes(const std::shared_ptr<PyFigure>& fig, plot::Axes* ax) {
    if (!ax) throw std::runtime_error("add_axes failed");
    return {fig, ax};
}

/// mpl `plot(x, y, [fmt])` — datetime64 inputs install date converters.
/// `fmt` is the mpl format string ("ro--"); explicit kwargs (color,
/// marker, linestyle, ...) override it.
PyLine2D axesPlot(PyAxes& a, const py::object& x, const py::object& y,
                  const py::object& fmt, const py::object& color,
                  float linewidth, const py::object& marker,
                  const py::object& linestyle, float markersize,
                  float alpha, const std::string& label) {
    bool xDate = false, yDate = false;
    auto xv = toFloats(x, &xDate), yv = toFloats(y, &yDate);
    if (xDate) a.ax->xaxis_date();
    if (yDate) a.ax->yaxis_date();
    auto s = makeSeries(xv, yv);
    if (!fmt.is_none()) {
        auto fs = fmt.cast<std::string>();
        if (!fs.empty()) applyFmt(s, fs);
    }
    if (auto c = parseColor(color); c.a > 0) s.color = c;
    if (!marker.is_none() &&
        !s.setMarker(marker.cast<std::string>()))
        throw std::invalid_argument("unrecognized marker spec");
    if (!linestyle.is_none()) {
        auto ls = plot::lineStyleFromString(linestyle.cast<std::string>());
        if (!ls)
            throw std::invalid_argument("unrecognized linestyle spec");
        s.lineStyle = *ls;
    }
    if (markersize > 0.0f) s.size = markersize;
    s.label = label;
    s.lineWidth = linewidth;
    auto* lp = static_cast<plot::LinePlot*>(
        a.ax->addPlot(std::make_unique<plot::LinePlot>(std::move(s))));
    if (alpha >= 0.0f) lp->series().color.a *= alpha;
    return {a.owner, lp};
}

PyCollection axesScatter(PyAxes& a, const py::object& x,
                         const py::object& y, const py::object& color,
                         float s, const std::string& label) {
    bool xDate = false, yDate = false;
    auto xv = toFloats(x, &xDate), yv = toFloats(y, &yDate);
    if (xDate) a.ax->xaxis_date();
    if (yDate) a.ax->yaxis_date();
    auto ser = makeSeries(xv, yv);
    if (auto c = parseColor(color); c.a > 0) ser.color = c;
    ser.label = label;
    ser.size = s;
    auto* sp = static_cast<plot::ScatterPlot*>(
        a.ax->addPlot(std::make_unique<plot::ScatterPlot>(std::move(ser))));
    return {a.owner, sp};
}

// --- 3D helpers ---------------------------------------------------------
// 3D plots share the axes' camera. Each plot holds its own Camera3D copy;
// `view_init`/`set_zlim` update every camera3D() plot on the axes.

/// Pending view angles for axes with no 3D plots yet (view_init before
/// the first plot3d). Keyed by Axes* (owned by the figure).
std::unordered_map<plot::Axes*, std::array<float, 3>> gView3d;

/// The camera for a new 3D plot on `a`: reuses the existing Axes3DPlot
/// box camera (expanding its data range to cover `v`), else creates the
/// mplot3d box + a fresh camera from pending/mpl-default view angles.
plot::Camera3D cameraFor3D(PyAxes& a, const plot::Viewport& v) {
    for (auto& p : a.ax->plots()) {
        auto* box = dynamic_cast<plot::Axes3DPlot*>(p.get());
        if (!box) continue;
        // Union the new data range into the box (mpl 3D autoscaled box).
        auto vr = box->range();
        auto uni = [](plot::Range& r, const plot::Range& o) {
            r.min = std::min(r.min, o.min); r.max = std::max(r.max, o.max);
        };
        uni(vr.x, v.x); uni(vr.y, v.y); uni(vr.z, v.z);
        box->setRange(vr);
        for (auto& q : a.ax->plots())
            if (auto* cam = q->camera3D()) {
                cam->dataMin = {vr.x.min, vr.y.min, vr.z.min};
                cam->dataMax = {vr.x.max, vr.y.max, vr.z.max};
            }
        return *box->camera3D();
    }
    // mpl view_init defaults: elev=30, azim=-60, roll=0.
    float elev = 30.0f, azim = -60.0f, roll = 0.0f;
    if (auto it = gView3d.find(a.ax); it != gView3d.end()) {
        elev = it->second[0]; azim = it->second[1]; roll = it->second[2];
    }
    auto cam = plot::Camera3D::viewInit(elev, azim, roll);
    auto ext = a.owner->backend_->extent();
    if (ext.height > 0)
        cam.aspect = float(ext.width) / float(ext.height);
    cam.dataMin = {v.x.min, v.y.min, v.z.min};
    cam.dataMax = {v.x.max, v.y.max, v.z.max};
    a.ax->addPlot(std::make_unique<plot::Axes3DPlot>(cam, v));
    return cam;
}

/// mpl Axes3D.view_init: rotate every 3D camera on the axes, keeping
/// each camera's target distance/aspect/data box.
void viewInit3D(PyAxes& a, float elev, float azim, float roll) {
    gView3d[a.ax] = {elev, azim, roll};
    for (auto& p : a.ax->plots()) {
        auto* cam = p->camera3D();
        if (!cam) continue;
        auto keep = *cam;
        float dx = keep.eye.x - keep.target.x,
              dy = keep.eye.y - keep.target.y,
              dz = keep.eye.z - keep.target.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        *cam = plot::Camera3D::viewInit(elev, azim, roll, keep.target,
                                        dist > 0.0f ? dist : 10.0f);
        cam->aspect = keep.aspect;
        cam->dataMin = keep.dataMin;
        cam->dataMax = keep.dataMax;
        // Axes3DPlot geometry depends on eye direction — force rebuild.
        if (auto* box = dynamic_cast<plot::Axes3DPlot*>(p.get()))
            box->setRange(box->range());
    }
    a.ax->touch();
}

/// mpl Axes3D.errorbar(x, y, z, xerr=, yerr=, zerr=, ...).
void axesErrorbar3D(PyAxes& a, const py::object& x, const py::object& y,
                    const py::object& z, const py::object& xerr,
                    const py::object& yerr, const py::object& zerr,
                    const py::object& color, float markersize,
                    const std::string& label) {
    auto xv = toFloats(x), yv = toFloats(y), zv = toFloats(z);
    auto v = range3(xv, yv, zv);
    plot::Errorbar3DConfig cfg;
    cfg.label = label;
    if (auto c = parseColor(color); c.a > 0)
        cfg.markerColor = cfg.errorbarColor = c;
    if (markersize > 0) cfg.markerSize = markersize;
    // mpl err semantics: scalar broadcasts; (N,) symmetric;
    // (2,N) → [lower, upper] asymmetric.
    auto applyErr = [&](const py::object& e, std::vector<float>& sym,
                        std::vector<float>& lo, std::vector<float>& hi,
                        size_t n) {
        if (e.is_none()) return;
        if (py::isinstance<py::float_>(e) || py::isinstance<py::int_>(e)) {
            sym.assign(n, e.cast<float>());
            return;
        }
        auto seq = py::cast<py::sequence>(e);
        if (seq.size() == 2 && py::isinstance<py::sequence>(seq[0])) {
            lo = toFloats(seq[0].cast<py::object>());
            hi = toFloats(seq[1].cast<py::object>());
            return;
        }
        sym = toFloats(e);
    };
    size_t n = xv.size();
    applyErr(xerr, cfg.xerr, cfg.xerrLower, cfg.xerrUpper, n);
    applyErr(yerr, cfg.yerr, cfg.yerrLower, cfg.yerrUpper, n);
    applyErr(zerr, cfg.zerr, cfg.zerrLower, cfg.zerrUpper, n);
    auto cam = cameraFor3D(a, v);
    auto p = std::make_unique<plot::Errorbar3D>(
        std::move(xv), std::move(yv), std::move(zv), cfg);
    p->setCamera(cam);
    a.ax->addPlot(std::move(p));
    a.ax->touch();
}

/// mpl Axes3D.text(x, y, z, s, ...).
void axesText3D(PyAxes& a, float x, float y, float z,
                const std::string& s, const py::object& color,
                float fontsize) {
    plot::Text3DItem it;
    it.x = x; it.y = y; it.z = z; it.text = s;
    if (auto c = parseColor(color); c.a > 0) it.color = c;
    if (fontsize > 0) it.fontSize = fontsize;
    plot::Viewport v;
    v.x = {x, x}; v.y = {y, y}; v.z = {z, z};
    // Pad zero-span ranges so the box is non-degenerate.
    if (v.x.span() == 0) { v.x.min -= 1; v.x.max += 1; }
    if (v.y.span() == 0) { v.y.min -= 1; v.y.max += 1; }
    if (v.z.span() == 0) { v.z.min -= 1; v.z.max += 1; }
    auto cam = cameraFor3D(a, v);
    auto p = std::make_unique<plot::Text3D>(
        std::vector<plot::Text3DItem>{it});
    p->setCamera(cam);
    a.ax->addPlot(std::move(p));
    a.ax->touch();
}

// --- pyplot-style module state (current figure / axes) ---
std::shared_ptr<PyFigure> gCurrentFig;
plot::Axes* gCurrentAx = nullptr;

std::shared_ptr<PyFigure> gcf() {
    if (!gCurrentFig)
        gCurrentFig = std::make_shared<PyFigure>(800, 600, 100.0f);
    return gCurrentFig;
}

PyAxes gca() {
    auto f = gcf();
    if (!gCurrentAx) gCurrentAx = f->addAxes();
    return {f, gCurrentAx};
}

/// mpl `plot(y)` → implicit x = 0..n-1.
py::object implicitX(const py::object& y) {
    auto n = toFloats(y).size();
    std::vector<float> xv(n);
    for (size_t i = 0; i < n; ++i) xv[i] = float(i);
    return py::cast(xv);
}

/// mpl FuncAnimation — wraps plot::FuncAnimation with Python callables.
/// `frames` follows mpl semantics: an int calls func(0..n-1); an
/// iterable calls func(element) per element. Extra `fargs` are appended
/// to every func call.
class PyAnimation {
public:
    PyAnimation(std::shared_ptr<PyFigure> fig, py::object func,
                const py::object& frames, const py::object& initFunc,
                const py::object& fargs, int interval, bool blit,
                bool repeat, int repeatDelay)
        : owner_(std::move(fig)), func_(std::move(func)) {
        size_t count = 0;
        if (frames.is_none())
            throw std::invalid_argument(
                "frames=None is an infinite generator — pass an int "
                "frame count or an iterable");
        if (py::isinstance<py::int_>(frames)) {
            count = frames.cast<size_t>();
        } else {
            for (auto a : frames)
                frameArgs_.push_back(
                    py::reinterpret_borrow<py::object>(a));
            count = frameArgs_.size();
        }
        if (count == 0)
            throw std::invalid_argument("frames must produce >0 frames");

        if (!fargs.is_none())
            for (auto a : fargs)
                extraArgs_.push_back(py::reinterpret_borrow<py::object>(a));

        std::function<void(size_t)> fn = [this](size_t i) {
            py::gil_scoped_acquire gil;
            py::tuple t(1 + extraArgs_.size());
            t[0] = frameArgs_.empty() ? py::cast(i) : frameArgs_[i];
            for (size_t k = 0; k < extraArgs_.size(); ++k)
                t[k + 1] = extraArgs_[k];
            func_(*t);
        };
        std::function<void()> init;
        if (!initFunc.is_none()) {
            initFn_ = initFunc;
            init = [this] {
                py::gil_scoped_acquire gil;
                initFn_();
            };
        }
        anim_ = std::make_unique<plot::FuncAnimation>(
            owner_->figure_, std::move(fn), count, std::move(init),
            interval, blit, repeat);
        anim_->repeatDelay = repeatDelay;
    }

    /// mpl anim.save(path, writer=..., fps=...). fps<=0 → 1000/interval.
    /// ".html"/".htm" writes the standalone JS-player page
    /// (to_jshtml output).
    bool save(const std::string& path, const std::string& writer,
              double fps) {
        if (fps <= 0) fps = 1000.0 / anim_->interval;
        auto ext = std::filesystem::path(path).extension().string();
        if (ext == ".html" || ext == ".htm") {
            auto html = owner_->renderer_->toJsHtml(*anim_, fps);
            if (html.empty()) return false;
            std::ofstream(path, std::ios::binary) << html;
            return true;
        }
        return owner_->renderer_->saveAnimation(*anim_, path, fps, writer);
    }

    std::string toJsHtml(double fps) {
        if (fps <= 0) fps = 1000.0 / anim_->interval;
        return owner_->renderer_->toJsHtml(*anim_, fps);
    }
    std::string toHtml5Video(double fps) {
        if (fps <= 0) fps = 1000.0 / anim_->interval;
        return owner_->renderer_->toHtml5Video(*anim_, fps);
    }

private:
    // anim_ references owner_->figure_ — declared after so it dies first.
    std::shared_ptr<PyFigure> owner_;
    py::object func_;
    py::object initFn_;
    std::vector<py::object> frameArgs_;
    std::vector<py::object> extraArgs_;
    std::unique_ptr<plot::FuncAnimation> anim_;
};

/// Python value → rcParam value string (booleans lowercased for the
/// .mplstyle parser, sequences comma-joined).
std::string rcValueStr(const py::object& v) {
    if (py::isinstance<py::bool_>(v))
        return v.cast<bool>() ? "true" : "false";
    if (py::isinstance<py::str>(v)) return v.cast<std::string>();
    if (py::isinstance<py::sequence>(v)) {
        std::string s;
        for (auto e : v) {
            if (!s.empty()) s += ",";
            s += py::str(e).cast<std::string>();
        }
        return s;
    }
    return py::str(v).cast<std::string>();
}

/// mpl rcParams — dict-like proxy over rc::set. The C++ side has no
/// key→value reverse map, so reads return values written through this
/// proxy (or raise KeyError), matching mpl's write-mostly usage.
class PyRcParams {
public:
    void set(const std::string& k, const py::object& v) {
        if (!plot::rc::set(k, rcValueStr(v)))
            throw py::key_error("unknown rcParam '" + k + "'");
        shadow_[k] = v;
    }
    py::object get(const std::string& k) const {
        auto it = shadow_.find(k);
        if (it == shadow_.end())
            throw py::key_error("rcParam '" + k + "' not set via rcParams");
        return it->second;
    }
    bool contains(const std::string& k) const {
        return shadow_.contains(k);
    }
    std::vector<std::string> keys() const {
        std::vector<std::string> ks;
        ks.reserve(shadow_.size());
        for (const auto& [k, _] : shadow_) ks.push_back(k);
        return ks;
    }
    void clear() { shadow_.clear(); }

private:
    std::unordered_map<std::string, py::object> shadow_;
};

/// plt.style.context — RAII guard for `with` blocks.
struct PyStyleContext {
    explicit PyStyleContext(plot::rc::Context c) : ctx(std::move(c)) {}
    PyStyleContext& enter() { return *this; }
    void exit(const py::object&, const py::object&, const py::object&) {
        ctx.reset();
    }
    std::optional<plot::rc::Context> ctx;
};

/// The module-level `rcParams` object — `rc()`/`rcdefaults()` share its
/// shadow map so writes are visible via either path.
std::shared_ptr<PyRcParams> gRcParams = std::make_shared<PyRcParams>();

} // namespace

PYBIND11_MODULE(volcanoplot, m) {
    m.doc() = "VolcanoPlot — Vulkan-accelerated matplotlib-style plotting";

    py::class_<PyFigure, std::shared_ptr<PyFigure>>(m, "Figure")
        .def(py::init<uint32_t, uint32_t, float>(),
             py::arg("width") = 800, py::arg("height") = 600,
             py::arg("dpi") = 100.0f)
        .def(py::init([](std::pair<double, double> figsize, float dpi) {
                 // mpl signature: Figure(figsize=(w_in, h_in), dpi=...)
                 return std::make_shared<PyFigure>(
                     uint32_t(figsize.first * dpi + 0.5),
                     uint32_t(figsize.second * dpi + 0.5), dpi);
             }),
             py::arg("figsize"), py::arg("dpi") = 100.0f)
        .def_property_readonly("stale",
             [](const PyFigure& f) { return f.figure_.stale(); })
        .def("add_axes", [](const std::shared_ptr<PyFigure>& f) {
                 return wrapAxes(f, f->addAxes());
             })
        .def("add_axes_fraction",
             [](const std::shared_ptr<PyFigure>& f, float l, float b,
                float w, float h) {
                 return wrapAxes(f, f->addAxesFraction(l, b, w, h));
             },
             py::arg("left"), py::arg("bottom"), py::arg("width"), py::arg("height"))
        // ── mpl fig.add_subplot / subplot_mosaic / colorbar ──
        .def("add_subplot",
             [](const std::shared_ptr<PyFigure>& f, const py::object& a,
                const py::object& b, const py::object& c,
                const py::object& projection) {
                 uint32_t nrows, ncols, index;
                 if (b.is_none()) {
                     // mpl 3-digit shorthand: add_subplot(231).
                     int code = a.cast<int>();
                     if (code < 100 || code > 999)
                         throw std::invalid_argument(
                             "add_subplot(int) expects a 3-digit code");
                     nrows = uint32_t(code / 100);
                     ncols = uint32_t(code / 10 % 10);
                     index = uint32_t(code % 10);
                 } else {
                     nrows = a.cast<uint32_t>();
                     ncols = b.cast<uint32_t>();
                     index = c.is_none() ? 1u : c.cast<uint32_t>();
                 }
                 if (index < 1 || index > nrows * ncols)
                     throw std::invalid_argument(
                         "add_subplot index out of range");
                 // mpl index is 1-based, row-major.
                 uint32_t r = (index - 1) / ncols, col = (index - 1) % ncols;
                 auto ax = wrapAxes(
                     f, f->figure_.subplot2grid({nrows, ncols}, {r, col}));
                 if (!projection.is_none())
                     ax.ax->setProjection(
                         projection.cast<std::string>());
                 return ax;
             },
             py::arg("nrows"), py::arg("ncols") = py::none(),
             py::arg("index") = py::none(),
             py::arg("projection") = py::none(),
             "mpl fig.add_subplot(111) or add_subplot(nrows, ncols, index).")
        .def("subplot2grid",
             [](const std::shared_ptr<PyFigure>& f,
                std::pair<uint32_t, uint32_t> shape,
                std::pair<uint32_t, uint32_t> loc, uint32_t rowspan,
                uint32_t colspan) {
                 return wrapAxes(f, f->figure_.subplot2grid(shape, loc,
                                                            rowspan,
                                                            colspan));
             },
             py::arg("shape"), py::arg("loc"), py::arg("rowspan") = 1,
             py::arg("colspan") = 1)
        .def("subplot_mosaic",
             [](const std::shared_ptr<PyFigure>& f,
                const py::object& mosaic) {
                 // mpl accepts "AB;CD" strings, ["AB","CD"] row lists,
                 // or [["A","B"],["C","D"]] cell lists.
                 std::vector<std::vector<std::string>> layout;
                 auto addRow = [](std::vector<std::vector<std::string>>& l,
                                  const std::string& row) {
                     l.emplace_back();
                     for (char ch : row)
                         if (!std::isspace(static_cast<unsigned char>(ch)))
                             l.back().push_back(std::string(1, ch));
                 };
                 if (py::isinstance<py::str>(mosaic)) {
                     std::stringstream ss(mosaic.cast<std::string>());
                     std::string row;
                     while (std::getline(ss, row, ';')) {
                         if (row.find_first_not_of(" \t\n") ==
                             std::string::npos)
                             continue;
                         addRow(layout, row);
                     }
                 } else {
                     for (auto row : py::cast<py::sequence>(mosaic)) {
                         if (py::isinstance<py::str>(row))
                             addRow(layout, row.cast<std::string>());
                         else
                             layout.push_back(
                                 row.cast<std::vector<std::string>>());
                     }
                 }
                 py::dict out;
                 for (auto& [k, v] : f->figure_.subplotMosaic(layout))
                     out[py::str(k)] = wrapAxes(f, v);
                 return out;
             },
             py::arg("mosaic"),
             "mpl fig.subplot_mosaic — returns {label: Axes}.")
        .def("colorbar",
             [](const std::shared_ptr<PyFigure>& f,
                const py::object& axObj, const std::string& orientation,
                float fraction, float pad, float shrink, float aspect,
                const std::string& label) {
                 plot::Axes* target = nullptr;
                 if (!axObj.is_none())
                     target = axObj.cast<PyAxes>().ax;
                 else if (auto all = f->figure_.allAxes(); !all.empty())
                     target = all.back();
                 if (!target)
                     throw std::invalid_argument(
                         "colorbar: no axes to attach to");
                 auto& cb = target->style().colorbar;
                 cb.visible = true;
                 cb.orientation = orientation;
                 cb.fraction = fraction;
                 cb.pad = pad;
                 cb.shrink = shrink;
                 if (aspect > 0.0f) cb.aspect = aspect;
                 if (!label.empty()) cb.label = label;
                 target->touch();
             },
             py::arg("ax") = py::none(),
             py::arg("orientation") = "vertical",
             py::arg("fraction") = 0.15f, py::arg("pad") = 0.05f,
             py::arg("shrink") = 1.0f, py::arg("aspect") = -1.0f,
             py::arg("label") = "",
             "mpl fig.colorbar(ax=...) — strip for the axes' "
             "colormapped plot.")
        .def("suptitle", [](PyFigure& f, std::string t) {
                 f.figure_.suptitle(std::move(t));
             })
        .def("supxlabel", [](PyFigure& f, std::string t) {
                 f.figure_.supxlabel(std::move(t));
             })
        .def("supylabel", [](PyFigure& f, std::string t) {
                 f.figure_.supylabel(std::move(t));
             })
        .def("legend", [](PyFigure& f, std::string loc) {
                 f.figure_.legend(loc);
             },
             py::arg("loc") = "upper right",
             "Figure-level legend collecting all axes' handles.")
        .def("tight_layout", [](PyFigure& f) {
                 f.figure_.setTightLayout(true); })
        .def("constrained_layout", [](PyFigure& f) {
                 f.figure_.setConstrainedLayout(true); })
        .def("subplots_adjust",
             [](PyFigure& f, float left, float bottom, float right,
                float top, float wspace, float hspace) {
                 f.figure_.subplotsAdjust(left, bottom, right, top,
                                          wspace, hspace);
             },
             py::arg("left") = 0.125f, py::arg("bottom") = 0.11f,
             py::arg("right") = 0.9f, py::arg("top") = 0.88f,
             py::arg("wspace") = 0.2f, py::arg("hspace") = 0.2f)
        .def("align_labels", [](PyFigure& f) { f.figure_.alignLabels(); })
        .def("align_xlabels", [](PyFigure& f) { f.figure_.alignXlabels(); })
        .def("align_ylabels", [](PyFigure& f) { f.figure_.alignYlabels(); })
        // mpl fig.autofmt_xdate: rotate x tick labels (30° right-aligned
        // by default) so date labels don't overlap.
        .def("autofmt_xdate",
             [](PyFigure& f, float rotation, const std::string& ha,
                const std::string& which) {
                 for (auto* ax : f.figure_.allAxes()) {
                     auto& xf = ax->style().xAxis.tickFont;
                     xf.rotation = rotation * float(M_PI) / 180.0f;
                     if (ha == "right") xf.halign = plot::HAlign::Right;
                     else if (ha == "center")
                         xf.halign = plot::HAlign::Center;
                     else if (ha == "left") xf.halign = plot::HAlign::Left;
                 }
             },
             py::arg("rotation") = 30.0f, py::arg("ha") = "right",
             py::arg("which") = "major")
        // mpl fig.figimage(Z, xo, yo): pixel-space RGBA overlay that
        // ignores axes coordinates (watermarks, logos).
        .def("figimage",
             [](PyFigure& f, const py::object& img, uint32_t xo,
                uint32_t yo, const std::string& cmap,
                const py::object& vmin, const py::object& vmax) {
                 auto im = toRgbaImage(img, cmap, vmin, vmax);
                 plot::FigImageConfig cfg;
                 cfg.x = xo; cfg.y = yo;
                 // FigImagePlot draws in figure pixels regardless of the
                 // owning axes' viewport — attach to the first axes.
                 auto* ax = f.figure_.allAxes().empty()
                                ? f.addAxes()
                                : f.figure_.allAxes().front();
                 ax->addPlot(std::make_unique<plot::FigImagePlot>(
                     std::move(im.px), im.w, im.h, cfg));
                 ax->touch();
             },
             py::arg("X"), py::arg("xo") = 0, py::arg("yo") = 0,
             py::arg("cmap") = "viridis", py::arg("vmin") = py::none(),
             py::arg("vmax") = py::none())
        /// mpl fig.canvas — mpl_connect/mpl_disconnect.
        .def_property_readonly("canvas",
             [](std::shared_ptr<PyFigure> f) { return PyCanvas{f}; })
        /// mpl plt.show(): open an SDL window and run the interactive
        /// event loop until the window closes (or 'q'). No-op without a
        /// display/SDL build.
        .def("show",
             [](std::shared_ptr<PyFigure> f) {
                 auto ext = f->backend_->extent();
                 backend::BackendDesc desc;
                 desc.width = ext.width;
                 desc.height = ext.height;
                 desc.windowTitle = "VolcanoPlot";
                 desc.samples = vk::SampleCountFlagBits::e4;
                 auto win = backend::createScreenBackend(desc);
                 if (!win)
                     throw std::runtime_error(
                         "screen backend unavailable (no display/SDL)");
                 render::Renderer r(*win);
                 r.prepare(f->figure_);
                 auto& nav = f->figure_.nav();
                 nav.scrollZoom = true;
                 bool running = true;
                 nav.onQuitRequest = [&] { running = false; };
                 nav.onFullscreenToggle = [&] { win->toggleFullscreen(); };
                 nav.onSaveRequest = [&] {
                     r.savefig(f->figure_, "volcano_screen.png");
                 };
                 {
                     py::gil_scoped_release release;
                     while (running && win->pollEvents()) {
                         if (!r.processInput(f->figure_)) break;
                         r.renderFrame(f->figure_);
                     }
                 }
             })
        .def("savefig", &PyFigure::savefig, py::arg("path"),
             "Render and save (png/webp/bmp/jpg/tiff/pdf/svg/eps).");

    py::class_<PyLine2D>(m, "Line2D")
        .def("set_data",
             [](PyLine2D& l, const py::object& x, const py::object& y) {
                 l.line->setData(toFloats(x), toFloats(y));
             })
        .def("set_xdata",
             [](PyLine2D& l, const py::object& x) {
                 l.line->setXdata(toFloats(x));
             })
        .def("set_ydata",
             [](PyLine2D& l, const py::object& y) {
                 l.line->setYdata(toFloats(y));
             });

    py::class_<PyCollection>(m, "PathCollection")
        .def("set_offsets",
             [](PyCollection& c, const py::object& x,
                const py::object& y) {
                 c.scatter->setData(toFloats(x), toFloats(y));
             });

    // ── mpl ticker: Locator / Formatter hierarchies ──
    py::class_<plot::Locator, std::shared_ptr<plot::Locator>>(m,
                                                            "Locator");
    py::class_<plot::Formatter, std::shared_ptr<plot::Formatter>>(
        m, "Formatter");

    py::class_<plot::NullLocator, plot::Locator,
               std::shared_ptr<plot::NullLocator>>(m, "NullLocator")
        .def(py::init<>());
    py::class_<plot::FixedLocator, plot::Locator,
               std::shared_ptr<plot::FixedLocator>>(m, "FixedLocator")
        .def(py::init([](const py::object& ticks) {
                 return std::make_shared<plot::FixedLocator>(
                     toFloats(ticks));
             }),
             py::arg("ticks"));
    py::class_<plot::LinearLocator, plot::Locator,
               std::shared_ptr<plot::LinearLocator>>(m, "LinearLocator")
        .def(py::init<int>(), py::arg("numticks") = 0);
    py::class_<plot::MultipleLocator, plot::Locator,
               std::shared_ptr<plot::MultipleLocator>>(m,
                                                       "MultipleLocator")
        .def(py::init<float, float>(), py::arg("base"),
             py::arg("offset") = 0.0f);
    py::class_<plot::IndexLocator, plot::Locator,
               std::shared_ptr<plot::IndexLocator>>(m, "IndexLocator")
        .def(py::init<float, float>(), py::arg("base"),
             py::arg("offset") = 0.0f);
    py::class_<plot::MaxNLocator, plot::Locator,
               std::shared_ptr<plot::MaxNLocator>>(m, "MaxNLocator")
        .def(py::init<int>(), py::arg("nbins") = 9);
    py::class_<plot::AutoLocator, plot::MaxNLocator,
               std::shared_ptr<plot::AutoLocator>>(m, "AutoLocator")
        .def(py::init<>());
    py::class_<plot::LogLocator, plot::Locator,
               std::shared_ptr<plot::LogLocator>>(m, "LogLocator")
        .def(py::init<float, int>(), py::arg("base") = 10.0f,
             py::arg("numticks") = 9);
    py::class_<plot::SymmetricalLogLocator, plot::Locator,
               std::shared_ptr<plot::SymmetricalLogLocator>>(
        m, "SymmetricalLogLocator")
        .def(py::init<float>(), py::arg("linthresh") = 2.0f);
    py::class_<plot::LogitLocator, plot::Locator,
               std::shared_ptr<plot::LogitLocator>>(m, "LogitLocator")
        .def(py::init<>());
    py::class_<plot::AutoMinorLocator, plot::Locator,
               std::shared_ptr<plot::AutoMinorLocator>>(m,
                                                        "AutoMinorLocator")
        .def(py::init<int>(), py::arg("ndivs") = 0);

    py::class_<plot::NullFormatter, plot::Formatter,
               std::shared_ptr<plot::NullFormatter>>(m, "NullFormatter")
        .def(py::init<>());
    py::class_<plot::FixedFormatter, plot::Formatter,
               std::shared_ptr<plot::FixedFormatter>>(m, "FixedFormatter")
        .def(py::init<std::vector<std::string>>(), py::arg("labels"));
    py::class_<plot::FuncFormatter, plot::Formatter,
               std::shared_ptr<plot::FuncFormatter>>(m, "FuncFormatter")
        .def(py::init([](py::function f) {
                 return std::make_shared<plot::FuncFormatter>(
                     [f](float v, int pos) {
                         py::gil_scoped_acquire gil;
                         return f(v, pos).cast<std::string>();
                     });
             }),
             py::arg("func"), "mpl FuncFormatter — func(value, pos).");
    py::class_<plot::FormatStrFormatter, plot::Formatter,
               std::shared_ptr<plot::FormatStrFormatter>>(
        m, "FormatStrFormatter")
        .def(py::init<std::string>(), py::arg("fmt"),
             "mpl printf-style format, e.g. '%.2f'.");
    py::class_<plot::StrMethodFormatter, plot::Formatter,
               std::shared_ptr<plot::StrMethodFormatter>>(
        m, "StrMethodFormatter")
        .def(py::init<std::string>(), py::arg("fmt"),
             "mpl '{x}' / '{pos}' template.");
    py::class_<plot::ScalarFormatter, plot::Formatter,
               std::shared_ptr<plot::ScalarFormatter>>(m,
                                                       "ScalarFormatter")
        .def(py::init<>())
        .def_readwrite("scilimits", &plot::ScalarFormatter::scilimits)
        .def_readwrite("useOffset", &plot::ScalarFormatter::useOffset)
        .def_readwrite("useMathText",
                       &plot::ScalarFormatter::useMathText);
    py::class_<plot::LogFormatter, plot::Formatter,
               std::shared_ptr<plot::LogFormatter>>(m, "LogFormatter")
        .def(py::init<float>(), py::arg("base") = 10.0f)
        .def_readwrite("labelOnlyBase",
                       &plot::LogFormatter::labelOnlyBase)
        .def_readwrite("minorThresholds",
                       &plot::LogFormatter::minorThresholds);
    py::class_<plot::LogFormatterExponent, plot::LogFormatter,
               std::shared_ptr<plot::LogFormatterExponent>>(
        m, "LogFormatterExponent")
        .def(py::init<float>(), py::arg("base") = 10.0f);
    py::class_<plot::LogFormatterMathtext, plot::LogFormatter,
               std::shared_ptr<plot::LogFormatterMathtext>>(
        m, "LogFormatterMathtext")
        .def(py::init<float>(), py::arg("base") = 10.0f);
    py::class_<plot::LogFormatterSciNotation, plot::LogFormatter,
               std::shared_ptr<plot::LogFormatterSciNotation>>(
        m, "LogFormatterSciNotation")
        .def(py::init<float>(), py::arg("base") = 10.0f);
    py::class_<plot::LogitFormatter, plot::Formatter,
               std::shared_ptr<plot::LogitFormatter>>(m, "LogitFormatter")
        .def(py::init<>());
    py::class_<plot::EngFormatter, plot::Formatter,
               std::shared_ptr<plot::EngFormatter>>(m, "EngFormatter")
        .def(py::init<std::string, int, std::string>(),
             py::arg("unit") = "", py::arg("places") = 1,
             py::arg("sep") = " ");
    py::class_<plot::PercentFormatter, plot::Formatter,
               std::shared_ptr<plot::PercentFormatter>>(m,
                                                        "PercentFormatter")
        .def(py::init<float, int, std::string>(),
             py::arg("xmax") = 100.0f, py::arg("decimals") = -1,
             py::arg("symbol") = "%");

    // ── mpl matplotlib.dates submodule ───────────────────────────
    {
        auto dm = m.def_submodule("dates");
        namespace pd = plot::dates;

        dm.def("date2num",
            [](const py::object& d) -> py::object {
                auto mod = py::module_::import("datetime");
                auto one = [&](const py::object& o) -> double {
                    if (py::isinstance<py::float_>(o) ||
                        py::isinstance<py::int_>(o))
                        return o.cast<double>();
                    py::object o2 = o;
                    if (!py::isinstance(o2, mod.attr("datetime")) &&
                        py::isinstance(o2, mod.attr("date")))
                        o2 = mod.attr("datetime").attr("combine")(
                            o, mod.attr("time")());
                    if (!py::isinstance(o2, mod.attr("datetime")))
                        throw std::invalid_argument(
                            "date2num: expected datetime/date/float");
                    if (o2.attr("tzinfo").is_none())
                        o2 = o2.attr("replace")(
                            py::arg("tzinfo") =
                                mod.attr("timezone").attr("utc"));
                    return o2.attr("timestamp")().cast<double>() /
                           86400.0;
                };
                if (py::isinstance<py::sequence>(d) &&
                    !py::isinstance<py::str>(d)) {
                    py::list out;
                    for (auto item : d)
                        out.append(one(item.cast<py::object>()));
                    return out;
                }
                return py::float_(one(d));
            },
            py::arg("d"));
        dm.def("num2date",
            [](const py::object& v) -> py::object {
                auto mod = py::module_::import("datetime");
                auto one = [&](double days) -> py::object {
                    return mod.attr("datetime").attr("fromtimestamp")(
                        days * 86400.0,
                        py::arg("tz") = mod.attr("timezone").attr("utc"));
                };
                if (py::isinstance<py::sequence>(v) &&
                    !py::isinstance<py::str>(v)) {
                    py::list out;
                    for (auto item : v)
                        out.append(one(item.cast<double>()));
                    return out;
                }
                return one(v.cast<double>());
            },
            py::arg("v"));
        dm.def("strfnum", [](double days, const std::string& fmt) {
            return pd::strfnum(float(days), fmt.c_str());
        }, py::arg("days"), py::arg("fmt"));

        py::class_<pd::YearLocator, plot::Locator,
                   std::shared_ptr<pd::YearLocator>>(dm, "YearLocator")
            .def(py::init<int, int, int>(),
                 py::arg("base") = 1, py::arg("month") = 1,
                 py::arg("day") = 1);
        py::class_<pd::MonthLocator, plot::Locator,
                   std::shared_ptr<pd::MonthLocator>>(dm, "MonthLocator")
            .def(py::init<int, int>(),
                 py::arg("interval") = 1, py::arg("day") = 1);
        py::class_<pd::WeekdayLocator, plot::Locator,
                   std::shared_ptr<pd::WeekdayLocator>>(dm,
                                                        "WeekdayLocator")
            .def(py::init<int, int>(),
                 py::arg("interval") = 1, py::arg("byweekday") = 0);
        py::class_<pd::DayLocator, plot::Locator,
                   std::shared_ptr<pd::DayLocator>>(dm, "DayLocator")
            .def(py::init<int>(), py::arg("interval") = 1);
        py::class_<pd::HourLocator, plot::Locator,
                   std::shared_ptr<pd::HourLocator>>(dm, "HourLocator")
            .def(py::init<int>(), py::arg("interval") = 1);
        py::class_<pd::MinuteLocator, plot::Locator,
                   std::shared_ptr<pd::MinuteLocator>>(dm, "MinuteLocator")
            .def(py::init<int>(), py::arg("interval") = 1);
        py::class_<pd::SecondLocator, plot::Locator,
                   std::shared_ptr<pd::SecondLocator>>(dm, "SecondLocator")
            .def(py::init<int>(), py::arg("interval") = 1);
        py::class_<pd::MicrosecondLocator, plot::Locator,
                   std::shared_ptr<pd::MicrosecondLocator>>(
            dm, "MicrosecondLocator")
            .def(py::init<int64_t>(), py::arg("interval") = 1);
        py::class_<pd::AutoDateLocator, plot::Locator,
                   std::shared_ptr<pd::AutoDateLocator>>(
            dm, "AutoDateLocator")
            .def(py::init<>())
            .def_readwrite("minticks", &pd::AutoDateLocator::minticks)
            .def_readwrite("maxticks", &pd::AutoDateLocator::maxticks);

        py::class_<pd::DateFormatter, plot::Formatter,
                   std::shared_ptr<pd::DateFormatter>>(dm,
                                                       "DateFormatter")
            .def(py::init<std::string>(), py::arg("fmt"));
        py::class_<pd::AutoDateFormatter, plot::Formatter,
                   std::shared_ptr<pd::AutoDateFormatter>>(
            dm, "AutoDateFormatter")
            .def(py::init<>());
        py::class_<pd::ConciseDateFormatter, plot::Formatter,
                   std::shared_ptr<pd::ConciseDateFormatter>>(
            dm, "ConciseDateFormatter")
            .def(py::init<>());
    }

    // mpl ax.xaxis / ax.yaxis proxy.
    py::class_<PyAxis>(m, "Axis")
        .def("set_major_locator",
             [](PyAxis& s, std::shared_ptr<plot::Locator> l) {
                 if (s.isX) s.ax->setXLocator(std::move(l));
                 else s.ax->setYLocator(std::move(l));
                 s.ax->touch();
             },
             py::arg("locator"))
        .def("set_minor_locator",
             [](PyAxis& s, std::shared_ptr<plot::Locator> l) {
                 if (s.isX) s.ax->setXMinorLocator(std::move(l));
                 else s.ax->setYMinorLocator(std::move(l));
                 s.ax->touch();
             },
             py::arg("locator"))
        .def("set_major_formatter",
             [](PyAxis& s, std::shared_ptr<plot::Formatter> f) {
                 if (s.isX) s.ax->setXFormatter(std::move(f));
                 else s.ax->setYFormatter(std::move(f));
                 s.ax->touch();
             },
             py::arg("formatter"))
        .def("set_minor_formatter",
             [](PyAxis& s, std::shared_ptr<plot::Formatter> f) {
                 if (s.isX) s.ax->setXMinorFormatter(std::move(f));
                 else s.ax->setYMinorFormatter(std::move(f));
                 s.ax->touch();
             },
             py::arg("formatter"));

    // ── mpl Path (vertices + codes) ──────────────────────────────
    py::class_<plot::Path>(m, "Path")
        .def(py::init([](const std::vector<std::pair<float,float>>& verts,
                         const py::object& codes) {
                 std::vector<plot::Point2D> v;
                 v.reserve(verts.size());
                 for (auto& [x, y] : verts) v.push_back({x, y});
                 if (codes.is_none()) return plot::Path(std::move(v));
                 auto c = codes.cast<std::vector<uint8_t>>();
                 std::vector<plot::Path::Code> cs;
                 cs.reserve(c.size());
                 for (auto b : c)
                     cs.push_back(static_cast<plot::Path::Code>(b));
                 return plot::Path(std::move(v), std::move(cs));
             }),
             py::arg("vertices"), py::arg("codes") = py::none())
        .def_static("unit_circle", &plot::Path::unitCircle)
        .def_static("unit_rectangle", &plot::Path::unitRectangle)
        .def_static("unit_regular_polygon",
                    &plot::Path::unitRegularPolygon, py::arg("n"))
        .def_static("unit_star", &plot::Path::unitStar, py::arg("n"))
        .def_static("unit_asterisk", &plot::Path::unitAsterisk,
                    py::arg("n"))
        .def_static("unit_wedge", &plot::Path::unitWedge,
                    py::arg("theta1"), py::arg("theta2"),
                    py::arg("inner_r") = 0.0f)
        .def_static("rectangle", &plot::Path::rectangle,
                    py::arg("x"), py::arg("y"),
                    py::arg("w"), py::arg("h"))
        .def_static("ellipse", &plot::Path::ellipse,
                    py::arg("center"), py::arg("rx"), py::arg("ry"),
                    py::arg("angle") = 0.0f)
        .def("contains_point",
             [](const plot::Path& p, std::pair<float,float> pt) {
                 return p.containsPoint({pt.first, pt.second});
             },
             py::arg("point"))
        .def_property_readonly("vertices",
             [](const plot::Path& p) {
                 std::vector<std::pair<float,float>> v;
                 v.reserve(p.vertices.size());
                 for (auto q : p.vertices) v.push_back({q.x, q.y});
                 return v;
             });
    // mpl Path code constants (Path.MOVETO etc.).
    {
        py::object pc = m.attr("Path");
        pc.attr("STOP")     = py::int_((int)plot::Path::Stop);
        pc.attr("MOVETO")   = py::int_((int)plot::Path::MoveTo);
        pc.attr("LINETO")   = py::int_((int)plot::Path::LineTo);
        pc.attr("CURVE3")   = py::int_((int)plot::Path::Curve3);
        pc.attr("CURVE4")   = py::int_((int)plot::Path::Curve4);
        pc.attr("CLOSEPOLY") = py::int_((int)plot::Path::ClosePoly);
    }

    // ── mpl Patch ────────────────────────────────────────────────
    py::class_<PyPatch>(m, "Patch")
        .def("set_facecolor",
             [](PyPatch& p, const py::object& c) {
                 p.tgt().style.face = parseColor(c); p.touch();
             }, py::arg("color"))
        .def("set_fc",
             [](PyPatch& p, const py::object& c) {
                 p.tgt().style.face = parseColor(c); p.touch();
             }, py::arg("color"))
        .def("set_edgecolor",
             [](PyPatch& p, const py::object& c) {
                 p.tgt().style.edge = parseColor(c); p.touch();
             }, py::arg("color"))
        .def("set_ec",
             [](PyPatch& p, const py::object& c) {
                 p.tgt().style.edge = parseColor(c); p.touch();
             }, py::arg("color"))
        .def("set_linewidth",
             [](PyPatch& p, float w) {
                 p.tgt().style.lineWidth = w; p.touch();
             }, py::arg("w"))
        .def("set_linestyle",
             [](PyPatch& p, const std::string& s) {
                 auto ls = plot::lineStyleFromString(s);
                 if (!ls) throw std::invalid_argument(
                     "unrecognized linestyle spec");
                 p.tgt().style.lineStyle = *ls; p.touch();
             }, py::arg("ls"))
        .def("set_alpha",
             [](PyPatch& p, float a) {
                 p.tgt().style.face.a = a;
                 p.tgt().style.edge.a = a;
                 p.touch();
             }, py::arg("a"))
        .def("set_hatch",
             [](PyPatch& p, const std::string& h) {
                 p.tgt().style.hatch = h; p.touch();
             }, py::arg("hatch"))
        .def("set_fill",
             [](PyPatch& p, bool f) {
                 if (!f) p.tgt().style.face.a = 0;
                 p.touch();
             }, py::arg("b"))
        .def("set_label",
             [](PyPatch& p, const std::string& s) {
                 if (p.coll) p.coll->label_ = s; p.touch();
             }, py::arg("label"));

    // ── mpl Collection ───────────────────────────────────────────
    py::class_<PyColl>(m, "Collection")
        .def("set_label",
             [](PyColl& c, const std::string& s) {
                 c.tgt().label_ = s; c.touch();
             }, py::arg("label"));

    // mpl BarContainer — return value of ax.bar/barh.
    py::class_<PyBarContainer>(m, "BarContainer")
        .def_property_readonly("patches", [](const PyBarContainer&) {
            return py::list();
        });

    // mpl Spine — visibility proxy for ax.spines['top'].
    py::class_<PySpine>(m, "Spine")
        .def("set_visible",
             [](PySpine& s, bool v) {
                 s.ax->setSpineVisible(s.side, v);
             },
             py::arg("visible"))
        .def("get_visible",
             [](const PySpine& s) {
                 const auto& sp = s.ax->spines();
                 if (s.side == "left") return sp.left;
                 if (s.side == "right") return sp.right;
                 if (s.side == "bottom") return sp.bottom;
                 return sp.top;
             });

    // mpl cycler.Cycler — buildable via vp.cycler(...) or
    // vp.cycler('color', [...]); supports + (concat) and * (outer product).
    py::class_<plot::Cycler>(m, "Cycler")
        .def("__len__", &plot::Cycler::length)
        .def("reset", &plot::Cycler::reset)
        .def("__add__",
             [](const plot::Cycler& a, const plot::Cycler& b) {
                 return a + b;
             })
        .def("__mul__",
             [](const plot::Cycler& a, const plot::Cycler& b) {
                 return a * b;
             });
    m.def("cycler",
          [](const py::args& args, const py::kwargs& kw) {
              return makeCycler(args, kw);
          },
          "mpl cycler(): cycler(color=[...]) or cycler('color', [...]) — "
          "multi-key form takes the outer product.");

    // mpl matplotlib.patches submodule.
    auto patches = m.def_submodule("patches");
    patches.def("Rectangle",
        [](std::pair<float,float> xy, float w, float h, float angle,
           const py::kwargs& kw) {
            auto p = plot::patch::Rectangle(xy.first, xy.second, w, h);
            if (angle != 0.0f)
                p.path = p.path.transformed(plot::Affine2D::rotateAround(
                    xy.first, xy.second, angle));
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("xy"), py::arg("width"), py::arg("height"),
        py::arg("angle") = 0.0f);
    patches.def("Circle",
        [](std::pair<float,float> xy, float radius,
           const py::kwargs& kw) {
            auto p = plot::patch::Circle({xy.first, xy.second}, radius);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("xy"), py::arg("radius") = 0.5f);
    patches.def("Ellipse",
        [](std::pair<float,float> xy, float w, float h, float angle,
           const py::kwargs& kw) {
            auto p = plot::patch::Ellipse({xy.first, xy.second}, w, h,
                                          angle);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("xy"), py::arg("width"), py::arg("height"),
        py::arg("angle") = 0.0f);
    patches.def("Polygon",
        [](const std::vector<std::pair<float,float>>& xy, bool closed,
           const py::kwargs& kw) {
            std::vector<plot::Point2D> v;
            v.reserve(xy.size());
            for (auto& [x, y] : xy) v.push_back({x, y});
            auto p = plot::patch::Polygon(std::move(v), closed);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("xy"), py::arg("closed") = true);
    patches.def("Wedge",
        [](std::pair<float,float> center, float r, float t1, float t2,
           const py::kwargs& kw) {
            auto p = plot::patch::Wedge({center.first, center.second},
                                        r, t1, t2);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("center"), py::arg("r"),
        py::arg("theta1"), py::arg("theta2"));
    patches.def("FancyBboxPatch",
        [](std::pair<float,float> xy, float w, float h,
           const std::string& boxstyle, float mutation_scale,
           const py::kwargs& kw) {
            plot::Patch p;
            if (auto spec = plot::parseBoxStyle(boxstyle)) {
                auto s = *spec;
                s.mutationSize *= mutation_scale;
                p = plot::patch::FancyBboxPatch(xy.first, xy.second,
                                              w, h, s);
            } else {
                p = plot::patch::FancyBboxPatch(xy.first, xy.second,
                                              w, h);
            }
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("xy"), py::arg("width"), py::arg("height"),
        py::arg("boxstyle") = "round", py::arg("mutation_scale") = 1.0f);
    patches.def("FancyArrowPatch",
        [](std::pair<float,float> a, std::pair<float,float> b,
           float mutation_scale, const py::kwargs& kw) {
            auto p = plot::patch::FancyArrowPatch(
                {a.first, a.second}, {b.first, b.second},
                0.2f * mutation_scale, 0.4f * mutation_scale);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("posA"), py::arg("posB"),
        py::arg("mutation_scale") = 1.0f);
    patches.def("PathPatch",
        [](const plot::Path& path, const py::kwargs& kw) {
            auto p = plot::patch::PathPatch(path);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("path"));
    patches.def("Arrow",
        [](float x, float y, float dx, float dy, float width,
           const py::kwargs& kw) {
            auto p = plot::patch::FancyArrowPatch({x, y}, {x + dx, y + dy},
                                                  width, width * 2.0f);
            applyPatchStyle(p, kw);
            PyPatch pp; pp.spec = std::move(p);
            return pp;
        },
        py::arg("x"), py::arg("y"), py::arg("dx"), py::arg("dy"),
        py::arg("width") = 0.1f);

    // mpl matplotlib.collections submodule.
    auto colls = m.def_submodule("collections");
    colls.def("LineCollection",
        [](const std::vector<std::vector<std::pair<float,float>>>& segs,
           const py::kwargs& kw) {
            std::vector<std::vector<plot::Point2D>> ss;
            ss.reserve(segs.size());
            for (auto& s : segs) {
                std::vector<plot::Point2D> v;
                v.reserve(s.size());
                for (auto& [x, y] : s) v.push_back({x, y});
                ss.push_back(std::move(v));
            }
            auto c = std::make_unique<plot::LineCollection>(
                std::move(ss));
            applyCollStyle(*c, kw);
            PyColl pc; pc.owned = std::move(c);
            return pc;
        },
        py::arg("segments"));
    colls.def("PolyCollection",
        [](const std::vector<std::vector<std::pair<float,float>>>& verts,
           const py::kwargs& kw) {
            std::vector<std::vector<plot::Point2D>> ps;
            ps.reserve(verts.size());
            for (auto& s : verts) {
                std::vector<plot::Point2D> v;
                v.reserve(s.size());
                for (auto& [x, y] : s) v.push_back({x, y});
                ps.push_back(std::move(v));
            }
            auto c = std::make_unique<plot::PolyCollection>(
                std::move(ps));
            applyCollStyle(*c, kw);
            PyColl pc; pc.owned = std::move(c);
            return pc;
        },
        py::arg("verts"));
    colls.def("PatchCollection",
        [](const std::vector<PyPatch>& pats, const py::kwargs& kw) {
            std::vector<plot::Patch> ps;
            ps.reserve(pats.size());
            for (auto& pp : pats) ps.push_back(pp.spec);
            auto c = std::make_unique<plot::PatchCollection>(
                std::move(ps));
            applyCollStyle(*c, kw);
            PyColl pc; pc.owned = std::move(c);
            return pc;
        },
        py::arg("patches"));
    colls.def("CircleCollection",
        [](std::vector<float> radii,
           const std::vector<std::pair<float,float>>& offsets,
           const py::kwargs& kw) {
            std::vector<plot::Point2D> offs;
            for (auto& [x, y] : offsets) offs.push_back({x, y});
            auto c = std::make_unique<plot::CircleCollection>(
                std::move(radii), std::move(offs));
            applyCollStyle(*c, kw);
            PyColl pc; pc.owned = std::move(c);
            return pc;
        },
        py::arg("radii"), py::arg("offsets"));

    // ── mpl canvas Event (MouseEvent/KeyEvent fields) ────────────
    py::class_<plot::Event>(m, "Event")
        .def_readonly("x", &plot::Event::x)
        .def_readonly("y", &plot::Event::y)
        .def_readonly("button", &plot::Event::button)
        .def_readonly("dblclick", &plot::Event::dblclick)
        .def_readonly("step", &plot::Event::step)
        .def_readonly("key", &plot::Event::key)
        .def_readonly("shift", &plot::Event::shift)
        .def_readonly("ctrl", &plot::Event::ctrl)
        .def_readonly("alt", &plot::Event::alt)
        .def_property_readonly("xdata",
            [](const plot::Event& e) { return e.dataPos.x; })
        .def_property_readonly("ydata",
            [](const plot::Event& e) { return e.dataPos.y; })
        .def_property_readonly("name",
            [](const plot::Event& e) { return std::string(e.name()); });

    // mpl fig.canvas — mpl_connect / mpl_disconnect.
    py::class_<PyCanvas>(m, "Canvas")
        .def("mpl_connect",
             [](PyCanvas& c, const std::string& name, py::function cb) {
                 return c.owner->figure_.canvas().connect(
                     name, [cb](const plot::Event& e) {
                         py::gil_scoped_acquire gil;
                         cb(e);
                     });
             },
             py::arg("name"), py::arg("func"))
        .def("mpl_disconnect",
             [](PyCanvas& c, int cid) {
                 c.owner->figure_.canvas().disconnect(cid);
             },
             py::arg("cid"))
        /// Headless testing hook: synthesize and dispatch a raw event
        /// (button_press_event etc.) at canvas pixel (x, y).
        .def("dispatch",
             [](PyCanvas& c, const std::string& name, float x, float y,
                int button, const std::string& key, float step) {
                 plot::Event e;
                 static const std::unordered_map<std::string,
                     plot::Event::Type> kTypes = {
                     {"button_press_event", plot::Event::Type::ButtonPress},
                     {"button_release_event",
                      plot::Event::Type::ButtonRelease},
                     {"motion_notify_event",
                      plot::Event::Type::MotionNotify},
                     {"scroll_event", plot::Event::Type::Scroll},
                     {"key_press_event", plot::Event::Type::KeyPress},
                     {"key_release_event", plot::Event::Type::KeyRelease},
                     {"resize_event", plot::Event::Type::Resize},
                     {"draw_event", plot::Event::Type::Draw},
                     {"figure_enter_event",
                      plot::Event::Type::FigureEnter},
                     {"figure_leave_event",
                      plot::Event::Type::FigureLeave},
                     {"axes_enter_event", plot::Event::Type::AxesEnter},
                     {"axes_leave_event", plot::Event::Type::AxesLeave},
                 };
                 auto it = kTypes.find(name);
                 if (it == kTypes.end())
                     throw std::invalid_argument(
                         "unknown event name '" + name + "'");
                 e.type = it->second;
                 e.x = x; e.y = y; e.button = button;
                 e.key = key; e.step = step;
                 c.owner->figure_.layout(figExtentPx(c.owner));
                 c.owner->figure_.dispatch(e);
             },
             py::arg("name"), py::arg("x") = 0.0f, py::arg("y") = 0.0f,
             py::arg("button") = 0, py::arg("key") = "",
             py::arg("step") = 0.0f);

    // ── mpl Widget base ──────────────────────────────────────────
    py::class_<PyWidget>(m, "Widget")
        .def("set_active",
             [](PyWidget& w, bool a) {
                 w.w->active = a;
                 w.owner->figure_.markStale();
             },
             py::arg("active"))
        .def_property_readonly("active",
             [](const PyWidget& w) { return w.w->active; })
        /// mpl Slider.val (also RangeSlider → tuple, RadioButtons →
        /// label, CheckButtons → status list, TextBox → text).
        .def_property_readonly("val",
             [](const PyWidget& w) -> py::object {
                 if (auto* s = dynamic_cast<plot::Slider*>(w.w))
                     return py::float_(s->val());
                 if (auto* r = dynamic_cast<plot::RangeSlider*>(w.w)) {
                     auto [lo, hi] = r->val();
                     return py::make_tuple(lo, hi);
                 }
                 if (auto* rb = dynamic_cast<plot::RadioButtons*>(w.w))
                     return py::cast(rb->labels[size_t(
                         rb->activeIndex())]);
                 if (auto* tb = dynamic_cast<plot::TextBox*>(w.w))
                     return py::str(tb->text());
                 return py::none();
             })
        .def("set_val",
             [](PyWidget& w, const py::object& v) {
                 if (auto* s = dynamic_cast<plot::Slider*>(w.w)) {
                     s->setVal(v.cast<float>());
                 } else if (auto* r =
                                dynamic_cast<plot::RangeSlider*>(w.w)) {
                     auto t = v.cast<std::pair<float, float>>();
                     r->setVal(t.first, t.second);
                 } else if (auto* r =
                                dynamic_cast<plot::RadioButtons*>(w.w)) {
                     if (py::isinstance<py::str>(v)) {
                         auto s = v.cast<std::string>();
                         for (size_t i = 0; i < r->labels.size(); ++i)
                             if (r->labels[i] == s)
                                 r->setActive(int(i));
                     } else r->setActive(v.cast<int>());
                 }
                 w.owner->figure_.markStale();
             },
             py::arg("val"))
        /// mpl CheckButtons.set_active(i) / RadioButtons.set_active(i) —
        /// label strings accepted too.
        .def("set_active",
             [](PyWidget& w, const py::object& v) {
                 if (auto* c =
                         dynamic_cast<plot::CheckButtons*>(w.w)) {
                     if (py::isinstance<py::str>(v)) {
                         auto s = v.cast<std::string>();
                         for (size_t i = 0; i < c->labels.size(); ++i)
                             if (c->labels[i] == s) c->setActive(i);
                     } else c->setActive(v.cast<size_t>());
                 } else if (auto* r =
                                dynamic_cast<plot::RadioButtons*>(w.w)) {
                     if (py::isinstance<py::str>(v)) {
                         auto s = v.cast<std::string>();
                         for (size_t i = 0; i < r->labels.size(); ++i)
                             if (r->labels[i] == s)
                                 r->setActive(int(i));
                     } else r->setActive(v.cast<int>());
                 } else throw std::invalid_argument(
                     "set_active is only valid on CheckButtons/"
                     "RadioButtons");
                 w.owner->figure_.markStale();
             },
             py::arg("index"))
        /// mpl Slider.on_changed — fires on drag/set_val.
        .def("on_changed",
             [](PyWidget& w, py::function cb) {
                 w.callbacks.push_back(cb);
                 if (auto* s = dynamic_cast<plot::Slider*>(w.w))
                     s->onChanged = [cb](float v) {
                         py::gil_scoped_acquire gil; cb(v);
                     };
                 else if (auto* r =
                              dynamic_cast<plot::RangeSlider*>(w.w))
                     r->onChanged = [cb](float lo, float hi) {
                         py::gil_scoped_acquire gil;
                         cb(py::make_tuple(lo, hi));
                     };
                 else if (auto* c =
                              dynamic_cast<plot::CheckButtons*>(w.w))
                     c->onChanged = [cb](size_t i, bool on) {
                         py::gil_scoped_acquire gil; cb(i, on);
                     };
                 else if (auto* r =
                              dynamic_cast<plot::RadioButtons*>(w.w))
                     r->onChanged = [cb](size_t i) {
                         py::gil_scoped_acquire gil; cb(i);
                     };
                 else throw std::invalid_argument(
                     "on_changed is only valid on Slider/RangeSlider/"
                     "CheckButtons/RadioButtons");
             },
             py::arg("func"))
        /// mpl Button.on_clicked(event) /
        /// CheckButtons.on_clicked(label) / RadioButtons.on_clicked(label).
        .def("on_clicked",
             [](PyWidget& w, py::function cb) {
                 w.callbacks.push_back(cb);
                 if (auto* b = dynamic_cast<plot::Button*>(w.w))
                     // mpl passes the MouseEvent; the C++ onClick carries
                     // none — deliver None.
                     b->onClick = [cb]() {
                         py::gil_scoped_acquire gil; cb(py::none());
                     };
                 else if (auto* c =
                              dynamic_cast<plot::CheckButtons*>(w.w))
                     c->onChanged = [cb, labels = w.labels](size_t i,
                                                            bool) {
                         py::gil_scoped_acquire gil;
                         cb(i < labels.size() ? labels[i] : "");
                     };
                 else if (auto* r =
                              dynamic_cast<plot::RadioButtons*>(w.w))
                     r->onChanged = [cb, labels = w.labels](size_t i) {
                         py::gil_scoped_acquire gil;
                         cb(i < labels.size() ? labels[i] : "");
                     };
                 else throw std::invalid_argument(
                     "on_clicked is only valid on Button/CheckButtons/"
                     "RadioButtons");
             },
             py::arg("func"))
        /// mpl TextBox.on_submit.
        .def("on_submit",
             [](PyWidget& w, py::function cb) {
                 auto* t = dynamic_cast<plot::TextBox*>(w.w);
                 if (!t)
                     throw std::invalid_argument(
                         "on_submit is only valid on TextBox");
                 w.callbacks.push_back(cb);
                 t->onSubmit = [cb](std::string_view s) {
                     py::gil_scoped_acquire gil;
                     cb(std::string(s));
                 };
             },
             py::arg("func"))
        .def_property_readonly("rect",
             [](const PyWidget& w) {
                 return py::make_tuple(w.w->rect.x, w.w->rect.y,
                                       w.w->rect.width,
                                       w.w->rect.height);
             });

    // mpl matplotlib.widgets submodule.
    {
        auto wm = m.def_submodule("widgets");
        wm.def("Slider",
            [](PyAxes& a, const std::string& label, float valmin,
               float valmax, const py::object& valinit,
               const py::object& valstep) {
                auto r = axesRectPx(a);
                auto* s = a.owner->figure_.addWidget<plot::Slider>(
                    r, label, valmin, valmax);
                if (!valinit.is_none()) s->setVal(valinit.cast<float>());
                if (!valstep.is_none()) s->valstep = valstep.cast<float>();
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                return w;
            },
            py::arg("ax"), py::arg("label"), py::arg("valmin"),
            py::arg("valmax"), py::arg("valinit") = py::none(),
            py::arg("valstep") = py::none());
        wm.def("RangeSlider",
            [](PyAxes& a, const std::string& label, float valmin,
               float valmax, std::pair<float,float> valinit) {
                auto r = axesRectPx(a);
                auto* s = a.owner->figure_.addWidget<plot::RangeSlider>(
                    r, label, valmin, valmax, valinit.first,
                    valinit.second);
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                return w;
            },
            py::arg("ax"), py::arg("label"), py::arg("valmin"),
            py::arg("valmax"), py::arg("valinit"));
        wm.def("Button",
            [](PyAxes& a, const std::string& label) {
                auto r = axesRectPx(a);
                auto* b = a.owner->figure_.addWidget<plot::Button>(r,
                                                                   label);
                PyWidget w;
                w.owner = a.owner; w.w = b; w.ax = a;
                return w;
            },
            py::arg("ax"), py::arg("label"));
        wm.def("CheckButtons",
            [](PyAxes& a, const std::vector<std::string>& labels,
               const py::object& actives) {
                auto r = axesRectPx(a);
                std::vector<bool> checked;
                if (!actives.is_none())
                    checked = actives.cast<std::vector<bool>>();
                auto* c =
                    a.owner->figure_.addWidget<plot::CheckButtons>(
                        r, labels, std::move(checked));
                PyWidget w;
                w.owner = a.owner; w.w = c; w.ax = a;
                w.labels = labels;
                return w;
            },
            py::arg("ax"), py::arg("labels"),
            py::arg("actives") = py::none());
        wm.def("RadioButtons",
            [](PyAxes& a, const std::vector<std::string>& labels,
               int active) {
                auto r = axesRectPx(a);
                auto* rb =
                    a.owner->figure_.addWidget<plot::RadioButtons>(
                        r, labels, active);
                PyWidget w;
                w.owner = a.owner; w.w = rb; w.ax = a;
                w.labels = labels;
                return w;
            },
            py::arg("ax"), py::arg("labels"), py::arg("active") = 0);
        wm.def("TextBox",
            [](PyAxes& a, const std::string& label,
               const std::string& initial) {
                auto r = axesRectPx(a);
                auto* t = a.owner->figure_.addWidget<plot::TextBox>(
                    r, label, initial);
                PyWidget w;
                w.owner = a.owner; w.w = t; w.ax = a;
                return w;
            },
            py::arg("ax"), py::arg("label"),
            py::arg("initial") = "");
        wm.def("Cursor",
            [](PyAxes& a, const py::object& color, float linewidth) {
                auto* c = a.owner->figure_.addWidget<plot::Cursor>(a.ax);
                if (auto cc = parseColor(color); cc.a > 0) c->color = cc;
                if (linewidth > 0) c->lineWidth = linewidth;
                PyWidget w;
                w.owner = a.owner; w.w = c; w.ax = a;
                return w;
            },
            py::arg("ax"), py::arg("color") = py::none(),
            py::arg("linewidth") = 1.0f);
        wm.def("MultiCursor",
            [](std::shared_ptr<PyFigure> f, const std::vector<PyAxes>& axes,
               const py::object& color) {
                std::vector<plot::Axes*> as;
                for (auto& a : axes) as.push_back(a.ax);
                auto* c =
                    f->figure_.addWidget<plot::MultiCursor>(
                        std::move(as));
                if (auto cc = parseColor(color); cc.a > 0) c->color = cc;
                PyWidget w;
                w.owner = f; w.w = c;
                return w;
            },
            py::arg("fig"), py::arg("axes"),
            py::arg("color") = py::none());
        wm.def("SpanSelector",
            [](PyAxes& a, py::function onselect,
               const std::string& direction) {
                auto* s =
                    a.owner->figure_.addWidget<plot::SpanSelector>(
                        a.ax, direction);
                s->onSelect = [onselect](float lo, float hi) {
                    py::gil_scoped_acquire gil; onselect(lo, hi);
                };
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                w.callbacks.push_back(onselect);
                return w;
            },
            py::arg("ax"), py::arg("onselect"),
            py::arg("direction") = "x");
        wm.def("RectangleSelector",
            [](PyAxes& a, py::function onselect) {
                auto* s = a.owner->figure_.addWidget<
                    plot::RectangleSelector>(a.ax);
                s->onSelect = [onselect](float x0, float y0, float x1,
                                         float y1) {
                    py::gil_scoped_acquire gil;
                    onselect(x0, y0, x1, y1);
                };
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                w.callbacks.push_back(onselect);
                return w;
            },
            py::arg("ax"), py::arg("onselect"));
        wm.def("EllipseSelector",
            [](PyAxes& a, py::function onselect) {
                auto* s = a.owner->figure_.addWidget<
                    plot::EllipseSelector>(a.ax);
                s->onSelect = [onselect](float x0, float y0, float x1,
                                         float y1) {
                    py::gil_scoped_acquire gil;
                    onselect(x0, y0, x1, y1);
                };
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                w.callbacks.push_back(onselect);
                return w;
            },
            py::arg("ax"), py::arg("onselect"));
        wm.def("PolygonSelector",
            [](PyAxes& a, py::function onselect) {
                auto* s = a.owner->figure_.addWidget<
                    plot::PolygonSelector>(a.ax);
                s->onSelect = [onselect](std::span<const plot::Point2D> v) {
                    py::gil_scoped_acquire gil;
                    py::list out;
                    for (auto p : v) out.append(py::make_tuple(p.x, p.y));
                    onselect(out);
                };
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                w.callbacks.push_back(onselect);
                return w;
            },
            py::arg("ax"), py::arg("onselect"));
        wm.def("LassoSelector",
            [](PyAxes& a, py::function onselect) {
                auto* s = a.owner->figure_.addWidget<
                    plot::LassoSelector>(a.ax);
                s->onSelect = [onselect](std::span<const plot::Point2D> v) {
                    py::gil_scoped_acquire gil;
                    py::list out;
                    for (auto p : v) out.append(py::make_tuple(p.x, p.y));
                    onselect(out);
                };
                PyWidget w;
                w.owner = a.owner; w.w = s; w.ax = a;
                w.callbacks.push_back(onselect);
                return w;
            },
            py::arg("ax"), py::arg("onselect"));
        wm.def("SubplotTool",
            [](std::shared_ptr<PyFigure> f) {
                auto* t = f->figure_.addWidget<plot::SubplotTool>(
                    &f->figure_);
                PyWidget w;
                w.owner = f; w.w = t;
                return w;
            },
            py::arg("fig"));
    }

    py::class_<PyAxes>(m, "Axes")
        .def("plot",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& fmt, const py::object& color,
                float linewidth, const py::object& marker,
                const py::object& linestyle, float markersize,
                float alpha, const std::string& label) {
                 // mpl plot(y), plot(y, fmt), plot(x, y), plot(x,y,fmt).
                 if (y.is_none())
                     return axesPlot(a, implicitX(x), x, fmt, color,
                                     linewidth, marker, linestyle,
                                     markersize, alpha, label);
                 if (py::isinstance<py::str>(y) && fmt.is_none())
                     return axesPlot(a, implicitX(x), x, y, color,
                                     linewidth, marker, linestyle,
                                     markersize, alpha, label);
                 return axesPlot(a, x, y, fmt, color, linewidth, marker,
                                 linestyle, markersize, alpha, label);
             },
             py::arg("x"), py::arg("y") = py::none(),
             py::arg("fmt") = py::none(), py::arg("color") = py::none(),
             py::arg("linewidth") = 1.5f, py::arg("marker") = py::none(),
             py::arg("linestyle") = py::none(),
             py::arg("markersize") = -1.0f, py::arg("alpha") = -1.0f,
             py::arg("label") = "")
        .def("scatter", &axesScatter,
             py::arg("x"), py::arg("y"), py::arg("color") = py::none(),
             py::arg("s") = 8.0f, py::arg("label") = "")
        .def("bar",
             [](PyAxes& a, const py::object& x,
                const py::object& h, const py::object& color,
                float width, const std::string& label) {
                 auto hv = toFloats(h);
                 plot::BarData b;
                 b.heights = hv;
                 if (isStringSeq(x)) {
                     // mpl categorical bar: string x → category ticks.
                     std::vector<std::string> cats;
                     for (auto item : py::reinterpret_borrow<py::sequence>(x))
                         cats.push_back(item.cast<std::string>());
                     a.ax->setXCategories(cats);
                 } else {
                     auto xv = toFloats(x);
                     if (xv.size() == hv.size())
                         for (float v : xv) b.labels.push_back(std::format("{}", v));
                 }
                 b.width = width;
                 if (auto c = parseColor(color); c.a > 0)
                     b.colors.assign(hv.size(), c);
                 a.ax->addPlot(std::make_unique<plot::BarPlot>(std::move(b)));
                 PyBarContainer bc;
                 bc.owner = a.owner; bc.heights = hv;
                 bc.x.resize(hv.size());
                 std::iota(bc.x.begin(), bc.x.end(), 0.0f);
                 return bc;
             },
             py::arg("x"), py::arg("height"), py::arg("color") = py::none(),
             py::arg("width") = 0.8f, py::arg("label") = "")
        .def("barh",
             [](PyAxes& a, const py::object& y,
                const py::object& w, const py::object& color,
                float height, const std::string& label) {
                 auto wv = toFloats(w);
                 plot::BarData b;
                 b.heights = wv;
                 b.horizontal = true;
                 if (isStringSeq(y)) {
                     std::vector<std::string> cats;
                     for (auto item : py::reinterpret_borrow<py::sequence>(y))
                         cats.push_back(item.cast<std::string>());
                     a.ax->setYCategories(cats);
                 } else {
                     auto yv = toFloats(y);
                     if (yv.size() == wv.size())
                         for (float v : yv) b.labels.push_back(std::format("{}", v));
                 }
                 b.width = height;
                 if (auto c = parseColor(color); c.a > 0)
                     b.colors.assign(wv.size(), c);
                 a.ax->addPlot(std::make_unique<plot::BarPlot>(std::move(b)));
                 PyBarContainer bc;
                 bc.owner = a.owner; bc.heights = wv;
                 bc.horizontal = true;
                 bc.x.resize(wv.size());
                 std::iota(bc.x.begin(), bc.x.end(), 0.0f);
                 return bc;
             },
             py::arg("y"), py::arg("width"), py::arg("color") = py::none(),
             py::arg("height") = 0.8f, py::arg("label") = "")
        .def("imshow",
             [](PyAxes& a, const py::object& values,
                const std::string& cmapName) {
                 a.ax->addPlot(std::make_unique<plot::HeatmapPlot>(
                     toGrid2D(values), cmapByName(cmapName)));
             },
             py::arg("values"), py::arg("cmap") = "viridis")
        .def("hist",
             [](PyAxes& a, const py::object& samples, int bins,
                const py::object& color, const std::string& label) {
                 plot::HistConfig cfg;
                 cfg.bins = plot::HistBinMethod::Fixed;
                 cfg.binCount = bins;
                 cfg.label = label;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 if (py::isinstance<py::sequence>(samples)
                     && py::len(samples) > 0
                     && py::isinstance<py::sequence>(samples.attr("__getitem__")(0))) {
                     // list of datasets
                     std::vector<std::vector<float>> ds;
                     for (auto item : py::reinterpret_borrow<py::sequence>(samples))
                         ds.push_back(toFloats(py::reinterpret_borrow<py::object>(item)));
                     a.ax->addPlot(std::make_unique<plot::HistPlot>(std::move(ds), cfg));
                 } else {
                     a.ax->addPlot(std::make_unique<plot::HistPlot>(toFloats(samples), cfg));
                 }
             },
             py::arg("samples"), py::arg("bins") = 10,
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("errorbar",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& yerr, const py::object& xerr,
                const py::object& color, const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y);
                 plot::ErrorbarConfig cfg;
                 cfg.label = label;
                 if (!yerr.is_none()) cfg.yerr = toFloats(yerr);
                 if (!xerr.is_none()) cfg.xerr = toFloats(xerr);
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 a.ax->addPlot(std::make_unique<plot::ErrorbarPlot>(
                     std::move(xv), std::move(yv), std::move(cfg)));
             },
             py::arg("x"), py::arg("y"),
             py::arg("yerr") = py::none(), py::arg("xerr") = py::none(),
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("stem",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& color, const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y);
                 plot::StemConfig cfg;
                 cfg.label = label;
                 if (auto c = parseColor(color); c.a > 0) {
                     cfg.lineColor = c; cfg.markerColor = c;
                 }
                 a.ax->addPlot(std::make_unique<plot::StemPlot>(
                     std::move(xv), std::move(yv), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("color") = py::none(),
             py::arg("label") = "")
        .def("step",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const std::string& where, const py::object& color,
                const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y);
                 auto w = where == "post" ? plot::StepWhere::Post
                        : where == "mid"  ? plot::StepWhere::Mid
                                          : plot::StepWhere::Pre;
                 auto c = parseColor(color);
                 auto p = std::make_unique<plot::StepPlot>(
                     std::move(xv), std::move(yv), w,
                     c.a > 0 ? c : plot::Color::blue());
                 p->setLabel(label);
                 a.ax->addPlot(std::move(p));
             },
             py::arg("x"), py::arg("y"), py::arg("where") = "pre",
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("fill_between",
             [](PyAxes& a, const py::object& x, const py::object& y1,
                const py::object& y2, const py::object& color) {
                 auto c = parseColor(color);
                 if (c.a == 0) c = plot::Color::fromRgba8(31, 119, 180, 128);
                 if (y2.is_none())
                     a.ax->addPlot(std::make_unique<plot::FillBetweenPlot>(
                         toFloats(x), toFloats(y1), c));
                 else
                     a.ax->addPlot(std::make_unique<plot::FillBetweenPlot>(
                         toFloats(x), toFloats(y1), toFloats(y2), c));
             },
             py::arg("x"), py::arg("y1"), py::arg("y2") = py::none(),
             py::arg("color") = py::none())
        .def("contour",
             [](PyAxes& a, const py::object& values,
                const py::object& levels, const std::string& cmapName,
                bool clabel) {
                 plot::ContourConfig cfg;
                 cfg.cmap = &cmapByName(cmapName);
                 cfg.clabel = clabel;
                 if (py::isinstance<py::int_>(levels))
                     cfg.numLevels = levels.cast<int>();
                 else if (!levels.is_none())
                     cfg.levels = toFloats(levels);
                 a.ax->addPlot(std::make_unique<plot::ContourPlot>(
                     toGrid2D(values), std::move(cfg)));
             },
             py::arg("values"), py::arg("levels") = py::none(),
             py::arg("cmap") = "viridis", py::arg("clabel") = false)
        .def("contourf",
             [](PyAxes& a, const py::object& values,
                const py::object& levels, const std::string& cmapName) {
                 plot::ContourConfig cfg;
                 cfg.cmap = &cmapByName(cmapName);
                 if (py::isinstance<py::int_>(levels))
                     cfg.numLevels = levels.cast<int>();
                 else if (!levels.is_none())
                     cfg.levels = toFloats(levels);
                 a.ax->addPlot(std::make_unique<plot::ContourfPlot>(
                     toGrid2D(values), std::move(cfg)));
             },
             py::arg("values"), py::arg("levels") = py::none(),
             py::arg("cmap") = "viridis")
        .def("boxplot",
             [](PyAxes& a, const py::object& groups,
                const py::object& labels) {
                 plot::BoxPlotConfig cfg;
                 std::vector<std::vector<float>> ds;
                 for (auto item : py::reinterpret_borrow<py::sequence>(groups))
                     ds.push_back(toFloats(py::reinterpret_borrow<py::object>(item)));
                 if (!labels.is_none())
                     cfg.labels = labels.cast<std::vector<std::string>>();
                 a.ax->addPlot(std::make_unique<plot::BoxPlot>(std::move(ds), cfg));
             },
             py::arg("groups"), py::arg("labels") = py::none())
        .def("bxp",
             [](PyAxes& a, const py::object& stats,
                const py::object& positions, const py::object& widths,
                bool showbox, bool showcaps, bool showfliers,
                bool showmeans, bool meanline, bool shownotches,
                bool patch_artist, bool manage_ticks) {
                 std::vector<plot::BxpStats> v;
                 for (auto item : py::reinterpret_borrow<py::sequence>(stats)) {
                     py::dict d = py::cast<py::dict>(item);
                     plot::BxpStats b;
                     b.med = d["med"].cast<float>();
                     b.q1 = d["q1"].cast<float>();
                     b.q3 = d["q3"].cast<float>();
                     b.whislo = d["whislo"].cast<float>();
                     b.whishi = d["whishi"].cast<float>();
                     if (d.contains("fliers"))
                         b.fliers = d["fliers"].cast<std::vector<float>>();
                     if (d.contains("mean"))
                         b.mean = d["mean"].cast<float>();
                     if (d.contains("cilo"))
                         b.cilo = d["cilo"].cast<float>();
                     if (d.contains("cihi"))
                         b.cihi = d["cihi"].cast<float>();
                     if (d.contains("label"))
                         b.label = d["label"].cast<std::string>();
                     v.push_back(std::move(b));
                 }
                 plot::BxpConfig cfg;
                 if (!positions.is_none())
                     cfg.positions = positions.cast<std::vector<float>>();
                 if (!widths.is_none())
                     cfg.widths = widths.cast<std::vector<float>>();
                 cfg.showbox = showbox;
                 cfg.showcaps = showcaps;
                 cfg.showfliers = showfliers;
                 cfg.showmeans = showmeans;
                 cfg.meanline = meanline;
                 cfg.shownotches = shownotches;
                 cfg.patchArtist = patch_artist;
                 cfg.manageTicks = manage_ticks;
                 a.ax->bxp(std::move(v), std::move(cfg));
             },
             py::arg("stats"), py::arg("positions") = py::none(),
             py::arg("widths") = py::none(), py::arg("showbox") = true,
             py::arg("showcaps") = true, py::arg("showfliers") = true,
             py::arg("showmeans") = false, py::arg("meanline") = false,
             py::arg("shownotches") = false, py::arg("patch_artist") = false,
             py::arg("manage_ticks") = true)
        .def("pcolormesh",
             [](PyAxes& a, const py::object& values,
                const std::string& cmapName, const std::string& shading) {
                 auto g = toGrid2D(values);
                 // Flat shading on a unit grid: edges 0..W, 0..H.
                 std::vector<float> x(g.width + 1), y(g.height + 1);
                 for (uint32_t i = 0; i <= g.width; ++i) x[i] = float(i);
                 for (uint32_t j = 0; j <= g.height; ++j) y[j] = float(j);
                 plot::PcolormeshConfig cfg;
                 cfg.cmap = &cmapByName(cmapName);
                 if (shading == "gouraud") cfg.shading = plot::PcmShading::Gouraud;
                 a.ax->addPlot(std::make_unique<plot::PcolormeshPlot>(
                     std::move(x), std::move(y), std::move(g.values),
                     g.width, g.height, std::move(cfg)));
             },
             py::arg("values"), py::arg("cmap") = "viridis",
             py::arg("shading") = "flat")
        .def("set_xlim", [](PyAxes& a, float lo, float hi) { a.ax->setXlim(lo, hi); })
        .def("set_ylim", [](PyAxes& a, float lo, float hi) { a.ax->setYlim(lo, hi); })
        .def("set_xscale", [](PyAxes& a, std::string s) { a.ax->setXscale(s); })
        .def("set_yscale", [](PyAxes& a, std::string s) { a.ax->setYscale(s); })
        .def("set_xlabel", [](PyAxes& a, std::string s) {
                 a.ax->style().xAxis.label = std::move(s);
             })
        .def("set_ylabel", [](PyAxes& a, std::string s) {
                 a.ax->style().yAxis.label = std::move(s);
             })
        .def("set_title", [](PyAxes& a, std::string s) { a.ax->setTitle(std::move(s)); })
        .def_property_readonly(
            "xaxis",
            [](PyAxes& a) { return PyAxis{a.owner, a.ax, true}; },
            "mpl ax.xaxis — locator/formatter accessors.")
        .def_property_readonly(
            "yaxis",
            [](PyAxes& a) { return PyAxis{a.owner, a.ax, false}; },
            "mpl ax.yaxis — locator/formatter accessors.")
        .def("grid",
             [](PyAxes& a, bool on, const std::string& which,
                const std::string& axis) {
                 a.ax->grid(on, which, axis);
             },
             py::arg("on") = true, py::arg("which") = "major",
             py::arg("axis") = "both")
        .def("legend",
             [](PyAxes& a, const py::object& loc, int ncols,
                const std::string& title, float fontsize,
                const py::object& frameon, const py::object& fancybox,
                const py::object& shadow, float framealpha) {
                 auto& ls = a.ax->legend();
                 if (!loc.is_none()) ls.location = loc.cast<std::string>();
                 ls.ncols = ncols;
                 ls.title = title;
                 if (fontsize > 0.0f) ls.font.size = fontsize;
                 if (!frameon.is_none()) ls.frameOn = frameon.cast<bool>();
                 if (!fancybox.is_none()) ls.fancyBox = fancybox.cast<bool>();
                 if (!shadow.is_none()) ls.shadow = shadow.cast<bool>();
                 if (framealpha >= 0.0f) ls.frameAlpha = framealpha;
             },
             py::arg("loc") = py::none(), py::arg("ncols") = 1,
             py::arg("title") = "", py::arg("fontsize") = -1.0f,
             py::arg("frameon") = py::none(),
             py::arg("fancybox") = py::none(),
             py::arg("shadow") = py::none(),
             py::arg("framealpha") = -1.0f)
        // ── mpl axis() / aspect / margins / inversion ──
        .def("axis",
             [](PyAxes& a, const py::object& arg) {
                 if (arg.is_none()) return;
                 if (py::isinstance<py::str>(arg)) {
                     auto s = arg.cast<std::string>();
                     if (s == "off") {
                         a.ax->style().xAxis.visible = false;
                         a.ax->style().yAxis.visible = false;
                     } else if (s == "on") {
                         a.ax->style().xAxis.visible = true;
                         a.ax->style().yAxis.visible = true;
                     } else if (s == "equal" || s == "square") {
                         a.ax->setAspect(plot::AspectMode::Equal);
                     } else if (s == "auto") {
                         a.ax->setAspect(plot::AspectMode::Auto);
                     } else if (s == "scaled" || s == "image") {
                         // equal aspect, adjustable="datalim".
                         a.ax->setAspect(plot::AspectMode::Equal);
                         a.ax->setAdjustable(plot::Adjustable::DataLim);
                     } else if (s == "tight") {
                         a.ax->margins(0.0f);
                     } else
                         throw std::invalid_argument(
                             "unknown axis() arg '" + s + "'");
                     a.ax->touch();
                     return;
                 }
                 auto lims = arg.cast<std::vector<float>>();
                 if (lims.size() != 4)
                     throw std::invalid_argument(
                         "axis([xmin, xmax, ymin, ymax])");
                 a.ax->setXlim(lims[0], lims[1]);
                 a.ax->setYlim(lims[2], lims[3]);
             },
             py::arg("arg") = py::none(),
             "mpl ax.axis(): 'off'/'on'/'equal'/'scaled'/'tight'/'auto' "
             "or [xmin, xmax, ymin, ymax].")
        .def("set_aspect",
             [](PyAxes& a, const py::object& aspect) {
                 if (py::isinstance<py::str>(aspect)) {
                     auto s = aspect.cast<std::string>();
                     a.ax->setAspect(s == "equal" ? plot::AspectMode::Equal
                                                  : plot::AspectMode::Auto);
                 } else {
                     // Numeric aspect — only equal/auto modes exist.
                     a.ax->setAspect(aspect.cast<float>() == 1.0f
                                         ? plot::AspectMode::Equal
                                         : plot::AspectMode::Auto);
                 }
             },
             py::arg("aspect"))
        .def("margins",
             [](PyAxes& a, float x, float y) { a.ax->margins(x, y); },
             py::arg("x"), py::arg("y") = -1.0f)
        .def("invert_xaxis", [](PyAxes& a) { a.ax->invertXAxis(); })
        .def("invert_yaxis", [](PyAxes& a) { a.ax->invertYAxis(); })
        .def("set_axis_off",
             [](PyAxes& a) {
                 a.ax->style().xAxis.visible = false;
                 a.ax->style().yAxis.visible = false;
                 a.ax->touch();
             })
        .def("set_axis_on",
             [](PyAxes& a) {
                 a.ax->style().xAxis.visible = true;
                 a.ax->style().yAxis.visible = true;
                 a.ax->touch();
             })
        // ── Ticks: locators, formatters, params ──
        .def("set_xticks",
             [](PyAxes& a, const py::object& ticks,
                const py::object& labels) {
                 a.ax->setXLocator(std::make_shared<plot::FixedLocator>(
                     toFloats(ticks)));
                 if (!labels.is_none())
                     a.ax->setXFormatter(
                         std::make_shared<plot::FixedFormatter>(
                             labels.cast<std::vector<std::string>>()));
             },
             py::arg("ticks"), py::arg("labels") = py::none())
        .def("set_yticks",
             [](PyAxes& a, const py::object& ticks,
                const py::object& labels) {
                 a.ax->setYLocator(std::make_shared<plot::FixedLocator>(
                     toFloats(ticks)));
                 if (!labels.is_none())
                     a.ax->setYFormatter(
                         std::make_shared<plot::FixedFormatter>(
                             labels.cast<std::vector<std::string>>()));
             },
             py::arg("ticks"), py::arg("labels") = py::none())
        .def("set_xticklabels",
             [](PyAxes& a, const std::vector<std::string>& labels) {
                 a.ax->setXFormatter(
                     std::make_shared<plot::FixedFormatter>(labels));
             },
             py::arg("labels"))
        .def("set_yticklabels",
             [](PyAxes& a, const std::vector<std::string>& labels) {
                 a.ax->setYFormatter(
                     std::make_shared<plot::FixedFormatter>(labels));
             },
             py::arg("labels"))
        .def("tick_params",
             [](PyAxes& a, const std::string& axis,
                const py::kwargs& kw) {
                 std::string direction;
                 float majorSize = -1, minorSize = -1,
                       majorWidth = -1, minorWidth = -1;
                 auto& xs = a.ax->style().xAxis;
                 auto& ys = a.ax->style().yAxis;
                 bool doX = axis == "x" || axis == "both";
                 bool doY = axis == "y" || axis == "both";
                 for (auto& [k, v] : kw) {
                     auto key = k.cast<std::string>();
                     if (key == "direction")
                         direction = v.cast<std::string>();
                     else if (key == "length")
                         majorSize = v.cast<float>();
                     else if (key == "minor_length")
                         minorSize = v.cast<float>();
                     else if (key == "width")
                         majorWidth = v.cast<float>();
                     else if (key == "minor_width")
                         minorWidth = v.cast<float>();
                     else if (key == "labelrotation") {
                         float r = v.cast<float>() * float(M_PI) / 180.0f;
                         if (doX) xs.tickFont.rotation = r;
                         if (doY) ys.tickFont.rotation = r;
                     } else if (key == "labelsize") {
                         float s = v.cast<float>();
                         if (doX) xs.tickFont.size = s;
                         if (doY) ys.tickFont.size = s;
                     } else if (key == "labelcolor" || key == "color") {
                         auto c = parseColor(v.cast<py::object>());
                         if (doX) xs.labelColor = c;
                         if (doY) ys.labelColor = c;
                     } else if (key == "labeltop")
                         a.ax->setXTicksTop(v.cast<bool>());
                     else if (key == "labelbottom")
                         a.ax->setXTicksTop(!v.cast<bool>());
                     else if (key == "labelright")
                         a.ax->setYTicksRight(v.cast<bool>());
                     else if (key == "labelleft")
                         a.ax->setYTicksRight(!v.cast<bool>());
                     else if (key == "top")
                         a.ax->setXTickMarksTop(v.cast<bool>());
                     else if (key == "right")
                         a.ax->setYTickMarksRight(v.cast<bool>());
                 }
                 a.ax->tickParams(axis, direction, majorSize, minorSize,
                                  majorWidth, minorWidth);
             },
             py::arg("axis") = "both")
        .def("ticklabel_format",
             [](PyAxes& a, const std::string& axis,
                const std::string& style,
                std::pair<int, int> scilimits, bool useOffset,
                bool useMathText) {
                 a.ax->ticklabelFormat(axis, style, scilimits, useOffset,
                                       useMathText);
             },
             py::arg("axis") = "both", py::arg("style") = "",
             py::arg("scilimits") = std::pair<int, int>{-5, 6},
             py::arg("useOffset") = true, py::arg("useMathText") = false)
        .def("minorticks_on", [](PyAxes& a) { a.ax->minorticksOn(); })
        .def("minorticks_off", [](PyAxes& a) { a.ax->minorticksOff(); })
        // ── mpl date axes ──
        .def("xaxis_date",
             [](PyAxes& a, const py::object& tz) { a.ax->xaxis_date(); },
             py::arg("tz") = py::none())
        .def("yaxis_date",
             [](PyAxes& a, const py::object& tz) { a.ax->yaxis_date(); },
             py::arg("tz") = py::none())
        .def("plot_date",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const std::string& fmt, bool xdate, bool ydate,
                const py::object& color) {
                 bool xd = false, yd = false;
                 auto xv = toFloats(x, &xd), yv = toFloats(y, &yd);
                 if (xdate || xd) a.ax->xaxis_date();
                 if (ydate || yd) a.ax->yaxis_date();
                 // mpl plot_date defaults to markers-only.
                 plot::Series2D s;
                 s.points.reserve(xv.size());
                 for (size_t i = 0; i < xv.size(); ++i)
                     s.points.push_back({xv[i],
                                         i < yv.size() ? yv[i] : 0.0f});
                 if (auto c = parseColor(color); c.a > 0) s.color = c;
                 applyFmt(s, fmt);
                 a.ax->addPlot(std::make_unique<plot::LinePlot>(
                     std::move(s)));
                 a.ax->touch();
             },
             py::arg("x"), py::arg("y"), py::arg("fmt") = "o",
             py::arg("xdate") = true, py::arg("ydate") = false,
             py::arg("color") = py::none())
        // ── text / annotate (mpl ax.text / ax.annotate) ──
        .def("text",
             [](PyAxes& a, float x, float y, const std::string& s,
                const std::string& transform, const py::object& color,
                float fontsize, const std::string& ha,
                const std::string& va, float rotation) {
                 auto* t = a.ax->text(x, y, s, toCoordSystem(transform));
                 if (auto c = parseColor(color); c.a > 0) t->color = c;
                 if (fontsize > 0.0f) t->fontSize = fontsize / 16.0f;
                 if (ha == "center") t->halign = plot::HAlign::Center;
                 else if (ha == "right") t->halign = plot::HAlign::Right;
                 else t->halign = plot::HAlign::Left;
                 if (va == "center") t->valign = plot::VAlign::Center;
                 else if (va == "top") t->valign = plot::VAlign::Top;
                 else if (va == "bottom") t->valign = plot::VAlign::Bottom;
                 if (rotation != 0.0f)
                     t->rotation = rotation * float(M_PI) / 180.0f;
             },
             py::arg("x"), py::arg("y"), py::arg("s"),
             py::arg("transform") = "data", py::arg("color") = py::none(),
             py::arg("fontsize") = -1.0f, py::arg("ha") = "left",
             py::arg("va") = "baseline", py::arg("rotation") = 0.0f)
        .def("annotate",
             [](PyAxes& a, const std::string& s,
                std::pair<float, float> xy,
                const py::object& xytext, const std::string& xycoords,
                const std::string& textcoords, const py::object& arrowprops,
                const py::object& color, float fontsize) {
                 auto cs = toCoordSystem(xycoords);
                 plot::Annotation* an;
                 if (xytext.is_none()) {
                     an = a.ax->annotate(xy.first, xy.second, xy.first,
                                         xy.second, s, cs);
                 } else if (py::isinstance<py::sequence>(xytext) &&
                            !py::isinstance<py::str>(xytext)) {
                     auto p = xytext.cast<std::pair<float, float>>();
                     an = a.ax->annotate(xy.first, xy.second, p.first,
                                         p.second, s, cs);
                 } else
                     throw std::invalid_argument(
                         "annotate: xytext must be an (x, y) pair");
                 an->xyTextCoords = toCoordSystem(textcoords);
                 if (an->xyTextCoords == plot::CoordSystem::OffsetPoints) {
                     an->textOffsetX = an->xyText[0];
                     an->textOffsetY = an->xyText[1];
                     an->xyText[0] = an->xyText[1] = 0.0f;
                 }
                 if (auto c = parseColor(color); c.a > 0) an->color = c;
                 if (fontsize > 0.0f) an->fontSize = fontsize / 16.0f;
                 if (!arrowprops.is_none()) {
                     auto d = py::cast<py::dict>(arrowprops);
                     if (d.contains("arrowstyle"))
                         an->arrowSpec = plot::parseArrowStyle(
                             d["arrowstyle"].cast<std::string>());
                     if (d.contains("color"))
                         if (auto c2 = parseColor(
                                 py::reinterpret_borrow<py::object>(
                                     d["color"]));
                             c2.a > 0)
                             an->arrowColor = c2;
                     if (d.contains("connectionstyle"))
                         an->connection = plot::parseConnectionStyle(
                             d["connectionstyle"].cast<std::string>());
                     if (d.contains("lw") || d.contains("linewidth")) {
                         auto k = d.contains("lw") ? "lw" : "linewidth";
                         an->arrowWidth = d[k].cast<float>();
                     }
                 }
             },
             py::arg("text"), py::arg("xy"), py::arg("xytext") = py::none(),
             py::arg("xycoords") = "data", py::arg("textcoords") = "data",
             py::arg("arrowprops") = py::none(),
             py::arg("color") = py::none(), py::arg("fontsize") = -1.0f)
        // ── 3D (mpl Axes3D) ──
        .def("view_init",
             [](PyAxes& a, float elev, float azim, float roll) {
                 viewInit3D(a, elev, azim, roll);
             },
             py::arg("elev") = 30.0f, py::arg("azim") = -60.0f,
             py::arg("roll") = 0.0f)
        .def("set_zlim",
             [](PyAxes& a, float lo, float hi) {
                 for (auto& p : a.ax->plots()) {
                     if (auto* box =
                             dynamic_cast<plot::Axes3DPlot*>(p.get())) {
                         auto v = box->range();
                         v.z = {lo, hi};
                         box->setRange(v);
                     }
                     if (auto* cam = p->camera3D()) {
                         cam->dataMin.z = lo;
                         cam->dataMax.z = hi;
                     }
                 }
                 a.ax->touch();
             },
             py::arg("lo"), py::arg("hi"))
        .def("set_zlabel",
             [](PyAxes& a, const std::string& s) {
                 for (auto& p : a.ax->plots())
                     if (auto* box =
                             dynamic_cast<plot::Axes3DPlot*>(p.get()))
                         box->setZLabel(s);
                 a.ax->touch();
             },
             py::arg("label"))
        .def("set_projection", [](PyAxes& a, std::string p) {
                 a.ax->setProjection(p);
             })
        // ── mpl polar axes API ──
        .def("set_rgrids",
             [](PyAxes& a, const py::object& radii) {
                 a.ax->setRgrids(toFloats(radii));
             },
             py::arg("radii"))
        .def("set_thetagrids",
             [](PyAxes& a, const py::object& angles) {
                 a.ax->setThetagrids(toFloats(angles));
             },
             py::arg("angles"))
        .def("set_theta_zero_location",
             [](PyAxes& a, const std::string& loc) {
                 a.ax->setThetaZeroLocation(loc);
             },
             py::arg("loc"))
        .def("set_theta_direction",
             [](PyAxes& a, int dir) { a.ax->setThetaDirection(dir); },
             py::arg("dir"))
        .def("set_theta_offset",
             [](PyAxes& a, float rad) { a.ax->setThetaOffset(rad); },
             py::arg("radians"))
        .def("set_rlabel_position",
             [](PyAxes& a, float deg) { a.ax->setRlabelPosition(deg); },
             py::arg("position"))
        .def("set_rmin",
             [](PyAxes& a, float v) { a.ax->setRmin(v); },
             py::arg("r"))
        .def("set_rmax",
             [](PyAxes& a, float v) { a.ax->setRmax(v); },
             py::arg("r"))
        .def("set_rorigin",
             [](PyAxes& a, float v) { a.ax->setRorigin(v); },
             py::arg("r"))
        // mpl ax.spines['top'].set_visible(False) — dict-like proxy.
        .def_property_readonly("spines",
             [](PyAxes& a) {
                 py::dict d;
                 for (const char* s : {"left", "right", "bottom", "top"})
                     d[s] = PySpine{a.owner, a.ax, s};
                 return d;
             })
        // mpl ax.set_prop_cycle: cycler object or key=[values] kwargs.
        .def("set_prop_cycle",
             [](PyAxes& a, const py::args& args,
                const py::kwargs& kw) {
                 if (args.size() == 1 &&
                     py::isinstance<plot::Cycler>(args[0])) {
                     a.ax->setPropCycle(
                         args[0].cast<plot::Cycler>());
                     return;
                 }
                 a.ax->setPropCycle(makeCycler(args, kw));
             })
        // ── Reference lines / spans (mpl ax.axhline etc.) ──
        .def("axhline",
             [](PyAxes& a, float y, const py::object& color, float lw) {
                 a.ax->axhline(y, lineColor(color, plot::Color::black()), lw);
             },
             py::arg("y") = 0.0f, py::arg("color") = py::none(),
             py::arg("linewidth") = 1.0f)
        .def("axvline",
             [](PyAxes& a, float x, const py::object& color, float lw) {
                 a.ax->axvline(x, lineColor(color, plot::Color::black()), lw);
             },
             py::arg("x") = 0.0f, py::arg("color") = py::none(),
             py::arg("linewidth") = 1.0f)
        .def("axline",
             [](PyAxes& a, std::pair<float, float> xy1,
                const py::object& xy2, const py::object& slope,
                const py::object& color, float lw) {
                 auto c = lineColor(color, plot::Color::black());
                 if (!slope.is_none())
                     a.ax->axline({xy1.first, xy1.second},
                                  slope.cast<float>(), c, lw);
                 else if (!xy2.is_none()) {
                     auto p = xy2.cast<std::pair<float, float>>();
                     a.ax->axline({xy1.first, xy1.second},
                                  {p.first, p.second}, c, lw);
                 } else
                     throw std::invalid_argument(
                         "axline needs xy2 or slope=");
             },
             py::arg("xy1"), py::arg("xy2") = py::none(),
             py::arg("slope") = py::none(),
             py::arg("color") = py::none(), py::arg("linewidth") = 1.0f)
        .def("axhspan",
             [](PyAxes& a, float ymin, float ymax, const py::object& color) {
                 a.ax->axhspan(ymin, ymax,
                               lineColor(color, plot::Color::fromRgba8(200, 200, 200, 128)));
             },
             py::arg("ymin"), py::arg("ymax"),
             py::arg("color") = py::none())
        .def("axvspan",
             [](PyAxes& a, float xmin, float xmax, const py::object& color) {
                 a.ax->axvspan(xmin, xmax,
                               lineColor(color, plot::Color::fromRgba8(200, 200, 200, 128)));
             },
             py::arg("xmin"), py::arg("xmax"),
             py::arg("color") = py::none())
        .def("hlines",
             [](PyAxes& a, const py::object& y, float xmin, float xmax,
                const py::object& color, float lw) {
                 a.ax->hlines(toFloatList(y), xmin, xmax,
                              lineColor(color, plot::Color::black()), lw);
             },
             py::arg("y"), py::arg("xmin"), py::arg("xmax"),
             py::arg("color") = py::none(), py::arg("linewidth") = 1.0f)
        .def("vlines",
             [](PyAxes& a, const py::object& x, float ymin, float ymax,
                const py::object& color, float lw) {
                 a.ax->vlines(toFloatList(x), ymin, ymax,
                              lineColor(color, plot::Color::black()), lw);
             },
             py::arg("x"), py::arg("ymin"), py::arg("ymax"),
             py::arg("color") = py::none(), py::arg("linewidth") = 1.0f)
        // ── Remaining mpl plot types ──
        .def("pie",
             [](PyAxes& a, const py::object& x, const py::object& labels,
                const py::object& colors, const py::object& explode,
                bool donut, float innerRadius) {
                 plot::PieData d;
                 d.values = toFloats(x);
                 if (!labels.is_none())
                     d.labels =
                         labels.cast<std::vector<std::string>>();
                 d.colors = toColors(colors, plot::Color::black());
                 if (!explode.is_none()) {
                     auto e = toFloatList(explode);
                     d.explode =
                         e.empty() ? 0.0f : *std::ranges::max_element(e);
                 }
                 d.donut = donut;
                 d.innerRadius = innerRadius;
                 a.ax->addPlot(
                     std::make_unique<plot::PiePlot>(std::move(d)));
             },
             py::arg("x"), py::arg("labels") = py::none(),
             py::arg("colors") = py::none(),
             py::arg("explode") = py::none(), py::arg("donut") = false,
             py::arg("inner_radius") = 0.0f)
        .def("stackplot",
             [](PyAxes& a, const py::object& x, const py::object& ys,
                const py::object& colors, const py::object& labels) {
                 std::vector<std::string> labs;
                 if (!labels.is_none())
                     labs = labels.cast<std::vector<std::string>>();
                 a.ax->addPlot(std::make_unique<plot::StackPlot>(
                     toFloats(x), toRows(ys),
                     toColors(colors, plot::Color::black()),
                     std::move(labs)));
             },
             py::arg("x"), py::arg("ys"), py::arg("colors") = py::none(),
             py::arg("labels") = py::none())
        .def("hexbin",
             [](PyAxes& a, const py::object& x, const py::object& y,
                int gridsize, const py::object& cmap, int mincnt) {
                 plot::HexbinConfig cfg;
                 cfg.gridsize = gridsize;
                 cfg.minCount = mincnt;
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 a.ax->addPlot(std::make_unique<plot::HexbinPlot>(
                     toFloats(x), toFloats(y), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("gridsize") = 50,
             py::arg("cmap") = py::none(), py::arg("mincnt") = 0)
        .def("quiver",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& u, const py::object& v,
                const py::object& color, float scale,
                const std::string& pivot, const std::string& label) {
                 auto [uv, ud] = flat2d(u);
                 auto [vv, vd] = flat2d(v);
                 if (ud != vd)
                     throw std::invalid_argument("u and v shapes differ");
                 auto [nru, ncu] = ud;
                 auto xv = toFloatList(x), yv = toFloatList(y);
                 // 1D x,y + 2D u,v → expand to meshgrid positions
                 // (mpl quiver(X, Y, U, V)).
                 if (nru > 1 && xv.size() == ncu && yv.size() == nru) {
                     std::vector<float> xe, ye;
                     xe.reserve(size_t(nru) * ncu);
                     ye.reserve(size_t(nru) * ncu);
                     for (uint32_t j = 0; j < nru; ++j)
                         for (uint32_t i = 0; i < ncu; ++i) {
                             xe.push_back(xv[i]);
                             ye.push_back(yv[j]);
                         }
                     xv = std::move(xe);
                     yv = std::move(ye);
                 }
                 plot::QuiverConfig cfg;
                 cfg.color = lineColor(color, plot::Color::black());
                 cfg.scale = scale;
                 cfg.label = label;
                 if (pivot == "middle" || pivot == "mid")
                     cfg.pivot = plot::QuiverConfig::Pivot::Middle;
                 else if (pivot == "tip")
                     cfg.pivot = plot::QuiverConfig::Pivot::Tip;
                 a.ax->addPlot(std::make_unique<plot::QuiverPlot>(
                     std::move(xv), std::move(yv), std::move(uv),
                     std::move(vv), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("u"), py::arg("v"),
             py::arg("color") = py::none(), py::arg("scale") = 0.0f,
             py::arg("pivot") = "tail", py::arg("label") = "")
        .def("streamplot",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& u, const py::object& v, float density,
                const py::object& color, float linewidth, bool arrows) {
                 auto gu = toGrid2D(u), gv = toGrid2D(v);
                 if (!x.is_none() && !y.is_none()) {
                     auto xv = toFloats(x), yv = toFloats(y);
                     if (xv.size() >= 2) {
                         gu.xRange = gv.xRange = {xv.front(), xv.back()};
                     }
                     if (yv.size() >= 2) {
                         gu.yRange = gv.yRange = {yv.front(), yv.back()};
                     }
                 }
                 plot::StreamConfig cfg;
                 cfg.density = density;
                 cfg.color = lineColor(color, plot::Color::black());
                 cfg.lineWidth = linewidth;
                 cfg.arrows = arrows;
                 a.ax->addPlot(std::make_unique<plot::StreamPlot>(
                     std::move(gu), std::move(gv), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("u"), py::arg("v"),
             py::arg("density") = 1.0f, py::arg("color") = py::none(),
             py::arg("linewidth") = 1.0f, py::arg("arrows") = true)
        .def("violinplot",
             [](PyAxes& a, const py::object& data,
                const py::object& positions, const py::object& widths,
                bool vert, bool showmeans, bool showextrema) {
                 plot::ViolinConfig cfg;
                 cfg.vert = vert;
                 cfg.showMean = showmeans;
                 cfg.showExtrema = showextrema;
                 if (!positions.is_none())
                     cfg.positions = toFloatList(positions);
                 if (!widths.is_none())
                     cfg.widths = toFloatList(widths);
                 a.ax->addPlot(std::make_unique<plot::ViolinPlot>(
                     toRows(data), cfg));
             },
             py::arg("dataset"), py::arg("positions") = py::none(),
             py::arg("widths") = py::none(), py::arg("vert") = true,
             py::arg("showmeans") = false, py::arg("showextrema") = true)
        .def("hist2d",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& bins, const py::object& cmap) {
                 plot::Hist2DConfig cfg;
                 if (bins.is_none()) {
                     cfg.bins = plot::Hist2DBinMethod::Auto;
                 } else {
                     cfg.bins = plot::Hist2DBinMethod::Fixed;
                     if (py::isinstance<py::int_>(bins)) {
                         cfg.nBinsX = cfg.nBinsY = bins.cast<int>();
                     } else {
                         auto b = bins.cast<std::vector<int>>();
                         if (b.size() != 2)
                             throw std::invalid_argument(
                                 "bins must be int or [nx, ny]");
                         cfg.nBinsX = b[0];
                         cfg.nBinsY = b[1];
                     }
                 }
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 a.ax->addPlot(std::make_unique<plot::Hist2DPlot>(
                     toFloats(x), toFloats(y), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("bins") = 10,
             py::arg("cmap") = py::none())
        .def("eventplot",
             [](PyAxes& a, const py::object& positions,
                std::string orientation, const py::object& colors,
                const py::object& lineoffsets,
                const py::object& linelengths,
                const py::object& linewidths) {
                 auto& ep = a.ax->eventplot(toRows(positions));
                 ep.orientation = std::move(orientation);
                 ep.colors = toColors(colors, plot::Color::black());
                 if (!lineoffsets.is_none())
                     ep.lineoffsets = toFloatList(lineoffsets);
                 if (!linelengths.is_none())
                     ep.linelengths = toFloatList(linelengths);
                 if (!linewidths.is_none())
                     ep.linewidths = toFloatList(linewidths);
             },
             py::arg("positions"),
             py::arg("orientation") = "horizontal",
             py::arg("colors") = py::none(),
             py::arg("lineoffsets") = py::none(),
             py::arg("linelengths") = py::none(),
             py::arg("linewidths") = py::none())
        // ── Triangular grid plots (mpl tri*) ──
        .def("triplot",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& triangles, const py::object& color) {
                 auto xv = toFloats(x), yv = toFloats(y);
                 plot::TriplotConfig cfg;
                 cfg.color = lineColor(color, plot::Color::black());
                 if (triangles.is_none())
                     a.ax->addPlot(std::make_unique<plot::TriplotPlot>(
                         std::move(xv), std::move(yv), cfg));
                 else
                     a.ax->addPlot(std::make_unique<plot::TriplotPlot>(
                         std::move(xv), std::move(yv),
                         toTriangles(triangles), cfg));
             },
             py::arg("x"), py::arg("y"),
             py::arg("triangles") = py::none(),
             py::arg("color") = py::none())
        .def("tripcolor",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& triangles,
                const py::object& cmap) {
                 auto xv = toFloats(x), yv = toFloats(y);
                 auto zv = toFloats(z);
                 plot::TripcolorConfig cfg;
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 if (triangles.is_none())
                     // Per-vertex z (mpl tripcolor(x, y, z)).
                     a.ax->addPlot(std::make_unique<plot::TripcolorPlot>(
                         std::move(xv), std::move(yv), std::move(zv),
                         cfg));
                 else
                     // Explicit triangulation + per-face values.
                     a.ax->addPlot(std::make_unique<plot::TripcolorPlot>(
                         std::move(xv), std::move(yv),
                         toTriangles(triangles), std::move(zv), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("triangles") = py::none(),
             py::arg("cmap") = py::none())
        .def("tricontour",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& levels,
                const py::object& cmap) {
                 plot::TriContourConfig cfg;
                 if (py::isinstance<py::int_>(levels))
                     cfg.numLevels = levels.cast<int>();
                 else if (!levels.is_none())
                     cfg.levels = toFloats(levels);
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 a.ax->addPlot(std::make_unique<plot::TriContourPlot>(
                     toFloats(x), toFloats(y), toFloats(z), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("levels") = py::none(), py::arg("cmap") = py::none())
        .def("tricontourf",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& levels,
                const py::object& cmap) {
                 plot::TriContourConfig cfg;
                 if (py::isinstance<py::int_>(levels))
                     cfg.numLevels = levels.cast<int>();
                 else if (!levels.is_none())
                     cfg.levels = toFloats(levels);
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 a.ax->addPlot(std::make_unique<plot::TriContourfPlot>(
                     toFloats(x), toFloats(y), toFloats(z), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("levels") = py::none(), py::arg("cmap") = py::none())
        // ── Spectral plots (mpl psd/csd/specgram/cohere/xcorr/acorr) ──
        .def("psd",
             [](PyAxes& a, const py::object& x, float Fs, int nfft,
                int noverlap, const std::string& window,
                const py::object& color, const std::string& label) {
                 plot::PsdConfig cfg;
                 cfg.sampleRate = Fs;
                 cfg.nfft = nfft > 0 ? uint32_t(nfft) : 0;
                 cfg.noverlap = noverlap > 0 ? uint32_t(noverlap) : 0;
                 cfg.window =
                     windowByName<plot::PsdConfig::Window>(window);
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::PsdPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("Fs") = 2.0f, py::arg("NFFT") = 0,
             py::arg("noverlap") = 0, py::arg("window") = "hann",
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("csd",
             [](PyAxes& a, const py::object& x, const py::object& y,
                float Fs, int nfft, int noverlap,
                const std::string& window, const py::object& color,
                const std::string& label) {
                 plot::CsdConfig cfg;
                 cfg.sampleRate = Fs;
                 cfg.nfft = nfft > 0 ? uint32_t(nfft) : 0;
                 cfg.noverlap = noverlap > 0 ? uint32_t(noverlap) : 0;
                 cfg.window =
                     windowByName<plot::CsdConfig::Window>(window);
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::CsdPlot>(
                     toFloats(x), toFloats(y), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("Fs") = 2.0f,
             py::arg("NFFT") = 0, py::arg("noverlap") = 0,
             py::arg("window") = "hann", py::arg("color") = py::none(),
             py::arg("label") = "")
        .def("specgram",
             [](PyAxes& a, const py::object& x, float Fs, int nfft,
                int noverlap, const py::object& cmap) {
                 plot::SpecgramConfig cfg;
                 cfg.sampleRate = Fs;
                 if (nfft > 0) cfg.nfft = uint32_t(nfft);
                 // mpl default: noverlap = NFFT // 2.
                 cfg.noverlap = noverlap >= 0
                     ? uint32_t(noverlap)
                     : cfg.nfft / 2;
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 a.ax->addPlot(std::make_unique<plot::SpecgramPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("Fs") = 2.0f, py::arg("NFFT") = 256,
             py::arg("noverlap") = -1, py::arg("cmap") = py::none())
        .def("cohere",
             [](PyAxes& a, const py::object& x, const py::object& y,
                float Fs, int nfft, int noverlap,
                const std::string& window, const py::object& color,
                const std::string& label) {
                 plot::CohereConfig cfg;
                 cfg.sampleRate = Fs;
                 cfg.nfft = nfft > 0 ? uint32_t(nfft) : 0;
                 cfg.noverlap = noverlap > 0 ? uint32_t(noverlap) : 0;
                 cfg.window =
                     windowByName<plot::CohereConfig::Window>(window);
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::CoherePlot>(
                     toFloats(x), toFloats(y), cfg));
             },
             py::arg("x"), py::arg("y"), py::arg("Fs") = 2.0f,
             py::arg("NFFT") = 0, py::arg("noverlap") = 0,
             py::arg("window") = "hann", py::arg("color") = py::none(),
             py::arg("label") = "")
        .def("xcorr",
             [](PyAxes& a, const py::object& x, const py::object& y,
                bool normed, int maxlags, const py::object& color,
                const std::string& label) {
                 plot::XCorrConfig cfg;
                 cfg.normed = normed;
                 cfg.maxLags = maxlags > 0 ? uint32_t(maxlags) : 0;
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 if (y.is_none())
                     a.ax->addPlot(std::make_unique<plot::XCorrPlot>(
                         toFloats(x), cfg));
                 else
                     a.ax->addPlot(std::make_unique<plot::XCorrPlot>(
                         toFloats(x), toFloats(y), cfg));
             },
             py::arg("x"), py::arg("y") = py::none(),
             py::arg("normed") = true, py::arg("maxlags") = 0,
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("acorr",
             [](PyAxes& a, const py::object& x, bool normed, int maxlags,
                const py::object& color, const std::string& label) {
                 plot::XCorrConfig cfg;
                 cfg.normed = normed;
                 cfg.maxLags = maxlags > 0 ? uint32_t(maxlags) : 0;
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::XCorrPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("normed") = true,
             py::arg("maxlags") = 0, py::arg("color") = py::none(),
             py::arg("label") = "")
        .def("magnitude_spectrum",
             [](PyAxes& a, const py::object& x, float Fs,
                const std::string& scale, const std::string& window,
                const py::object& color, const std::string& label) {
                 plot::SpectrumConfig cfg;
                 cfg.type = plot::SpectrumType::Magnitude;
                 cfg.scale = scale == "dB" ? plot::SpectrumScale::dB
                                           : plot::SpectrumScale::Linear;
                 cfg.sampleRate = Fs;
                 cfg.window =
                     windowByName<plot::SpectrumConfig::Window>(window);
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::SpectrumPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("Fs") = 2.0f,
             py::arg("scale") = "linear", py::arg("window") = "hann",
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("phase_spectrum",
             [](PyAxes& a, const py::object& x, float Fs,
                const std::string& window, const py::object& color,
                const std::string& label) {
                 plot::SpectrumConfig cfg;
                 cfg.type = plot::SpectrumType::Phase;
                 cfg.sampleRate = Fs;
                 cfg.window =
                     windowByName<plot::SpectrumConfig::Window>(window);
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::SpectrumPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("Fs") = 2.0f,
             py::arg("window") = "hann", py::arg("color") = py::none(),
             py::arg("label") = "")
        .def("angle_spectrum",
             [](PyAxes& a, const py::object& x, float Fs,
                const std::string& window, const py::object& color,
                const std::string& label) {
                 plot::SpectrumConfig cfg;
                 cfg.type = plot::SpectrumType::Angle;
                 cfg.sampleRate = Fs;
                 cfg.window =
                     windowByName<plot::SpectrumConfig::Window>(window);
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::SpectrumPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("Fs") = 2.0f,
             py::arg("window") = "hann", py::arg("color") = py::none(),
             py::arg("label") = "")
        // ── Misc mpl plot types ──
        .def("ecdf",
             [](PyAxes& a, const py::object& x, bool complementary,
                bool markers, bool fill, const py::object& color,
                const std::string& label) {
                 plot::ECDFConfig cfg;
                 cfg.complementary = complementary;
                 cfg.markers = markers;
                 cfg.fill = fill;
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 cfg.label = label;
                 a.ax->addPlot(std::make_unique<plot::ECDFPlot>(
                     toFloats(x), cfg));
             },
             py::arg("x"), py::arg("complementary") = false,
             py::arg("markers") = false, py::arg("fill") = false,
             py::arg("color") = py::none(), py::arg("label") = "")
        .def("spy",
             [](PyAxes& a, const py::object& z, float precision,
                float markersize, const py::object& color) {
                 auto [data, dims] = flat2d(z);
                 plot::SpyConfig cfg;
                 cfg.precision = precision;
                 cfg.markerSize = markersize;
                 cfg.color = lineColor(
                     color, plot::Color::fromRgba8(31, 119, 180, 255));
                 a.ax->addPlot(std::make_unique<plot::SpyPlot>(
                     std::move(data), dims.first, dims.second, cfg));
             },
             py::arg("Z"), py::arg("precision") = 0.0f,
             py::arg("markersize") = 1.0f,
             py::arg("color") = py::none())
        .def("matshow",
             [](PyAxes& a, const py::object& z, const py::object& cmap) {
                 auto [data, dims] = flat2d(z);
                 plot::MatshowConfig cfg;
                 if (!cmap.is_none())
                     cfg.cmap = &cmapByName(cmap.cast<std::string>());
                 a.ax->addPlot(std::make_unique<plot::MatshowPlot>(
                     std::move(data), dims.first, dims.second, cfg));
             },
             py::arg("A"), py::arg("cmap") = py::none())
        .def("fill_betweenx",
             [](PyAxes& a, const py::object& y, const py::object& x1,
                const py::object& x2, const py::object& color,
                const std::string& label) {
                 auto yv = toFloats(y);
                 auto x1v = toFloatList(x1);
                 auto x2v = x2.is_none()
                     ? std::vector<float>(yv.size(), 0.0f)
                     : toFloatList(x2);
                 auto broadcast = [&](std::vector<float>& v,
                                      const char* name) {
                     if (v.size() == 1 && yv.size() > 1)
                         v.assign(yv.size(), v[0]);
                     if (v.size() != yv.size())
                         throw std::invalid_argument(
                             std::string(name) +
                             " length must match y");
                 };
                 broadcast(x1v, "x1");
                 broadcast(x2v, "x2");
                 plot::Series2D s;
                 s.points.reserve(2 * yv.size());
                 for (size_t i = 0; i < yv.size(); ++i)
                     s.points.push_back({x1v[i], yv[i]});
                 for (size_t i = yv.size(); i-- > 0;)
                     s.points.push_back({x2v[i], yv[i]});
                 s.color = lineColor(
                     color,
                     plot::Color::fromRgba8(31, 119, 180, 128));
                 s.label = label;
                 a.ax->addPlot(
                     std::make_unique<plot::FillPlot>(std::move(s)));
             },
             py::arg("y"), py::arg("x1"), py::arg("x2") = py::none(),
             py::arg("color") = py::none(), py::arg("label") = "")
        // ── 3D plot types (mpl Axes3D methods) ──
        .def("plot3d",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& color,
                float linewidth, const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y), zv = toFloats(z);
                 auto vr = range3(xv, yv, zv);
                 plot::Plot3DConfig cfg;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 cfg.lineWidth = linewidth; cfg.label = label;
                 auto p = std::make_unique<plot::Plot3D>(std::move(xv),
                     std::move(yv), std::move(zv), std::move(cfg));
                 p->setCamera(cameraFor3D(a, vr));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("color") = py::none(), py::arg("linewidth") = 1.5f,
             py::arg("label") = "")
        .def("scatter3d",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& color, float s,
                bool depthshade, const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y), zv = toFloats(z);
                 auto vr = range3(xv, yv, zv);
                 plot::Scatter3DConfig cfg;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 cfg.size = s; cfg.depthshade = depthshade;
                 cfg.label = label;
                 auto p = std::make_unique<plot::Scatter3D>(std::move(xv),
                     std::move(yv), std::move(zv), std::move(cfg));
                 p->setCamera(cameraFor3D(a, vr));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("color") = py::none(), py::arg("s") = 6.0f,
             py::arg("depthshade") = true, py::arg("label") = "")
        .def("plot_surface",
             [](PyAxes& a, const py::object& values,
                const py::object& x, const py::object& y,
                bool shade) {
                 auto g = toGrid2D(values);
                 if (!x.is_none()) {
                     auto xv = toFloats(x);
                     if (!xv.empty()) g.xRange = {xv.front(), xv.back()};
                 }
                 if (!y.is_none()) {
                     auto yv = toFloats(y);
                     if (!yv.empty()) g.yRange = {yv.front(), yv.back()};
                 }
                 auto p = std::make_unique<plot::SurfacePlot>(
                     g, cameraFor3D(a, gridRange3(g)));
                 p->shade = shade;
                 a.ax->addPlot(std::move(p));
             },
             py::arg("z"), py::arg("x") = py::none(),
             py::arg("y") = py::none(), py::arg("shade") = true)
        .def("plot_wireframe",
             [](PyAxes& a, const py::object& values,
                const py::object& x, const py::object& y,
                const py::object& color, float linewidth,
                uint32_t rstride, uint32_t cstride) {
                 auto g = toGrid2D(values);
                 if (!x.is_none()) {
                     auto xv = toFloats(x);
                     if (!xv.empty()) g.xRange = {xv.front(), xv.back()};
                 }
                 if (!y.is_none()) {
                     auto yv = toFloats(y);
                     if (!yv.empty()) g.yRange = {yv.front(), yv.back()};
                 }
                 plot::WireframeConfig cfg;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 cfg.lineWidth = linewidth;
                 cfg.rowStride = rstride; cfg.colStride = cstride;
                 auto p = std::make_unique<plot::WireframePlot>(
                     g, std::move(cfg));
                 p->setCamera(cameraFor3D(a, gridRange3(g)));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("z"), py::arg("x") = py::none(),
             py::arg("y") = py::none(), py::arg("color") = py::none(),
             py::arg("linewidth") = 1.0f, py::arg("rstride") = 1,
             py::arg("cstride") = 1)
        .def("bar3d",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& dx,
                const py::object& dy, const py::object& dz,
                const py::object& color, const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y), zv = toFloats(z);
                 auto dxv = toFloats(dx), dyv = toFloats(dy),
                      dzv = toFloats(dz);
                 // Bars span [x, x+dx] × [y, y+dy] × [z, z+dz].
                 auto vr = range3(xv, yv, zv);
                 for (size_t i = 0; i < xv.size(); ++i) {
                     if (i < dxv.size()) {
                         vr.x.min = std::min(vr.x.min, xv[i] + dxv[i]);
                         vr.x.max = std::max(vr.x.max, xv[i] + dxv[i]);
                     }
                     if (i < dyv.size()) {
                         vr.y.min = std::min(vr.y.min, yv[i] + dyv[i]);
                         vr.y.max = std::max(vr.y.max, yv[i] + dyv[i]);
                     }
                     if (i < dzv.size()) {
                         vr.z.min = std::min(vr.z.min, zv[i] + dzv[i]);
                         vr.z.max = std::max(vr.z.max, zv[i] + dzv[i]);
                     }
                 }
                 plot::Bar3DConfig cfg;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 cfg.label = label;
                 auto p = std::make_unique<plot::Bar3D>(std::move(xv),
                     std::move(yv), std::move(zv), std::move(dxv),
                     std::move(dyv), std::move(dzv), std::move(cfg));
                 p->setCamera(cameraFor3D(a, vr));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("x"), py::arg("y"), py::arg("z"), py::arg("dx"),
             py::arg("dy"), py::arg("dz"), py::arg("color") = py::none(),
             py::arg("label") = "")
        .def("voxels",
             [](PyAxes& a, const py::object& filled,
                const py::object& color, bool edge) {
                 py::array_t<uint8_t,
                     py::array::forcecast | py::array::c_style> f =
                     py::array_t<uint8_t, py::array::forcecast |
                                         py::array::c_style>::ensure(
                         filled);
                 if (!f || f.ndim() != 3)
                     throw std::invalid_argument(
                         "voxels expects a 3D bool array");
                 auto r = f.unchecked<3>();
                 uint32_t nx = uint32_t(r.shape(0)),
                          ny = uint32_t(r.shape(1)),
                          nz = uint32_t(r.shape(2));
                 std::vector<uint8_t> flat(size_t(nx) * ny * nz);
                 for (py::ssize_t i = 0; i < r.shape(0); ++i)
                     for (py::ssize_t j = 0; j < r.shape(1); ++j)
                         for (py::ssize_t k = 0; k < r.shape(2); ++k)
                             flat[(size_t(i) * ny + size_t(j)) * nz +
                                  size_t(k)] = r(i, j, k) ? 1 : 0;
                 plot::VoxelsConfig cfg;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 cfg.drawEdges = edge;
                 auto p = std::make_unique<plot::VoxelsPlot>(
                     std::move(flat), nx, ny, nz, std::move(cfg));
                 p->setCamera(cameraFor3D(
                     a, plot::Viewport{{0, float(nx)}, {0, float(ny)},
                                       {0, float(nz)}}));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("filled"), py::arg("color") = py::none(),
             py::arg("edge") = false)
        .def("plot_trisurf",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& triangles,
                const std::string& cmapName) {
                 auto xv = toFloats(x), yv = toFloats(y), zv = toFloats(z);
                 auto vr = range3(xv, yv, zv);
                 plot::TrisurfConfig cfg;
                 cfg.cmap = &cmapByName(cmapName);
                 std::unique_ptr<plot::TrisurfPlot> p;
                 if (triangles.is_none())
                     p = std::make_unique<plot::TrisurfPlot>(
                         std::move(xv), std::move(yv), std::move(zv),
                         std::move(cfg));
                 else {
                     std::vector<plot::Triangle> tris;
                     for (auto t : py::cast<py::sequence>(triangles)) {
                         auto v = t.cast<std::vector<uint32_t>>();
                         if (v.size() == 3)
                             tris.push_back({v[0], v[1], v[2]});
                     }
                     p = std::make_unique<plot::TrisurfPlot>(
                         std::move(xv), std::move(yv), std::move(zv),
                         std::move(tris), std::move(cfg));
                 }
                 p->setCamera(cameraFor3D(a, vr));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("triangles") = py::none(), py::arg("cmap") = "viridis")
        .def("quiver3d",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& z, const py::object& u,
                const py::object& v, const py::object& w,
                const py::object& color, float scale,
                const std::string& label) {
                 auto xv = toFloats(x), yv = toFloats(y), zv = toFloats(z);
                 auto uv = toFloats(u), vv = toFloats(v), wv = toFloats(w);
                 auto vr = range3(xv, yv, zv);
                 for (size_t i = 0; i < xv.size(); ++i) {
                     if (i < uv.size()) {
                         vr.x.min = std::min(vr.x.min, xv[i] + uv[i]);
                         vr.x.max = std::max(vr.x.max, xv[i] + uv[i]);
                     }
                     if (i < vv.size()) {
                         vr.y.min = std::min(vr.y.min, yv[i] + vv[i]);
                         vr.y.max = std::max(vr.y.max, yv[i] + vv[i]);
                     }
                     if (i < wv.size()) {
                         vr.z.min = std::min(vr.z.min, zv[i] + wv[i]);
                         vr.z.max = std::max(vr.z.max, zv[i] + wv[i]);
                     }
                 }
                 plot::Quiver3DConfig cfg;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 cfg.scale = scale; cfg.label = label;
                 auto p = std::make_unique<plot::Quiver3D>(std::move(xv),
                     std::move(yv), std::move(zv), std::move(uv),
                     std::move(vv), std::move(wv), std::move(cfg));
                 p->setCamera(cameraFor3D(a, vr));
                 a.ax->addPlot(std::move(p));
             },
             py::arg("x"), py::arg("y"), py::arg("z"), py::arg("u"),
             py::arg("v"), py::arg("w"), py::arg("color") = py::none(),
             py::arg("scale") = 1.0f, py::arg("label") = "")
        // ── Twin / shared / secondary axes (mpl ax.twinx etc.) ──
        .def("twinx",
             [](PyAxes& a) {
                 auto* t = a.ax->twinx();
                 if (!t) throw std::runtime_error("twinx failed");
                 return wrapAxes(a.owner, t);
             })
        .def("twiny",
             [](PyAxes& a) {
                 auto* t = a.ax->twiny();
                 if (!t) throw std::runtime_error("twiny failed");
                 return wrapAxes(a.owner, t);
             })
        .def("sharex",
             [](PyAxes& a, PyAxes& other) { a.ax->shareX(*other.ax); },
             py::arg("other"))
        .def("sharey",
             [](PyAxes& a, PyAxes& other) { a.ax->shareY(*other.ax); },
             py::arg("other"))
        .def("secondary_xaxis",
             [](PyAxes& a, const std::string& location,
                py::object functions, const std::string& label) {
                 // mpl: functions=(forward, inverse) callables.
                 auto seq = functions.cast<py::sequence>();
                 if (seq.size() != 2)
                     throw std::invalid_argument(
                         "secondary_xaxis: functions=(fwd, inv)");
                 py::function forward = seq[0].cast<py::function>(),
                              inverse = seq[1].cast<py::function>();
                 auto fwd = [forward](float v) {
                     py::gil_scoped_acquire gil;
                     return forward(v).cast<float>();
                 };
                 auto inv = [inverse](float v) {
                     py::gil_scoped_acquire gil;
                     return inverse(v).cast<float>();
                 };
                 a.ax->secondaryXaxis(fwd, inv, label);
             },
             py::arg("location"), py::arg("functions"),
             py::arg("label") = "")
        .def("secondary_yaxis",
             [](PyAxes& a, const std::string& location,
                py::object functions, const std::string& label) {
                 auto seq = functions.cast<py::sequence>();
                 if (seq.size() != 2)
                     throw std::invalid_argument(
                         "secondary_yaxis: functions=(fwd, inv)");
                 py::function forward = seq[0].cast<py::function>(),
                              inverse = seq[1].cast<py::function>();
                 auto fwd = [forward](float v) {
                     py::gil_scoped_acquire gil;
                     return forward(v).cast<float>();
                 };
                 auto inv = [inverse](float v) {
                     py::gil_scoped_acquire gil;
                     return inverse(v).cast<float>();
                 };
                 a.ax->secondaryYaxis(fwd, inv, label);
             },
             py::arg("location"), py::arg("functions"),
             py::arg("label") = "")

        // mpl ax.add_patch: copies the patch spec into a one-element
        // PatchCollection and returns a live handle.
        .def("add_patch",
             [](PyAxes& a, PyPatch& p) -> py::object {
                 auto coll = std::make_unique<plot::PatchCollection>(
                     std::vector<plot::Patch>{p.spec});
                 auto* raw = coll.get();
                 a.ax->addPlot(std::move(coll));
                 a.ax->touch();
                 p.owner = a.owner;
                 p.ax = a.ax;
                 p.coll = raw;
                 p.live = &raw->patches.back();
                 return py::cast(&p,
                                 py::return_value_policy::reference);
             },
             py::arg("patch"))
        // mpl ax.add_collection: takes ownership of the collection.
        .def("add_collection",
             [](PyAxes& a, PyColl& c) -> py::object {
                 if (!c.owned)
                     throw std::runtime_error(
                         "collection already added to an axes");
                 c.live = static_cast<plot::Collection*>(
                     a.ax->addPlot(std::move(c.owned)));
                 c.owner = a.owner;
                 c.ax = a.ax;
                 a.ax->touch();
                 return py::cast(&c,
                                 py::return_value_policy::reference);
             },
             py::arg("collection"))

        // ── mpl ax.barbs: wind barbs (x, y, u, v) ──
        .def("barbs",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& u, const py::object& v,
                float length, bool flip, const py::object& color,
                float linewidth, bool rounding, const std::string& label) {
                 plot::BarbsConfig cfg;
                 cfg.length = length; cfg.flip = flip;
                 cfg.lineWidth = linewidth; cfg.rounding = rounding;
                 cfg.label = label;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 a.ax->addPlot(std::make_unique<plot::BarbsPlot>(
                     toFloats(x), toFloats(y), toFloats(u), toFloats(v),
                     cfg));
                 a.ax->touch();
             },
             py::arg("x"), py::arg("y"), py::arg("u"), py::arg("v"),
             py::arg("length") = 7.0f, py::arg("flip") = false,
             py::arg("color") = py::none(), py::arg("linewidth") = 1.0f,
             py::arg("rounding") = false, py::arg("label") = "")
        // mpl ax.broken_barh(xranges, yrange): xranges=[(x0,dx),...]
        // sharing one (y, height) range — or per-segment yrange list.
        .def("broken_barh",
             [](PyAxes& a, const py::object& xranges,
                const py::object& yrange, const py::object& facecolors,
                const py::object& edgecolor, const std::string& label) {
                 auto yrs = [&]() -> std::vector<std::pair<float,float>> {
                     auto first = yrange.cast<py::sequence>();
                     if (first.size() == 2 &&
                         (py::isinstance<py::float_>(first[0]) ||
                          py::isinstance<py::int_>(first[0])))
                         return {first.cast<std::pair<float,float>>()};
                     std::vector<std::pair<float,float>> out;
                     for (auto item : first)
                         out.push_back(
                             item.cast<std::pair<float,float>>());
                     return out;
                 }();
                 auto xr = xranges.cast<
                     std::vector<std::pair<float,float>>>();
                 std::vector<plot::BarHSegment> segs;
                 segs.reserve(xr.size());
                 for (size_t i = 0; i < xr.size(); ++i) {
                     auto [y, h] = yrs[i % yrs.size()];
                     segs.push_back({xr[i].first, xr[i].second, y, h});
                 }
                 plot::BrokenBarHConfig cfg;
                 cfg.label = label;
                 if (!facecolors.is_none()) {
                     if (py::isinstance<py::str>(facecolors))
                         cfg.color = parseColor(facecolors);
                     else for (auto item : facecolors.cast<py::sequence>())
                         cfg.colors.push_back(
                             parseColor(item.cast<py::object>()));
                 }
                 if (auto c = parseColor(edgecolor); c.a > 0)
                     cfg.edgeColor = c;
                 a.ax->addPlot(std::make_unique<plot::BrokenBarHPlot>(
                     std::move(segs), cfg));
                 a.ax->touch();
             },
             py::arg("xranges"), py::arg("yrange"),
             py::arg("facecolors") = py::none(),
             py::arg("edgecolor") = py::none(), py::arg("label") = "")
        // mpl ax.bar_label: value labels on bars. Accepts the
        // BarContainer returned by ax.bar/barh, or explicit x/heights.
        .def("bar_label",
             [](PyAxes& a, const py::object& container,
                const py::object& heights,
                float baseline, const std::string& fmt,
                const std::string& label_type, float padding,
                const py::object& color, bool horizontal,
                const py::object& labels) {
                 plot::BarLabelConfig cfg;
                 cfg.fmt = fmt; cfg.padding = padding;
                 cfg.horizontal = horizontal;
                 cfg.position =
                     label_type == "center" || label_type == "centre"
                         ? plot::BarLabelPosition::Center
                         : plot::BarLabelPosition::Edge;
                 if (auto c = parseColor(color); c.a > 0) cfg.color = c;
                 if (!labels.is_none())
                     cfg.labels = labels.cast<std::vector<std::string>>();
                 std::vector<float> xv, hv;
                 if (py::isinstance<PyBarContainer>(container)) {
                     const auto& bc = container.cast<PyBarContainer>();
                     xv = bc.x; hv = bc.heights;
                     baseline = bc.baseline;
                     cfg.horizontal = bc.horizontal;
                 } else {
                     xv = toFloats(container);
                     hv = toFloats(heights);
                 }
                 a.ax->addPlot(std::make_unique<plot::BarLabelPlot>(
                     std::move(xv), std::move(hv), baseline, cfg));
                 a.ax->touch();
             },
             py::arg("container"), py::arg("heights") = py::none(),
             py::arg("baseline") = 0.0f,
             py::arg("fmt") = "", py::arg("label_type") = "edge",
             py::arg("padding") = 3.0f, py::arg("color") = py::none(),
             py::arg("horizontal") = false,
             py::arg("labels") = py::none())
        // mpl ax.table(cellText=, rowLabels=, colLabels=, loc=,
        // cellColours=, cellLoc=, colWidths=, rowColours=, colColours=).
        // Per-cell text alignment isn't modeled — cells render centered.
        .def("table",
             [](PyAxes& a, const py::object& cellText,
                const py::object& rowLabels, const py::object& colLabels,
                const std::string& loc, const py::object& cellColours,
                const py::object& rowColours,
                const py::object& colColours, const py::kwargs& kw) {
                 std::vector<std::vector<std::string>> cells;
                 if (!cellText.is_none())
                     for (auto row : cellText.cast<py::sequence>()) {
                         std::vector<std::string> r;
                         for (auto c : row.cast<py::sequence>())
                             r.push_back(py::str(c));
                         cells.push_back(std::move(r));
                     }
                 auto& t = a.ax->table(std::move(cells), loc);
                 if (!rowLabels.is_none())
                     t.rowLabels =
                         rowLabels.cast<std::vector<std::string>>();
                 if (!colLabels.is_none())
                     t.colLabels =
                         colLabels.cast<std::vector<std::string>>();
                 if (!cellColours.is_none()) {
                     for (auto row : cellColours.cast<py::sequence>()) {
                         std::vector<plot::Color> r;
                         for (auto c : row.cast<py::sequence>())
                             r.push_back(parseColor(
                                 c.cast<py::object>()));
                         t.cellColors.push_back(std::move(r));
                     }
                 }
                 auto labelCols = [&](const py::object& seq) {
                     if (!seq.is_none()) {
                         auto v = seq.cast<py::sequence>();
                         if (v.size() > 0)
                             t.labelColor = parseColor(
                                 v[0].cast<py::object>());
                     }
                 };
                 labelCols(rowColours);
                 labelCols(colColours);
                 // cellLoc/rowLoc/colLoc/colWidths/edges accepted via
                 // kwargs for signature parity (renderer centers text).
                 a.ax->touch();
             },
             py::arg("cellText") = py::none(),
             py::arg("rowLabels") = py::none(),
             py::arg("colLabels") = py::none(), py::arg("loc") = "bottom",
             py::arg("cellColours") = py::none(),
             py::arg("rowColours") = py::none(),
             py::arg("colColours") = py::none())
        // wordcloud-package equivalent: words = dict or [(w, weight)].
        .def("wordcloud",
             [](PyAxes& a, const py::object& words,
                const py::kwargs& kw) {
                 std::vector<std::pair<std::string, double>> ws;
                 if (py::isinstance<py::dict>(words)) {
                     for (auto& [k, v] : words.cast<py::dict>())
                         ws.emplace_back(k.cast<std::string>(),
                                         v.cast<double>());
                 } else {
                     for (auto item : words.cast<py::sequence>()) {
                         auto p = item.cast<
                             std::pair<std::string, double>>();
                         ws.push_back(p);
                     }
                 }
                 auto& wc = a.ax->wordcloud(std::move(ws));
                 for (auto& [k, v] : kw) {
                     auto key = k.cast<std::string>();
                     if (key == "min_font_scale")
                         wc.minFontScale = v.cast<float>();
                     else if (key == "max_font_scale")
                         wc.maxFontScale = v.cast<float>();
                     else if (key == "rotation_ratio" ||
                              key == "prefer_horizontal")
                         wc.rotationRatio = v.cast<float>();
                     else if (key == "max_words")
                         wc.maxWords = v.cast<uint32_t>();
                     else if (key == "seed")
                         wc.seed = v.cast<uint32_t>();
                     else if (key == "margin")
                         wc.margin = v.cast<float>();
                     else if (key == "log_scale")
                         wc.logScale = v.cast<bool>();
                     else if (key == "colormap" || key == "cmap") {
                         auto name = v.cast<std::string>();
                         if (!name.empty())
                             wc.cmap = &plot::Colormap::byName(name);
                     }
                 }
                 a.ax->touch();
             },
             py::arg("words"))
        // networkx.draw equivalent: ax.network(n, edges, layout=...).
        .def("network",
             [](PyAxes& a, uint32_t nodeCount, const py::object& edges,
                const std::string& layout, const py::object& positions,
                const py::object& labels, const py::object& node_color,
                const py::object& edge_color, float node_size,
                uint32_t seed, int iterations) {
                 std::vector<std::pair<uint32_t, uint32_t>> es;
                 for (auto e : edges.cast<py::sequence>())
                     es.push_back(
                         e.cast<std::pair<uint32_t, uint32_t>>());
                 auto& np = a.ax->network(nodeCount, std::move(es));
                 if (layout == "circular")
                     np.options().layout = plot::NetworkPlot::Layout::Circular;
                 else if (layout == "random")
                     np.options().layout = plot::NetworkPlot::Layout::Random;
                 else if (layout == "given")
                     np.options().layout = plot::NetworkPlot::Layout::Given;
                 if (!positions.is_none())
                     for (auto p : positions.cast<py::sequence>()) {
                         auto q = p.cast<std::pair<float, float>>();
                         np.options().positions.push_back(
                             {q.first, q.second});
                     }
                 if (!labels.is_none())
                     np.options().labels =
                         labels.cast<std::vector<std::string>>();
                 if (auto c = parseColor(node_color); c.a > 0)
                     np.options().nodeColor = c;
                 if (auto c = parseColor(edge_color); c.a > 0)
                     np.options().edgeColor = c;
                 if (node_size > 0) np.options().nodeSize = node_size;
                 np.options().seed = seed;
                 if (iterations > 0) np.options().iterations = iterations;
                 a.ax->touch();
             },
             py::arg("node_count"), py::arg("edges"),
             py::arg("layout") = "spring",
             py::arg("positions") = py::none(),
             py::arg("labels") = py::none(),
             py::arg("node_color") = py::none(),
             py::arg("edge_color") = py::none(),
             py::arg("node_size") = 14.0f, py::arg("seed") = 42,
             py::arg("iterations") = 60)
        // mpl ax.pcolorfast: pcolorfast(C), pcolorfast((x0,x1),(y0,y1),C)
        // or pcolorfast(x_edges, y_edges, C).
        .def("pcolorfast",
             [](PyAxes& a, const py::args& args,
                const std::string& cmap, float alpha) {
                 if (args.size() != 1 && args.size() != 3)
                     throw std::invalid_argument(
                         "pcolorfast(C) or pcolorfast(x, y, C)");
                 py::array_t<float> cArr =
                     args[args.size() - 1].cast<py::array_t<float>>();
                 if (cArr.ndim() != 2)
                     throw std::invalid_argument("C must be 2D");
                 uint32_t rows = uint32_t(cArr.shape(0)),
                          cols = uint32_t(cArr.shape(1));
                 std::vector<float> C(cArr.data(),
                                      cArr.data() + rows * cols);
                 plot::PcolorfastConfig cfg;
                 if (!cmap.empty())
                     cfg.cmap = &plot::Colormap::byName(cmap);
                 if (args.size() == 1) {
                     a.ax->addPlot(std::make_unique<plot::PcolorfastPlot>(
                         std::move(C), cols, rows,
                         plot::Range{0.0f, float(cols)},
                         plot::Range{0.0f, float(rows)}, cfg));
                 } else {
                     auto xv = toFloats(args[0].cast<py::object>());
                     auto yv = toFloats(args[1].cast<py::object>());
                     if (xv.size() == 2 && yv.size() == 2) {
                         // mpl extent form: ((x0,x1), (y0,y1), C)
                         a.ax->addPlot(
                             std::make_unique<plot::PcolorfastPlot>(
                                 std::move(C), cols, rows,
                                 plot::Range{xv[0], xv[1]},
                                 plot::Range{yv[0], yv[1]}, cfg));
                     } else {
                         a.ax->addPlot(
                             std::make_unique<plot::PcolorfastPlot>(
                                 std::move(xv), std::move(yv),
                                 std::move(C), cols, rows, cfg));
                     }
                 }
                 a.ax->touch();
             },
             py::arg("cmap") = "viridis", py::arg("alpha") = -1.0f)
        // ── mpl scale conveniences ──
        .def("loglog",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& fmt, const py::kwargs& kw) {
                 a.ax->loglog();
                 return axesPlot(a, x, y, fmt,
                                 kw.contains("color")
                                     ? kw["color"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("linewidth")
                                     ? kw["linewidth"].cast<float>()
                                     : 1.5f,
                                 kw.contains("marker")
                                     ? kw["marker"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("linestyle")
                                     ? kw["linestyle"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("markersize")
                                     ? kw["markersize"].cast<float>() : 0,
                                 kw.contains("alpha")
                                     ? kw["alpha"].cast<float>() : -1,
                                 kw.contains("label")
                                     ? kw["label"].cast<std::string>()
                                     : "");
             },
             py::arg("x"), py::arg("y"), py::arg("fmt") = py::none())
        .def("semilogx",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& fmt, const py::kwargs& kw) {
                 a.ax->semilogx();
                 return axesPlot(a, x, y, fmt,
                                 kw.contains("color")
                                     ? kw["color"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("linewidth")
                                     ? kw["linewidth"].cast<float>()
                                     : 1.5f,
                                 kw.contains("marker")
                                     ? kw["marker"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("linestyle")
                                     ? kw["linestyle"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("markersize")
                                     ? kw["markersize"].cast<float>() : 0,
                                 kw.contains("alpha")
                                     ? kw["alpha"].cast<float>() : -1,
                                 kw.contains("label")
                                     ? kw["label"].cast<std::string>()
                                     : "");
             },
             py::arg("x"), py::arg("y"), py::arg("fmt") = py::none())
        .def("semilogy",
             [](PyAxes& a, const py::object& x, const py::object& y,
                const py::object& fmt, const py::kwargs& kw) {
                 a.ax->semilogy();
                 return axesPlot(a, x, y, fmt,
                                 kw.contains("color")
                                     ? kw["color"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("linewidth")
                                     ? kw["linewidth"].cast<float>()
                                     : 1.5f,
                                 kw.contains("marker")
                                     ? kw["marker"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("linestyle")
                                     ? kw["linestyle"].cast<py::object>()
                                     : py::none(),
                                 kw.contains("markersize")
                                     ? kw["markersize"].cast<float>() : 0,
                                 kw.contains("alpha")
                                     ? kw["alpha"].cast<float>() : -1,
                                 kw.contains("label")
                                     ? kw["label"].cast<std::string>()
                                     : "");
             },
             py::arg("x"), py::arg("y"), py::arg("fmt") = py::none())
        // ── mpl Axes3D extras ──
        .def("errorbar3d", &axesErrorbar3D,
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("xerr") = py::none(), py::arg("yerr") = py::none(),
             py::arg("zerr") = py::none(), py::arg("color") = py::none(),
             py::arg("markersize") = 6.0f, py::arg("label") = "")
        .def("errorbar3D", &axesErrorbar3D,
             py::arg("x"), py::arg("y"), py::arg("z"),
             py::arg("xerr") = py::none(), py::arg("yerr") = py::none(),
             py::arg("zerr") = py::none(), py::arg("color") = py::none(),
             py::arg("markersize") = 6.0f, py::arg("label") = "")
        .def("text3d", &axesText3D,
             py::arg("x"), py::arg("y"), py::arg("z"), py::arg("s"),
             py::arg("color") = py::none(), py::arg("fontsize") = 0.0f)
        .def("text3D", &axesText3D,
             py::arg("x"), py::arg("y"), py::arg("z"), py::arg("s"),
             py::arg("color") = py::none(), py::arg("fontsize") = 0.0f)
        // mpl-3.10-style orientation indicator: nav cube in a corner.
        .def("navcube",
             [](PyAxes& a, const std::string& corner,
                const std::string& mode, float size) {
                 plot::NavCubeConfig cfg;
                 cfg.size = size;
                 if (corner == "upper right")
                     cfg.corner = plot::NavCubeCorner::UpperRight;
                 else if (corner == "lower left")
                     cfg.corner = plot::NavCubeCorner::LowerLeft;
                 else if (corner == "lower right")
                     cfg.corner = plot::NavCubeCorner::LowerRight;
                 if (mode == "cube")
                     cfg.mode = plot::NavCubeMode::Cube;
                 // Share the axes' 3D camera if one exists.
                 for (auto& p : a.ax->plots())
                     if (auto* cam = p->camera3D()) {
                         cfg.camera = *cam;
                         break;
                     }
                 a.ax->addPlot(
                     std::make_unique<plot::NavCubePlot>(cfg));
                 a.ax->touch();
             },
             py::arg("corner") = "upper left",
             py::arg("mode") = "triad", py::arg("size") = 36.0f);

    // mpl FuncAnimation(fig, func, frames, init_func, fargs, ...)
    py::class_<PyAnimation>(m, "FuncAnimation")
        .def(py::init<std::shared_ptr<PyFigure>, py::object, py::object,
                      py::object, py::object, int, bool, bool, int>(),
             py::arg("fig"), py::arg("func"), py::arg("frames"),
             py::arg("init_func") = py::none(),
             py::arg("fargs") = py::none(),
             py::arg("interval") = 200, py::arg("blit") = false,
             py::arg("repeat") = true, py::arg("repeat_delay") = 0)
        .def("save", &PyAnimation::save, py::arg("path"),
             py::arg("writer") = "", py::arg("fps") = 0.0,
             "Save to .apng/.gif/.mp4 (ffmpeg) or .html (JS player).")
        .def("to_jshtml", &PyAnimation::toJsHtml, py::arg("fps") = 0.0)
        .def("to_html5_video", &PyAnimation::toHtml5Video,
             py::arg("fps") = 0.0);

    py::class_<plot::LegendStyle>(m, "Legend")
        .def_readwrite("location", &plot::LegendStyle::location)
        .def_readwrite("visible", &plot::LegendStyle::visible)
        .def_readwrite("frameAlpha", &plot::LegendStyle::frameAlpha);

    // ── pyplot-style stateful API ────────────────────────────────────
    // `import volcanoplot.pyplot`-style usage directly on the module:
    //   vp.plot([1,2,3], [0,1,0]); vp.savefig("out.png")
    m.def("figure",
          [](const py::object& figsize, float dpi) {
              auto f = std::make_shared<PyFigure>(
                  figsize.is_none() ? 800u
                      : uint32_t(figsize.cast<std::pair<double,double>>().first * dpi + 0.5),
                  figsize.is_none() ? 600u
                      : uint32_t(figsize.cast<std::pair<double,double>>().second * dpi + 0.5),
                  dpi);
              gCurrentFig = f;
              gCurrentAx = nullptr;
              return f;
          },
          py::arg("figsize") = py::none(), py::arg("dpi") = 100.0f)
        .def("gcf", [] { return gcf(); })
        .def("gca", [] { return gca(); })
        .def("subplots",
             [](uint32_t nrows, uint32_t ncols, const py::object& figsize,
                float dpi, const py::object& sharex,
                const py::object& sharey, const py::object& projection,
                const py::object& subplot_kw) {
                 auto f = std::make_shared<PyFigure>(
                     figsize.is_none() ? 640u
                         : uint32_t(figsize.cast<std::pair<double,double>>().first * dpi + 0.5),
                     figsize.is_none() ? 480u
                         : uint32_t(figsize.cast<std::pair<double,double>>().second * dpi + 0.5),
                     dpi);
                 gCurrentFig = f;
                 gCurrentAx = nullptr;
                 // mpl: projection kwarg or subplot_kw={'projection': ...}.
                 std::string proj;
                 if (!projection.is_none())
                     proj = projection.cast<std::string>();
                 else if (!subplot_kw.is_none()) {
                     auto d = subplot_kw.cast<py::dict>();
                     if (d.contains("projection"))
                         proj = d["projection"].cast<std::string>();
                 }
                 py::list axs;
                 for (uint32_t r = 0; r < nrows; ++r)
                     for (uint32_t c = 0; c < ncols; ++c) {
                         auto* ax = f->figure_.subplot2grid({nrows, ncols}, {r, c});
                         if (!proj.empty()) ax->setProjection(proj);
                         axs.append(wrapAxes(f, ax));
                     }
                 if (!axs.empty())
                     gCurrentAx = axs[0].cast<PyAxes>().ax;
                 // mpl sharex/sharey: True → all share with the first
                 // axes; an Axes → share with it; "col"/"row" share per
                 // column/row.
                 auto applyShare = [&](const py::object& sh, bool isX) {
                     if (sh.is_none()) return;
                     if (py::isinstance<PyAxes>(sh)) {
                         auto* other = sh.cast<PyAxes>().ax;
                         for (auto item : axs) {
                             auto* ax = item.cast<PyAxes>().ax;
                             if (ax != other)
                                 isX ? ax->shareX(*other)
                                     : ax->shareY(*other);
                         }
                         return;
                     }
                     // bool or string form.
                     std::string mode;
                     if (py::isinstance<py::bool_>(sh) ||
                         py::isinstance<py::int_>(sh))
                         mode = sh.cast<bool>() ? "all" : "none";
                     else mode = sh.cast<std::string>();
                     if (mode == "none" || mode == "false") return;
                     auto getAx = [&](uint32_t r, uint32_t c) {
                         return axs[size_t(r) * ncols + c]
                             .cast<PyAxes>()
                             .ax;
                     };
                     for (uint32_t r = 0; r < nrows; ++r)
                         for (uint32_t c = 0; c < ncols; ++c) {
                             auto* ax = getAx(r, c);
                             plot::Axes* other = nullptr;
                             if (mode == "all")
                                 other = getAx(0, 0);
                             else if (mode == "col")
                                 other = getAx(0, c);
                             else if (mode == "row")
                                 other = getAx(r, 0);
                             else if (mode != "none")
                                 throw std::runtime_error(
                                     "sharex/sharey must be bool, 'all', "
                                     "'none', 'row', or 'col'");
                             if (other && other != ax)
                                 isX ? ax->shareX(*other)
                                     : ax->shareY(*other);
                         }
                 };
                 applyShare(sharex, true);
                 applyShare(sharey, false);
                 // mpl: single axes returned directly, grid as array
                 if (axs.size() == 1)
                     return py::make_tuple(f, axs[0]);
                 return py::make_tuple(f, axs);
             },
             py::arg("nrows") = 1, py::arg("ncols") = 1,
             py::arg("figsize") = py::none(), py::arg("dpi") = 100.0f,
             py::arg("sharex") = py::none(), py::arg("sharey") = py::none(),
             py::arg("projection") = py::none(),
             py::arg("subplot_kw") = py::none())
        .def("plot",
             [](const py::object& x, const py::object& y,
                const py::object& fmt, const py::object& color,
                float linewidth, const py::object& marker,
                const py::object& linestyle, float markersize,
                float alpha, const std::string& label) {
                 auto a = gca();
                 // mpl plot(y), plot(y, fmt), plot(x, y), plot(x, y, fmt).
                 if (y.is_none())
                     return axesPlot(a, implicitX(x), x, fmt, color,
                                     linewidth, marker, linestyle,
                                     markersize, alpha, label);
                 if (py::isinstance<py::str>(y) && fmt.is_none())
                     return axesPlot(a, implicitX(x), x, y, color,
                                     linewidth, marker, linestyle,
                                     markersize, alpha, label);
                 return axesPlot(a, x, y, fmt, color, linewidth, marker,
                                 linestyle, markersize, alpha, label);
             },
             py::arg("x"), py::arg("y") = py::none(),
             py::arg("fmt") = py::none(), py::arg("color") = py::none(),
             py::arg("linewidth") = 1.5f, py::arg("marker") = py::none(),
             py::arg("linestyle") = py::none(),
             py::arg("markersize") = -1.0f, py::arg("alpha") = -1.0f,
             py::arg("label") = "")
        .def("scatter",
             [](const py::object& x, const py::object& y,
                const py::object& color, float s, const std::string& label) {
                 auto a = gca();
                 return axesScatter(a, x, y, color, s, label);
             },
             py::arg("x"), py::arg("y"), py::arg("color") = py::none(),
             py::arg("s") = 8.0f, py::arg("label") = "")
        .def("xlabel", [](const std::string& s) {
                 gca().ax->style().xAxis.label = s; })
        .def("ylabel", [](const std::string& s) {
                 gca().ax->style().yAxis.label = s; })
        .def("title", [](const std::string& s) { gca().ax->setTitle(s); })
        .def("suptitle", [](const std::string& s) {
                 gcf()->figure_.suptitle(s); })
        .def("xlim", [](float lo, float hi) { gca().ax->setXlim(lo, hi); })
        .def("ylim", [](float lo, float hi) { gca().ax->setYlim(lo, hi); })
        .def("xscale", [](const std::string& s) { gca().ax->setXscale(s); })
        .def("yscale", [](const std::string& s) { gca().ax->setYscale(s); })
        .def("grid",
             [](bool on, const std::string& which,
                const std::string& axis) {
                 gca().ax->grid(on, which, axis);
             },
             py::arg("on") = true, py::arg("which") = "major",
             py::arg("axis") = "both")
        .def("legend", [] { gca().ax->legend(); })
        .def("axis",
             [](const py::object& arg) {
                 auto a = gca();
                 if (arg.is_none()) return;
                 if (py::isinstance<py::str>(arg)) {
                     auto s = arg.cast<std::string>();
                     if (s == "off") {
                         a.ax->style().xAxis.visible = false;
                         a.ax->style().yAxis.visible = false;
                     } else if (s == "on") {
                         a.ax->style().xAxis.visible = true;
                         a.ax->style().yAxis.visible = true;
                     } else if (s == "equal" || s == "square") {
                         a.ax->setAspect(plot::AspectMode::Equal);
                     } else if (s == "auto") {
                         a.ax->setAspect(plot::AspectMode::Auto);
                     } else if (s == "scaled" || s == "image") {
                         a.ax->setAspect(plot::AspectMode::Equal);
                         a.ax->setAdjustable(plot::Adjustable::DataLim);
                     } else if (s == "tight") {
                         a.ax->margins(0.0f);
                     } else
                         throw std::invalid_argument(
                             "unknown axis() arg '" + s + "'");
                     a.ax->touch();
                     return;
                 }
                 auto lims = arg.cast<std::vector<float>>();
                 if (lims.size() != 4)
                     throw std::invalid_argument(
                         "axis([xmin, xmax, ymin, ymax])");
                 a.ax->setXlim(lims[0], lims[1]);
                 a.ax->setYlim(lims[2], lims[3]);
             },
             py::arg("arg") = py::none())
        .def("xticks",
             [](const py::object& ticks, const py::object& labels) {
                 auto a = gca();
                 a.ax->setXLocator(std::make_shared<plot::FixedLocator>(
                     toFloats(ticks)));
                 if (!labels.is_none())
                     a.ax->setXFormatter(
                         std::make_shared<plot::FixedFormatter>(
                             labels.cast<std::vector<std::string>>()));
             },
             py::arg("ticks"), py::arg("labels") = py::none())
        .def("yticks",
             [](const py::object& ticks, const py::object& labels) {
                 auto a = gca();
                 a.ax->setYLocator(std::make_shared<plot::FixedLocator>(
                     toFloats(ticks)));
                 if (!labels.is_none())
                     a.ax->setYFormatter(
                         std::make_shared<plot::FixedFormatter>(
                             labels.cast<std::vector<std::string>>()));
             },
             py::arg("ticks"), py::arg("labels") = py::none())
        .def("minorticks_on", [] { gca().ax->minorticksOn(); })
        .def("tick_params",
             [](const std::string& axis, const std::string& direction,
                float length, float minor_length, float width,
                float minor_width) {
                 gca().ax->tickParams(axis, direction, length,
                                      minor_length, width, minor_width);
             },
             py::arg("axis") = "both", py::arg("direction") = "",
             py::arg("length") = -1.0f, py::arg("minor_length") = -1.0f,
             py::arg("width") = -1.0f, py::arg("minor_width") = -1.0f)
        .def("text",
             [](float x, float y, const std::string& s,
                const std::string& transform, const py::object& color,
                float fontsize) {
                 auto* t = gca().ax->text(x, y, s,
                                          toCoordSystem(transform));
                 if (auto c = parseColor(color); c.a > 0) t->color = c;
                 if (fontsize > 0.0f) t->fontSize = fontsize / 16.0f;
             },
             py::arg("x"), py::arg("y"), py::arg("s"),
             py::arg("transform") = "data", py::arg("color") = py::none(),
             py::arg("fontsize") = -1.0f)
        .def("subplot",
             [](const py::object& a, const py::object& b,
                const py::object& c, const py::object& projection) {
                 auto f = gcf();
                 uint32_t nrows, ncols, index;
                 if (b.is_none()) {
                     int code = a.cast<int>();
                     nrows = uint32_t(code / 100);
                     ncols = uint32_t(code / 10 % 10);
                     index = uint32_t(code % 10);
                 } else {
                     nrows = a.cast<uint32_t>();
                     ncols = b.cast<uint32_t>();
                     index = c.is_none() ? 1u : c.cast<uint32_t>();
                 }
                 uint32_t r = (index - 1) / ncols,
                          col = (index - 1) % ncols;
                 auto* ax = f->figure_.subplot2grid({nrows, ncols},
                                                    {r, col});
                 if (!projection.is_none())
                     ax->setProjection(
                         projection.cast<std::string>());
                 gCurrentAx = ax;
                 return wrapAxes(f, ax);
             },
             py::arg("nrows"), py::arg("ncols") = py::none(),
             py::arg("index") = py::none(),
             py::arg("projection") = py::none(),
             "mpl plt.subplot(111) or subplot(nrows, ncols, index).")
        .def("subplot_mosaic",
             [](const py::object& mosaic) {
                 auto f = gcf();
                 std::vector<std::vector<std::string>> layout;
                 auto addRow = [](std::vector<std::vector<std::string>>& l,
                                  const std::string& row) {
                     l.emplace_back();
                     for (char ch : row)
                         if (!std::isspace(static_cast<unsigned char>(ch)))
                             l.back().push_back(std::string(1, ch));
                 };
                 if (py::isinstance<py::str>(mosaic)) {
                     std::stringstream ss(mosaic.cast<std::string>());
                     std::string row;
                     while (std::getline(ss, row, ';')) {
                         if (row.find_first_not_of(" \t\n") ==
                             std::string::npos)
                             continue;
                         addRow(layout, row);
                     }
                 } else {
                     for (auto row : py::cast<py::sequence>(mosaic)) {
                         if (py::isinstance<py::str>(row))
                             addRow(layout, row.cast<std::string>());
                         else
                             layout.push_back(
                                 row.cast<std::vector<std::string>>());
                     }
                 }
                 py::dict out;
                 for (auto& [k, v] : f->figure_.subplotMosaic(layout))
                     out[py::str(k)] = wrapAxes(f, v);
                 return out;
             },
             py::arg("mosaic"))
        .def("twinx", [] {
                 auto a = gca();
                 auto* t = a.ax->twinx();
                 if (!t) throw std::runtime_error("twinx failed");
                 return wrapAxes(a.owner, t);
             })
        .def("twiny", [] {
                 auto a = gca();
                 auto* t = a.ax->twiny();
                 if (!t) throw std::runtime_error("twiny failed");
                 return wrapAxes(a.owner, t);
             })
        .def("colorbar",
             [](const py::object& axObj, const std::string& orientation,
                float fraction, float pad, float shrink,
                const std::string& label) {
                 auto f = gcf();
                 plot::Axes* target = nullptr;
                 if (!axObj.is_none())
                     target = axObj.cast<PyAxes>().ax;
                 else if (gCurrentAx) target = gCurrentAx;
                 else if (auto all = f->figure_.allAxes(); !all.empty())
                     target = all.back();
                 if (!target)
                     throw std::invalid_argument(
                         "colorbar: no axes to attach to");
                 auto& cb = target->style().colorbar;
                 cb.visible = true;
                 cb.orientation = orientation;
                 cb.fraction = fraction;
                 cb.pad = pad;
                 cb.shrink = shrink;
                 if (!label.empty()) cb.label = label;
                 target->touch();
             },
             py::arg("ax") = py::none(),
             py::arg("orientation") = "vertical",
             py::arg("fraction") = 0.15f, py::arg("pad") = 0.05f,
             py::arg("shrink") = 1.0f, py::arg("label") = "")
        .def("savefig", [](const std::string& path) {
                 return gcf()->savefig(path); },
             py::arg("path"))
        .def("cla", [] {
                 gca().ax->plots().clear();
                 gca().ax->touch();
             })
        .def("clf", [] {
                 // Recreate the figure (Axes pointers dangle).
                 auto f = std::make_shared<PyFigure>(800, 600, 100.0f);
                 gCurrentFig = f;
                 gCurrentAx = nullptr;
             })
        .def("close", [] {
                 gCurrentFig.reset();
                 gCurrentAx = nullptr;
             })
        // ── Reference lines / layout on the current axes/figure ──
        .def("axhline",
             [](float y, const py::object& color, float lw) {
                 gca().ax->axhline(
                     y, lineColor(color, plot::Color::black()), lw);
             },
             py::arg("y") = 0.0f, py::arg("color") = py::none(),
             py::arg("linewidth") = 1.0f)
        .def("axvline",
             [](float x, const py::object& color, float lw) {
                 gca().ax->axvline(
                     x, lineColor(color, plot::Color::black()), lw);
             },
             py::arg("x") = 0.0f, py::arg("color") = py::none(),
             py::arg("linewidth") = 1.0f)
        .def("axhspan",
             [](float ymin, float ymax, const py::object& color) {
                 gca().ax->axhspan(
                     ymin, ymax,
                     lineColor(color,
                               plot::Color::fromRgba8(200, 200, 200, 128)));
             },
             py::arg("ymin"), py::arg("ymax"),
             py::arg("color") = py::none())
        .def("axvspan",
             [](float xmin, float xmax, const py::object& color) {
                 gca().ax->axvspan(
                     xmin, xmax,
                     lineColor(color,
                               plot::Color::fromRgba8(200, 200, 200, 128)));
             },
             py::arg("xmin"), py::arg("xmax"),
             py::arg("color") = py::none())
        .def("hlines",
             [](const py::object& y, float xmin, float xmax,
                const py::object& color, float lw) {
                 gca().ax->hlines(
                     toFloatList(y), xmin, xmax,
                     lineColor(color, plot::Color::black()), lw);
             },
             py::arg("y"), py::arg("xmin"), py::arg("xmax"),
             py::arg("color") = py::none(), py::arg("linewidth") = 1.0f)
        .def("vlines",
             [](const py::object& x, float ymin, float ymax,
                const py::object& color, float lw) {
                 gca().ax->vlines(
                     toFloatList(x), ymin, ymax,
                     lineColor(color, plot::Color::black()), lw);
             },
             py::arg("x"), py::arg("ymin"), py::arg("ymax"),
             py::arg("color") = py::none(), py::arg("linewidth") = 1.0f)
        .def("tight_layout", [] {
                 gcf()->figure_.setTightLayout(true); })
        .def("subplots_adjust",
             [](float left, float bottom, float right, float top,
                float wspace, float hspace) {
                 gcf()->figure_.subplotsAdjust(left, bottom, right, top,
                                               wspace, hspace);
             },
             py::arg("left") = 0.125f, py::arg("bottom") = 0.11f,
             py::arg("right") = 0.9f, py::arg("top") = 0.88f,
             py::arg("wspace") = 0.2f, py::arg("hspace") = 0.2f)
        .def("figlegend",
             [](std::string loc) { gcf()->figure_.legend(loc); },
             py::arg("loc") = "upper right")
        // ── rc configuration (mpl matplotlib.rc / rcParams) ──
        .def("rc",
             [](const std::string& group, const py::kwargs& kw) {
                 for (auto [k, v] : kw) {
                     std::string key = k.cast<std::string>();
                     if (!group.empty() &&
                         key.find('.') == std::string::npos)
                         key = group + "." + key;
                     gRcParams->set(
                         key, py::reinterpret_borrow<py::object>(v));
                 }
             },
             py::arg("group") = "",
             "mpl rc(group, **kwargs) — e.g. rc('lines', linewidth=2).")
        .def("rcdefaults", [] {
                 plot::rc::rcdefaults();
                 gRcParams->clear();
             })
        .def("rc_context",
             [](const py::kwargs& kw) {
                 auto ctx = std::make_shared<PyStyleContext>(
                     plot::rc::Context{});
                 for (auto [k, v] : kw)
                     gRcParams->set(
                         k.cast<std::string>(),
                         py::reinterpret_borrow<py::object>(v));
                 return ctx;
             })
        .def("show", [] {
                 // Headless backend: nothing to display — savefig is the
                 // output path (mirrors mpl's Agg backend behavior).
             });

    py::class_<PyRcParams, std::shared_ptr<PyRcParams>>(m, "RcParams")
        .def("__setitem__", &PyRcParams::set)
        .def("__getitem__", &PyRcParams::get)
        .def("__contains__", &PyRcParams::contains)
        .def("keys", &PyRcParams::keys);
    m.attr("rcParams") = gRcParams;

    py::class_<PyStyleContext, std::shared_ptr<PyStyleContext>>(
        m, "RcContext")
        .def("__enter__", &PyStyleContext::enter)
        .def("__exit__", &PyStyleContext::exit);

    // plt.style submodule: use / available / context
    auto styleMod = m.def_submodule("style");
    styleMod.def("use",
                 [](const py::object& name) {
                     if (py::isinstance<py::str>(name))
                         return plot::style::use(
                             name.cast<std::string>());
                     return plot::style::use(
                         name.cast<std::vector<std::string>>());
                 })
        .def("available", [] { return plot::style::available(); })
        .def("context",
             [](const py::object& name) {
                 if (py::isinstance<py::str>(name))
                     return std::make_shared<PyStyleContext>(
                         plot::style::context(name.cast<std::string>()));
                 return std::make_shared<PyStyleContext>(
                     plot::style::context(
                         name.cast<std::vector<std::string>>()));
             });
}
