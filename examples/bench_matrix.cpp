// examples/bench_matrix.cpp — native C++ leg of the benchmark matrix.
//
// Replays the same cases as scripts/bench_matrix.py on the raw C++ API
// (no Python, no matplotlib). Input arrays are the .npy files dumped by
// `python3 scripts/bench_matrix.py dump` (data/<fn>/*.npy), so all three
// stacks render byte-identical inputs at identical size/dpi.
//
// Usage: example_bench_matrix <outdir> [--repeats N]
//
// Writes <outdir>/<case>_cpp.png, <outdir>/bench_matrix_cpp.json and
// prints progress lines in the same format as the Python stacks.
//
// Timing policy mirrors the Python harness: Vulkan instance/device and
// pipeline warm-up happen once in a dedicated warm-up figure whose cost
// is reported separately as "_init"; each case gets one untimed rep
// (first-use pipeline compile) before the timed reps, so the reported
// median is steady-state — "the user wants to make 100 plots".

#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/HistPlot.hpp>
#include <volcano/plot/plots/BoxPlot.hpp>
#include <volcano/plot/plots/ViolinPlot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/QuiverPlot.hpp>
#include <volcano/plot/plots/StemPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/PcolormeshPlot.hpp>
#include <volcano/plot/plots/StackPlot.hpp>
#include <volcano/plot/plots/PiePlot.hpp>
#include <volcano/plot/plots/ReferenceLines.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <print>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace volcano;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0)
        .count();
}

// ── minimal .npy reader (v1/v2, C-order, <f8 <f4 <i8 <i4 |u1 <u8) ──

struct Npy {
    std::vector<uint32_t> dims;
    std::vector<float> data;  // row-major
};

static Npy loadNpy(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("not npy: " + p.string());
    uint32_t hlen;
    if (magic[6] == 1) {
        uint16_t l;
        f.read(reinterpret_cast<char*>(&l), 2);
        hlen = l;
    } else {
        f.read(reinterpret_cast<char*>(&hlen), 4);
    }
    std::string hdr(hlen, ' ');
    f.read(hdr.data(), hlen);

    auto find = [&](const std::string& key) -> std::string {
        auto i = hdr.find("'" + key + "'");
        if (i == std::string::npos) i = hdr.find("\"" + key + "\"");
        if (i == std::string::npos) return {};
        i = hdr.find(':', i);
        ++i;
        // tuple values (shape) may contain commas — slice to the ')'
        if (key == "shape") {
            auto lp = hdr.find('(', i), rp = hdr.find(')', lp);
            if (lp == std::string::npos || rp == std::string::npos)
                return {};
            return hdr.substr(lp, rp - lp + 1);
        }
        auto j = hdr.find_first_of(",}", i);
        return hdr.substr(i, j - i);
    };
    std::string descr = find("descr");
    descr.erase(std::remove_if(descr.begin(), descr.end(), ::isspace),
                descr.end());
    descr.erase(std::remove(descr.begin(), descr.end(), '\''),
                descr.end());
    descr.erase(std::remove(descr.begin(), descr.end(), '"'),
                descr.end());
    std::string shape = find("shape");  // e.g. " (1200, 1600)"
    Npy out;
    {
        auto lp = shape.find('('), rp = shape.find(')', lp);
        if (lp == std::string::npos || rp == std::string::npos)
            throw std::runtime_error("npy bad shape: " + p.string());
        for (size_t i = lp + 1; i < rp; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(shape[i])))
                continue;
            size_t j = shape.find_first_not_of("0123456789", i);
            out.dims.push_back(uint32_t(std::stoul(shape.substr(i, j - i))));
            i = j;
        }
    }
    if (out.dims.empty()) out.dims.push_back(1);
    if (out.dims.size() > 2)
        throw std::runtime_error("npy >2D unsupported: " + p.string());
    size_t n = std::accumulate(out.dims.begin(), out.dims.end(), 1ull,
                               std::multiplies<>());
    out.data.resize(n);
    auto rd = [&](auto tag) {
        using T = decltype(tag);
        std::vector<T> buf(n);
        f.read(reinterpret_cast<char*>(buf.data()), n * sizeof(T));
        for (size_t i = 0; i < n; ++i) out.data[i] = float(buf[i]);
    };
    if (descr == "<f8") rd(double{});
    else if (descr == "<f4") rd(float{});
    else if (descr == "<i8") rd(int64_t{});
    else if (descr == "<i4") rd(int32_t{});
    else if (descr == "|u1" || descr == "<u1") rd(uint8_t{});
    else if (descr == "<u8") rd(uint64_t{});
    else throw std::runtime_error("npy dtype " + descr + ": " +
                                  p.string());
    return out;
}

