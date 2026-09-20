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
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>

#include <format>
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

/// Categorical x: true when the input is a sequence of strings.
bool isStringSeq(const py::object& obj) {
    if (!py::isinstance<py::sequence>(obj) || py::isinstance<py::str>(obj))
        return false;
    py::sequence seq = py::reinterpret_borrow<py::sequence>(obj);
    if (seq.size() == 0) return false;
    return py::isinstance<py::str>(seq[0]);
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

PyAxes wrapAxes(const std::shared_ptr<PyFigure>& fig, plot::Axes* ax) {
    if (!ax) throw std::runtime_error("add_axes failed");
    return {fig, ax};
}

} // namespace

PYBIND11_MODULE(volcanoplot, m) {
    m.doc() = "VolcanoPlot — Vulkan-accelerated matplotlib-style plotting";

    py::class_<PyFigure, std::shared_ptr<PyFigure>>(m, "Figure")
        .def(py::init<uint32_t, uint32_t, float>(),
             py::arg("width") = 800, py::arg("height") = 600,
             py::arg("dpi") = 100.0f)
        .def("add_axes", [](const std::shared_ptr<PyFigure>& f) {
                 return wrapAxes(f, f->addAxes());
             })
        .def("add_axes_fraction",
             [](const std::shared_ptr<PyFigure>& f, float l, float b,
                float w, float h) {
                 return wrapAxes(f, f->addAxesFraction(l, b, w, h));
             },
             py::arg("left"), py::arg("bottom"), py::arg("width"), py::arg("height"))
        .def("suptitle", [](PyFigure& f, std::string t) {
                 f.figure_.suptitle(std::move(t));
             })
        .def("supxlabel", [](PyFigure& f, std::string t) {
                 f.figure_.supxlabel(std::move(t));
             })
        .def("supylabel", [](PyFigure& f, std::string t) {
                 f.figure_.supylabel(std::move(t));
             })
        .def("savefig", &PyFigure::savefig, py::arg("path"),
             "Render and save (png/webp/bmp/jpg/tiff/pdf/svg/eps).");

    py::class_<PyAxes>(m, "Axes")
        .def("plot",
             [](PyAxes& a, const py::object& x,
                const py::object& y, const py::object& color,
                float linewidth, const std::string& label) {
                 bool xDate = false, yDate = false;
                 auto xv = toFloats(x, &xDate), yv = toFloats(y, &yDate);
                 if (xDate) a.ax->xaxis_date();
                 if (yDate) a.ax->yaxis_date();
                 auto s = makeSeries(xv, yv);
                 if (auto c = parseColor(color); c.a > 0) s.color = c;
                 s.label = label;
                 s.lineWidth = linewidth;
                 a.ax->addPlot(std::make_unique<plot::LinePlot>(std::move(s)));
             },
             py::arg("x"), py::arg("y"), py::arg("color") = py::none(),
             py::arg("linewidth") = 1.5f, py::arg("label") = "")
        .def("scatter",
             [](PyAxes& a, const py::object& x,
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
                 a.ax->addPlot(std::make_unique<plot::ScatterPlot>(std::move(ser)));
             },
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
             },
             py::arg("x"), py::arg("height"), py::arg("color") = py::none(),
             py::arg("width") = 0.8f, py::arg("label") = "")
        .def("imshow",
             [](PyAxes& a, const std::vector<std::vector<float>>& values,
                const std::string& cmapName) {
                 if (values.empty() || values[0].empty())
                     throw std::invalid_argument("imshow needs a non-empty 2D array");
                 plot::Grid2D g;
                 g.height = uint32_t(values.size());
                 g.width = uint32_t(values[0].size());
                 g.xRange = {0.0f, float(g.width)};
                 g.yRange = {0.0f, float(g.height)};
                 g.values.reserve(g.width * g.height);
                 for (auto& row : values) {
                     if (row.size() != g.width)
                         throw std::invalid_argument("ragged 2D array");
                     g.values.insert(g.values.end(), row.begin(), row.end());
                 }
                 const plot::Colormap* cmap = &plot::colormaps::viridis();
                 try { cmap = &plot::Colormap::byName(cmapName); }
                 catch (...) {}
                 a.ax->addPlot(std::make_unique<plot::HeatmapPlot>(std::move(g), *cmap));
             },
             py::arg("values"), py::arg("cmap") = "viridis")
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
        .def("grid", [](PyAxes& a, bool on) { a.ax->grid(on); },
             py::arg("on") = true)
        .def("legend", [](PyAxes& a) -> plot::LegendStyle& { return a.ax->legend(); },
             py::return_value_policy::reference_internal)
        .def("set_projection", [](PyAxes& a, std::string p) {
                 a.ax->setProjection(p);
             });

    py::class_<plot::LegendStyle>(m, "Legend")
        .def_readwrite("location", &plot::LegendStyle::location)
        .def_readwrite("visible", &plot::LegendStyle::visible)
        .def_readwrite("frameAlpha", &plot::LegendStyle::frameAlpha);
}