// ── per-case data directory ──────────────────────────────────────────

struct CaseData {
    fs::path dir;
    [[nodiscard]] std::vector<float> v(const std::string& k) const {
        return loadNpy(dir / (k + ".npy")).data;
    }
    [[nodiscard]] Npy a(const std::string& k) const {
        return loadNpy(dir / (k + ".npy"));
    }
    [[nodiscard]] std::vector<std::vector<float>>
    rows(const std::string& k) const {
        Npy n = a(k);
        if (n.dims.size() == 1)
            return {std::move(n.data)};
        std::vector<std::vector<float>> r(n.dims[0]);
        for (uint32_t i = 0; i < n.dims[0]; ++i)
            r[i].assign(n.data.begin() + i * n.dims[1],
                        n.data.begin() + (i + 1) * n.dims[1]);
        return r;
    }
};

// ── helpers ──────────────────────────────────────────────────────────

static plot::Series2D mkSeries(std::vector<float> x,
                               std::vector<float> y) {
    plot::Series2D s;
    const size_t n = std::min(x.size(), y.size());
    s.points.reserve(n);
    for (size_t i = 0; i < n; ++i) s.points.push_back({x[i], y[i]});
    return s;
}

static void addLine(plot::Axes& ax, std::vector<float> x,
                    std::vector<float> y, std::string label = {}) {
    auto s = mkSeries(std::move(x), std::move(y));
    s.label = std::move(label);
    ax.addPlot(std::make_unique<plot::LinePlot>(std::move(s)));
}

static void addScatter(plot::Axes& ax, std::vector<float> x,
                       std::vector<float> y, float sizePx,
                       float alpha = 1.0f) {
    auto s = mkSeries(std::move(x), std::move(y));
    s.size = sizePx;
    s.alpha = alpha;
    ax.addPlot(std::make_unique<plot::ScatterPlot>(std::move(s)));
}

// ── case builders: mirror the c_* functions in bench_matrix.py ───────

using Builder = void (*)(plot::Axes&, const CaseData&, float dpi);

static void b_line_100(plot::Axes& ax, const CaseData& d, float) {
    addLine(ax, d.v("plot0.0"), d.v("plot0.1"));
    ax.setTitle("line 100");
}

static void b_line_2k(plot::Axes& ax, const CaseData& d, float dpi) {
    for (int i = 0; i < 3; ++i)
        addLine(ax, d.v(std::format("plot{}.0", i)),
                d.v(std::format("plot{}.1", i)),
                std::format("s{}", i));
    addScatter(ax, d.v("scatter0.0"), d.v("scatter0.1"),
               std::sqrt(8.0f) * dpi / 72.0f, 0.5f);
    ax.legend();
    ax.setTitle("line 2k + scatter");
}

static void b_scatter_100(plot::Axes& ax, const CaseData& d, float dpi) {
    auto s = mkSeries(d.v("scatter0.0"), d.v("scatter0.1"));
    s.size = std::sqrt(30.0f) * dpi / 72.0f;
    auto p = std::make_unique<plot::ScatterPlot>(std::move(s));
    p->setArray(d.v("scatter0.c"));
    ax.addPlot(std::move(p));
}

static void b_bar(plot::Axes& ax, const CaseData& d, float) {
    plot::BarData bd;
    bd.heights = d.v("bar0.1");
    ax.addPlot(std::make_unique<plot::BarPlot>(std::move(bd)));
}

static void b_errorbar_50(plot::Axes& ax, const CaseData& d, float dpi) {
    plot::ErrorbarConfig cfg;
    cfg.yerr = d.v("errorbar0.yerr");
    cfg.drawCaps = true;
    cfg.capSize = 3.0f * dpi / 72.0f;
    ax.addPlot(std::make_unique<plot::ErrorbarPlot>(d.v("errorbar0.0"),
                                                  d.v("errorbar0.1"),
                                                  std::move(cfg)));
}

static void b_boxplot(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(
        std::make_unique<plot::BoxPlot>(d.rows("boxplot0.0")));
}

static void b_text_50(plot::Axes& ax, const CaseData&, float) {
    for (int i = 0; i < 50; ++i) {
        auto* t = ax.text(0.02f + (i % 10) * 0.09f,
                          0.05f + (i / 10) * 0.18f,
                          std::format("label {}", i));
        t->fontSize = 7.0f;
    }
    ax.setXlim(0, 1);
    ax.setYlim(0, 1);
}

static void b_pie(plot::Axes& ax, const CaseData& d, float) {
    plot::PieData pd;
    pd.values = d.v("pie0.0");
    pd.labels = {"a", "b", "c", "d"};
    pd.autopct = "%1.0f%%";
    ax.pie(std::move(pd));
}

static void b_line(plot::Axes& ax, const CaseData& d, float) {
    addLine(ax, d.v("plot0.0"), d.v("plot0.1"));
}

static void b_multiline(plot::Axes& ax, const CaseData& d, float) {
    for (int i = 0; i < 20; ++i)
        addLine(ax, d.v(std::format("plot{}.0", i)),
                d.v(std::format("plot{}.1", i)));
}

static void b_scatter_10k(plot::Axes& ax, const CaseData& d, float dpi,
                          float sPt = 10.0f, float alpha = 0.7f) {
    auto s = mkSeries(d.v("scatter0.0"), d.v("scatter0.1"));
    s.size = std::sqrt(sPt) * dpi / 72.0f;
    s.alpha = alpha;
    auto p = std::make_unique<plot::ScatterPlot>(std::move(s));
    p->setArray(d.v("scatter0.c"));
    ax.addPlot(std::move(p));
}

static void b_scatter_10k_(plot::Axes& a, const CaseData& d, float dpi) {
    b_scatter_10k(a, d, dpi, 10.0f, 0.7f);
}
static void b_scatter_50k_(plot::Axes& a, const CaseData& d, float dpi) {
    b_scatter_10k(a, d, dpi, 6.0f, 0.7f);
}
static void b_scatter_200k_(plot::Axes& a, const CaseData& d, float dpi) {
    b_scatter_10k(a, d, dpi, 4.0f, 0.6f);
}

static void b_scatter_1M(plot::Axes& ax, const CaseData& d, float dpi) {
    auto s = mkSeries(d.v("scatter0.0"), d.v("scatter0.1"));
    s.size = dpi / 72.0f;
    s.setMarker(".");
    auto p = std::make_unique<plot::ScatterPlot>(std::move(s));
    p->setArray(d.v("scatter0.c"));
    ax.addPlot(std::move(p));
}

static void b_scatter_2M(plot::Axes& ax, const CaseData& d, float dpi) {
    auto s = mkSeries(d.v("scatter0.0"), d.v("scatter0.1"));
    s.size = dpi / 72.0f;
    s.setMarker(".");
    s.color = plot::Color::fromRgba8(31, 119, 180);  // tab:blue
    ax.addPlot(std::make_unique<plot::ScatterPlot>(std::move(s)));
}

static void b_scatter_sz(plot::Axes& ax, const CaseData& d, float dpi,
                         bool plasma) {
    auto s = mkSeries(d.v("scatter0.0"), d.v("scatter0.1"));
    auto p = std::make_unique<plot::ScatterPlot>(std::move(s));
    std::vector<float> sz = d.v("scatter0.s");
    for (auto& v : sz) v = std::sqrt(v) * dpi / 72.0f;
    p->setSizes(std::move(sz));
    p->setArray(d.v("scatter0.c"));
    if (plasma) p->setCmap(plot::colormaps::plasma());
    ax.addPlot(std::move(p));
}

static void b_scatter_sz_200k_(plot::Axes& a, const CaseData& d, float dpi) {
    b_scatter_sz(a, d, dpi, false);
}
static void b_scatter_sz_1M_(plot::Axes& a, const CaseData& d, float dpi) {
    b_scatter_sz(a, d, dpi, true);
}

static void b_hist_100k(plot::Axes& ax, const CaseData& d, float) {
    plot::HistConfig cfg;
    cfg.bins = plot::HistBinMethod::Fixed;
    cfg.binCount = 100;
    ax.addPlot(
        std::make_unique<plot::HistPlot>(d.v("hist0.0"), cfg));
}

static void b_hist_10M(plot::Axes& ax, const CaseData& d, float) {
    plot::HistConfig cfg;
    cfg.bins = plot::HistBinMethod::Fixed;
    cfg.binCount = 200;
    ax.addPlot(
        std::make_unique<plot::HistPlot>(d.v("hist0.0"), cfg));
}

static void b_contourf_100(plot::Axes& ax, const CaseData& d, float) {
    Npy z = d.a("contourf0.2");
    plot::Grid2D g;
    g.values = std::move(z.data);
    g.width = z.dims[1];
    g.height = z.dims[0];
    g.xRange = {0, 1};
    g.yRange = {0, 1};
    plot::ContourConfig cfg;
    cfg.numLevels = 15;
    cfg.cmap = &plot::colormaps::viridis();
    ax.addPlot(
        std::make_unique<plot::ContourfPlot>(std::move(g), cfg));
}

static void b_contourf_400(plot::Axes& ax, const CaseData& d, float) {
    Npy z = d.a("contourf0.2");
    plot::Grid2D g;
    g.values = std::move(z.data);
    g.width = z.dims[1];
    g.height = z.dims[0];
    g.xRange = {0, 1};
    g.yRange = {0, 1};
    plot::ContourConfig cfg;
    cfg.numLevels = 20;
    cfg.cmap = &plot::colormaps::viridis();
    ax.addPlot(
        std::make_unique<plot::ContourfPlot>(std::move(g), cfg));
}

static void b_quiver(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::QuiverPlot>(
        d.v("quiver0.0"), d.v("quiver0.1"), d.v("quiver0.2"),
        d.v("quiver0.3")));
}

static void b_step(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::StepPlot>(d.v("step0.0"),
                                                d.v("step0.1"),
                                                plot::StepWhere::Mid));
}

static void b_fill_100k(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::FillBetweenPlot>(
        d.v("fill_between0.0"), d.v("fill_between0.1"),
        d.v("fill_between0.2"),
        plot::Color::fromRgba8(31, 119, 180, 153)));  // alpha .6
}

static void b_fill_1M(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::FillBetweenPlot>(
        d.v("fill_between0.0"), d.v("fill_between0.1"),
        d.v("fill_between0.2"),
        plot::Color::fromRgba8(31, 119, 180, 178)));  // alpha .7
}

static void b_eventplot(plot::Axes& ax, const CaseData& d, float) {
    auto rows = d.rows("eventplot0.0");
    auto& ep = ax.eventplot(rows);
    ep.linelengths.assign(rows.size(), 0.8f);
}

static void b_stackplot(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::StackPlot>(
        d.v("stackplot0.0"), d.rows("stackplot0.1")));
}

static void b_errorbar_5k(plot::Axes& ax, const CaseData& d, float) {
    plot::ErrorbarConfig cfg;
    cfg.yerr = d.v("errorbar0.yerr");
    cfg.drawMarker = false;
    ax.addPlot(std::make_unique<plot::ErrorbarPlot>(d.v("errorbar0.0"),
                                                  d.v("errorbar0.1"),
                                                  std::move(cfg)));
}

static void b_violin(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::ViolinPlot>(
        d.rows("violinplot0.0")));
}

static void b_stem(plot::Axes& ax, const CaseData& d, float) {
    ax.addPlot(std::make_unique<plot::StemPlot>(d.v("stem0.0"),
                                                d.v("stem0.1")));
}

static void b_markers_100k(plot::Axes& ax, const CaseData& d, float dpi) {
    auto s = mkSeries(d.v("plot0.0"), d.v("plot0.1"));
    s.setMarker("o");
    s.size = 3.0f * dpi / 72.0f;
    s.lineStyle = plot::LineStyle::None;
    ax.addPlot(std::make_unique<plot::LinePlot>(std::move(s)));
}

static void b_pcolormesh(plot::Axes& ax, const CaseData& d, float) {
    Npy c = d.a("pcolormesh0.2");
    plot::PcolormeshConfig cfg;
    cfg.cmap = &plot::colormaps::viridis();
    ax.addPlot(std::make_unique<plot::PcolormeshPlot>(
        d.v("pcolormesh0.0"), d.v("pcolormesh0.1"), std::move(c.data),
        c.dims[1], c.dims[0], cfg));
}

static void b_imshow_none(plot::Axes& ax, const CaseData& d, float) {
    Npy g = d.a("imshow0.0");
    plot::Grid2D grid;
    grid.values = std::move(g.data);
    grid.width = g.dims[1];
    grid.height = g.dims[0];
    ax.imshow(std::move(grid), plot::colormaps::viridis(), "nearest");
}

static void b_imshow_bilinear(plot::Axes& ax, const CaseData& d, float) {
    Npy g = d.a("imshow0.0");
    plot::Grid2D grid;
    grid.values = std::move(g.data);
    grid.width = g.dims[1];
    grid.height = g.dims[0];
    ax.imshow(std::move(grid), plot::colormaps::viridis(), "bilinear");
}

static void b_multi(plot::Axes& ax, const CaseData& d, float) {
    for (int j = 0; j < 3; ++j)
        addLine(ax, d.v(std::format("plot{}.0", j)),
                d.v(std::format("plot{}.1", j)));
}

static const std::unordered_map<std::string, Builder> BUILDERS = {
    {"line_100", b_line_100},
    {"line_2k", b_line_2k},
    {"scatter_100", b_scatter_100},
    {"bar_10", b_bar},
    {"bar_1k", b_bar},
    {"bar_5k", b_bar},
    {"bar_50k", b_bar},
    {"errorbar_50", b_errorbar_50},
    {"boxplot", b_boxplot},
    {"text_50", b_text_50},
    {"pie", b_pie},
    {"line_10k", b_line},
    {"line_100k", b_line},
    {"line_1M", b_line},
    {"line_10M", b_line},
    {"multiline_20x100k", b_multiline},
    {"multi_line2k", b_multi},
    {"scatter_10k", b_scatter_10k_},
    {"scatter_50k", b_scatter_50k_},
    {"scatter_200k", b_scatter_200k_},
    {"scatter_1M", b_scatter_1M},
    {"scatter_2M", b_scatter_2M},
    {"scatter_sz_200k", b_scatter_sz_200k_},
    {"scatter_sz_1M", b_scatter_sz_1M_},
    {"hist_100k", b_hist_100k},
    {"hist_10M", b_hist_10M},
    {"contourf_100", b_contourf_100},
    {"contourf_400", b_contourf_400},
    {"quiver_30x30", b_quiver},
    {"step_50k", b_step},
    {"fill_100k", b_fill_100k},
    {"fill_1M", b_fill_1M},
    {"eventplot_2k", b_eventplot},
    {"eventplot_20k", b_eventplot},
    {"stackplot", b_stackplot},
    {"errorbar_5k", b_errorbar_5k},
    {"violin", b_violin},
    {"stem_2k", b_stem},
    {"markers_100k", b_markers_100k},
    {"pcolormesh_2M", b_pcolormesh},
    {"pcolormesh_7M", b_pcolormesh},
    {"quadmesh_1M", b_pcolormesh},
    {"imshow_4M_none", b_imshow_none},
    {"imshow_16M_none", b_imshow_none},
    {"imshow_9M_bilinear", b_imshow_bilinear},
};

// ── backend/renderer pool (mirrors the binding's per-size pooling) ────

struct Ctx {
    std::unique_ptr<backend::IBackend> backend;
    std::unique_ptr<render::Renderer> renderer;
};

static std::map<std::pair<uint32_t, uint32_t>, Ctx>& pool() {
    static std::map<std::pair<uint32_t, uint32_t>, Ctx> p;
    return p;
}

static render::Renderer& rendererFor(uint32_t w, uint32_t h) {
    auto& p = pool();
    auto it = p.find({w, h});
    if (it == p.end()) {
        Ctx c;
        backend::BackendDesc desc;
        desc.width = w;
        desc.height = h;
        desc.samples = vk::SampleCountFlagBits::e4;
        c.backend =
            backend::createHeadlessBackend(desc,
                                           backend::sharedGpuContext());
        c.renderer = std::make_unique<render::Renderer>(*c.backend);
        it = p.emplace(std::pair{w, h}, std::move(c)).first;
    }
    return *it->second.renderer;
}

// ── warm-up / init ───────────────────────────────────────────────────

static double warmup(const fs::path& outdir) {
    // Touches device init + the common pipelines (line, scatter, bar,
    // image, text, fill). Reported as "_init", never added to a case.
    auto t0 = Clock::now();
    auto& r = rendererFor(640, 480);
    plot::Figure fig(1, 1);
    fig.style().dpi = 100;
    auto* ax = fig.addAxes();
    std::vector<float> x(100), y(100);
    for (size_t i = 0; i < 100; ++i) x[i] = y[i] = float(i) / 99.f;
    addLine(*ax, x, y);
    std::vector<float> sx(20), sy(20);
    for (size_t i = 0; i < 20; ++i) sx[i] = sy[i] = float(i) / 19.f;
    addScatter(*ax, sx, sy, 5.0f);
    plot::BarData bd;
    bd.heights = {1.f, 2.f, .5f, 1.5f};
    ax->addPlot(std::make_unique<plot::BarPlot>(std::move(bd)));
    plot::Grid2D g;
    g.width = g.height = 8;
    g.values.assign(64, 0.5f);
    ax->imshow(std::move(g));
    ax->text(0.5f, 0.5f, "warmup");
    (void)r.savefig(fig, outdir / "_warmup.png");
    return ms(t0);
}

// ── main ─────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    fs::path outdir = argc > 1 ? argv[1] : "gallery/benchmark";
    int repeats = 3;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--repeats")
            repeats = std::stoi(argv[++i]);
    fs::path datadir = outdir / "data";
    fs::create_directories(outdir);

    // manifest: case<TAB>dpi<TAB>data<TAB>cat
    struct Case {
        std::string name, data, cat;
        float dpi;
    };
    std::vector<Case> cases;
    {
        std::ifstream mf(datadir / "manifest.tsv");
        if (!mf) {
            std::print(stderr, "no manifest at {} — run "
                               "bench_matrix.py dump first\n",
                       (datadir / "manifest.tsv").string());
            return 1;
        }
        std::string line;
        while (std::getline(mf, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            Case c;
            std::string dpi;
            std::getline(ss, c.name, '\t');
            std::getline(ss, dpi, '\t');
            std::getline(ss, c.data, '\t');
            std::getline(ss, c.cat, '\t');
            c.dpi = std::stof(dpi);
            cases.push_back(std::move(c));
        }
    }

    double initMs = warmup(outdir);
    std::print("[cpp   ] _init                  {:9.1f} ms\n", initMs);
    std::fflush(stdout);

    std::ostringstream js;
    js << "{\n \"_init\": {\"cat\":\"meta\",\"ms\":" << initMs
       << ",\"err\":null},\n";
    bool first = true;

    auto emit = [&](const Case& c, double medMs, const char* err) {
        if (!first) js << ",\n";
        first = false;
        js << " \"" << c.name << "\": {\"cat\":\"" << c.cat << "\"";
        if (err)
            js << ",\"ms\":null,\"err\":\"" << err << "\"";
        else
            js << ",\"ms\":" << medMs << ",\"err\":null";
        js << "}";
    };

    for (const Case& c : cases) {
        if (c.name == "multi_50x_line2k") continue;  // handled below
        auto bit = BUILDERS.find(c.data);
        if (bit == BUILDERS.end()) {
            emit(c, 0, "unsupported");
            std::print("[cpp   ] {:<22} ERR unsupported\n", c.name);
            continue;
        }
        const uint32_t w = uint32_t(6.4 * c.dpi + 0.5);
        const uint32_t h = uint32_t(4.8 * c.dpi + 0.5);
        std::vector<double> ts;
        const char* err = nullptr;
        try {
            CaseData cd{datadir / c.data};
            for (int rep = 0; rep <= repeats; ++rep) {
                // rep 0 untimed: first-use pipeline compile warm-up
                plot::Figure fig(1, 1);
                fig.style().dpi = c.dpi;
                auto* ax = fig.addAxes();
                bit->second(*ax, cd, c.dpi);
                auto t0 = Clock::now();
                bool ok = rendererFor(w, h).savefig(
                    fig, outdir / (c.name + "_cpp.png"));
                if (rep > 0) ts.push_back(ms(t0));
                if (!ok) { err = "savefig failed"; break; }
            }
        } catch (const std::exception& e) {
            err = "exception";
            std::print(stderr, "  {}: {}\n", c.name, e.what());
        }
        if (err) {
            emit(c, 0, err);
            std::print("[cpp   ] {:<22} ERR {}\n", c.name, err);
        } else {
            std::ranges::sort(ts);
            double med = ts[ts.size() / 2];
            emit(c, med, nullptr);
            std::print("[cpp   ] {:<22} {:9.1f} ms\n", c.name, med);
        }
        std::fflush(stdout);
    }

    // multi-figure workload: 50 figures, total/50 — amortized cost
    {
        Case multi{"multi_50x_line2k", "multi_line2k", "multi", 100};
        auto t0 = Clock::now();
        CaseData cd{datadir / "multi_line2k"};
        std::vector<std::future<bool>> pending;
        pending.reserve(50);
        for (int i = 0; i < 50; ++i) {
            plot::Figure fig(1, 1);
            fig.style().dpi = 100;
            b_multi(*fig.addAxes(), cd, 100.f);
            // savefigAsync overlaps figure N's CPU encode+write with
            // figure N+1's GPU render — the "make 100 plots" workload.
            pending.push_back(rendererFor(640, 480).savefigAsync(
                fig, outdir / "multi_50x_line2k_cpp.png"));
        }
        for (auto& f : pending) (void)f.get();
        double per = ms(t0) / 50.0;
        emit(multi, per, nullptr);
        std::print("[cpp   ] {:<22} {:9.1f} ms/plot\n", multi.name, per);
    }

    js << "\n}\n";
    std::ofstream(outdir / "bench_matrix_cpp.json") << js.str();
    return 0;
}
