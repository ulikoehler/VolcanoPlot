// examples/microgallery.cpp — one PNG per microfeature for parity checking
//
// Each numbered feature renders exactly one small thing (a grid line, a tick
// mark, a marker shape, ...). Numbers follow the dependency-ordered checklist
// in docs/MICROFEATURES.md — the lowest failing number is the one to fix.
//
// Usage:
//   example_microgallery OUT_DIR                     # all features
//   example_microgallery OUT_DIR --only=009,026      # subset by name prefix
//   example_microgallery OUT_DIR --shard=K/N         # K-th of N shards
//   example_microgallery --list                      # print feature names
//
// Matching matplotlib renderers: scripts/matplotlib_microgallery.py
#include <volcano/backend/Backend.hpp>
#include <volcano/render/Renderer.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Dates.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/Collections.hpp>
#include <volcano/plot/Path.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Normalize.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/FillPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>
#include <volcano/plot/plots/QuiverPlot.hpp>
#include <volcano/plot/plots/QuiverKeyPlot.hpp>
#include <volcano/plot/plots/PcolormeshPlot.hpp>
#include <volcano/plot/plots/StreamPlot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/BarbsPlot.hpp>
#include <volcano/plot/plots/Scatter3D.hpp>
#include <volcano/plot/plots/SurfacePlot.hpp>
#include <volcano/plot/plots/Axes3DPlot.hpp>
#include <volcano/plot/plots/PiePlot.hpp>
#include <volcano/plot/plots/StackPlot.hpp>
#include <volcano/plot/plots/HistPlot.hpp>
#include <volcano/plot/GridSpec.hpp>
#include <volcano/plot/Specialized.hpp>
#include <volcano/plot/Triangulation.hpp>
#include <volcano/plot/plots/TripcolorPlot.hpp>
#include <volcano/plot/plots/TriplotPlot.hpp>
#include <volcano/plot/plots/StemPlot.hpp>
#include <volcano/plot/plots/ReferenceLines.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace volcano;
using namespace volcano::plot;

namespace {

constexpr uint32_t kWidth = 800;
constexpr uint32_t kHeight = 600;

struct MicroCtx {
    backend::BackendDesc desc;
    std::unique_ptr<backend::IBackend> backend;
    std::unique_ptr<render::Renderer> renderer;
    std::string outDir;

    explicit MicroCtx(std::string dir) : outDir(std::move(dir)) {
        desc.width = kWidth;
        desc.height = kHeight;
        desc.samples = vk::SampleCountFlagBits::e4;
        backend = backend::createHeadlessBackend(desc);
        renderer = std::make_unique<render::Renderer>(*backend);
        std::filesystem::create_directories(outDir);
    }

    void render(Figure& fig, std::string_view name) {
        renderer->prepare(fig);
        renderer->renderFrame(fig);
        auto px = backend->readbackRgba8();
        auto enc = encode::createCpuEncoder(encode::ImageFormat::Png);
        std::string path = outDir + "/" + std::string(name) + ".png";
        if (enc->encodeToFile(px, desc.width, desc.height, path))
            std::cout << "  wrote " << path << "\n";
        else
            std::cerr << "  FAILED " << path << "\n";
    }
};

/// matplotlib-default style at 2x DPI (800x600 px == 4x3in at 200 dpi).
FigureStyle mfStyle() {
    auto s = styles::defaultStyle();
    s.dpi = 200.0f;
    return s;
}

/// One axes with matplotlib-default-like style (white bg, no grid, ticks on).
Axes* mfAxes(Figure& fig) {
    auto* ax = fig.addAxes();
    ax->setStyle(mfStyle());
    ax->style().faceColor = Color::white();
    return ax;
}

std::vector<float> linspace(float a, float b, int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = a + (b - a) * float(i) / float(n - 1);
    return v;
}

/// Small deterministic data curve used across line features.
Series2D sineSeries() {
    Series2D s;
    auto x = linspace(0, 10, 60);
    for (float xi : x)
        s.points.push_back({xi, std::sin(xi)});
    return s;
}

Series2D fewPoints() {
    Series2D s;
    s.points = {{1, 1}, {2, 3}, {3, 2}, {4, 4}, {5, 3}};
    return s;
}

std::unique_ptr<LinePlot> lineOf(std::vector<Point2D> pts, Color c) {
    Series2D s;
    s.points = std::move(pts);
    s.color = c;
    return std::make_unique<LinePlot>(std::move(s));
}

// ═══ Tier 0 — canvas & axes chrome ═══════════════════════════════════════

void f001_blank_canvas(Figure& fig) { (void)fig; /* no axes at all */ }

void f002_fig_facecolor(Figure& fig) {
    fig.style().faceColor = Color::fromRgba8(0xdd, 0xdd, 0xdd);
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
}

void f003_axes_facecolor(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    // Emulate ax.set_facecolor: full-extent span painted under everything.
    ax->axvspan(-1e6f, 1e6f, Color::fromRgba8(0xff, 0xff, 0xe0));
}

void f004_spines_box(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
}

void f005_spines_left_bottom(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->setSpineVisible("top", false);
    ax->setSpineVisible("right", false);
}

void f006_spines_none(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->setSpineVisible("all", false);
}

void f007_axes_inset_rect(Figure& fig) {
    auto* ax = fig.addAxesFraction(0.25f, 0.25f, 0.5f, 0.5f);
    ax->setStyle(mfStyle());
    ax->setXlim(0, 1); ax->setYlim(0, 1);
}

void f008_suptitle(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    fig.setTitle("Figure Title");
}

// ═══ Tier 1 — ticks & tick labels ═══════════════════════════════════════

void f009_ticks_major_x(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 1);
    ax->style().yAxis.visible = false;
}

void f010_ticks_major_y(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 10);
    ax->style().xAxis.visible = false;
}

void f011_ticks_none(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->style().xAxis.ticks.positions = std::vector<float>{};
    ax->style().yAxis.ticks.positions = std::vector<float>{};
}

void f012_ticks_minor(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->minorticksOn();
}

void f013_ticks_dir_in(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->tickParams("both", "in");
}

void f014_ticks_dir_inout(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->tickParams("both", "inout");
}

void f015_ticks_long(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->tickParams("both", "out", 10.0f, -1.0f, -1.0f, -1.0f);
}

void f016_ticks_wide(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->tickParams("both", "", -1.0f, -1.0f, 2.5f, -1.0f);
}

void f017_ticks_top_right(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    // mpl tick_params(top=True, right=True): marks on all four sides,
    // labels stay bottom/left.
    ax->setXTickMarksTop(true);
    ax->setYTickMarksRight(true);
}

void f018_ticklabels_default(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
}

void f019_ticklabels_fixed(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 4); ax->setYlim(0, 1);
    ax->style().xAxis.ticks.positions = std::vector<float>{0, 1, 2, 3, 4};
    ax->style().xAxis.ticks.labels =
        std::vector<std::string>{"zero", "one", "two", "three", "four"};
}

void f020_ticklabels_format(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->style().xAxis.ticks.format = "%.2f";
    ax->style().yAxis.ticks.format = "%.2f";
}

void f021_ticklabels_sci(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1e6f); ax->setYlim(0, 1e-4f);
    ax->ticklabelFormat("both", "sci");
}

void f022_ticklabels_rotation(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 4); ax->setYlim(0, 1);
    ax->style().xAxis.ticks.positions = std::vector<float>{0, 1, 2, 3, 4};
    ax->style().xAxis.ticks.labels =
        std::vector<std::string>{"alpha", "beta", "gamma", "delta", "eps"};
    ax->style().xAxis.tickFont.rotation = -0.7854f;  // mpl rotation=45
}

void f023_ticklabels_hidden(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->setXFormatter(std::make_shared<NullFormatter>());
    ax->setYFormatter(std::make_shared<NullFormatter>());
}

void f024_minor_ticklabels(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 1);
    ax->minorticksOn();
    ax->setXMinorFormatter(std::make_shared<FormatStrFormatter>("%.1f"));
    ax->style().yAxis.visible = false;
}

void f025_tick_nbins(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->style().xAxis.ticks.nbins = 4;
    ax->style().yAxis.ticks.nbins = 4;
}

// ═══ Tier 2 — grid ══════════════════════════════════════════════════════

void f026_grid_major(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->grid(true);
}

void f027_grid_x_only(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->grid(true, "major", "x");
}

void f028_grid_y_only(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->grid(true, "major", "y");
}

void f029_grid_minor(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->minorticksOn();
    ax->grid(true, "minor");
}

void f030_grid_both(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->minorticksOn();
    ax->grid(true, "both");
}

void f031_grid_dashed(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->style().xAxis.gridLineStyle = "--";
    ax->style().yAxis.gridLineStyle = "--";
    ax->grid(true);
}

void f032_grid_color(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->style().xAxis.gridColor = Color::fromRgba8(200, 60, 60);
    ax->style().yAxis.gridColor = Color::fromRgba8(200, 60, 60);
    ax->grid(true);
}

void f033_grid_linewidth(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->style().xAxis.gridLineWidth = 2.0f;
    ax->style().yAxis.gridLineWidth = 2.0f;
    ax->grid(true);
}

void f034_grid_axisbelow(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->style().axisBelow = true;
    ax->grid(true);
    Series2D s = sineSeries();
    s.color = Color::black();
    s.lineWidth = 6.0f;
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setYlim(-1.2f, 1.2f);
}

// ═══ Tier 3 — labels & titles ═══════════════════════════════════════════

void f035_xlabel(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->style().xAxis.label = "Time (s)";
}

void f036_ylabel(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->style().yAxis.label = "Amplitude";
}

void f037_label_fontsize(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->style().xAxis.label = "Big X";
    ax->style().xAxis.labelFont.size = 22.0f;
}

void f038_title(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->setTitle("Axes Title");
}

void f039_title_color(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->setTitle("Red Title");
    ax->style().title.color = Color::fromRgba8(200, 40, 40);
}

void f040_title_bold(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->setTitle("Bold Title");
    ax->style().title.weight = "bold";
}

void f041_title_pad(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->setTitle("Padded Title");
    ax->style().title.pad = 30.0f;
}

void f042_label_color(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    ax->style().xAxis.label = "Blue X";
    ax->style().xAxis.labelColor = Color::fromRgba8(30, 60, 200);
}

// ═══ Tier 4 — lines ═════════════════════════════════════════════════════

void addLine(Axes* ax, Series2D s) {
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
}

void f043_line_solid(Figure& fig) { addLine(mfAxes(fig), sineSeries()); }

void f044_line_color(Figure& fig) {
    Series2D s = sineSeries();
    s.color = Color::fromRgba8(214, 39, 40);
    addLine(mfAxes(fig), std::move(s));
}

void f045_line_width(Figure& fig) {
    Series2D s = sineSeries();
    s.lineWidth = 5.0f;
    addLine(mfAxes(fig), std::move(s));
}

void f046_line_dashed(Figure& fig) {
    Series2D s = sineSeries();
    s.lineStyle = LineStyle::Dashed;
    addLine(mfAxes(fig), std::move(s));
}

void f047_line_dotted(Figure& fig) {
    Series2D s = sineSeries();
    s.lineStyle = LineStyle::Dotted;
    addLine(mfAxes(fig), std::move(s));
}

void f048_line_dashdot(Figure& fig) {
    Series2D s = sineSeries();
    s.lineStyle = LineStyle::DashDot;
    addLine(mfAxes(fig), std::move(s));
}

void f049_line_dashes_custom(Figure& fig) {
    Series2D s = sineSeries();
    s.dashes = {8.0f, 3.0f, 2.0f, 3.0f};
    addLine(mfAxes(fig), std::move(s));
}

void f050_line_alpha(Figure& fig) {
    Series2D s = sineSeries();
    s.color = Color::fromRgba8(31, 119, 180, 90);
    s.lineWidth = 4.0f;
    addLine(mfAxes(fig), std::move(s));
}

void f051_line_zorder(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D a = sineSeries();
    a.color = Color::fromRgba8(214, 39, 40);
    a.lineWidth = 8.0f;
    Series2D b = sineSeries();
    for (auto& p : b.points) p.y = -p.y;
    b.color = Color::black();
    b.lineWidth = 2.0f;
    auto p1 = std::make_unique<LinePlot>(std::move(a));
    p1->zorder = 2.0f;   // thick red on top
    auto p2 = std::make_unique<LinePlot>(std::move(b));
    p2->zorder = 1.0f;
    ax->addPlot(std::move(p1));
    ax->addPlot(std::move(p2));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
}

void f052_line_prop_cycle(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D a = sineSeries();
    a.usePropCycle = true;
    Series2D b = sineSeries();
    for (auto& p : b.points) p.y = std::cos(p.x);
    b.usePropCycle = true;
    ax->addPlot(std::make_unique<LinePlot>(std::move(a)));
    ax->addPlot(std::make_unique<LinePlot>(std::move(b)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
}

// ═══ Tier 5 — markers ═══════════════════════════════════════════════════

void addMarkers(Axes* ax, Series2D s) {
    ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    ax->setXlim(0, 6); ax->setYlim(0, 5);
}

void f053_marker_circle(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Circle;
    s.size = 14.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f054_marker_square(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Square;
    s.size = 14.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f055_marker_diamond(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Diamond;
    s.size = 14.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f056_marker_triangle(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Triangle;
    s.size = 14.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f057_marker_star(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Star;
    s.size = 20.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f058_marker_plus(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Plus;
    s.size = 17.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f059_marker_size(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Circle;
    s.size = 28.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f060_marker_hollow(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Circle;
    s.markerFill = MarkerFill::None;
    s.size = 17.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f061_marker_edge_color(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Circle;
    s.markerFill = MarkerFill::None;
    s.color = Color::fromRgba8(214, 39, 40);
    s.size = 17.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f062_marker_with_line(Figure& fig) {
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Circle;
    s.size = 11.0f;
    s.lineWidth = 1.5f;
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 6); ax->setYlim(0, 5);
}

void f063_marker_path(Figure& fig) {
    Series2D s = fewPoints();
    s.markerPath = Path::unitStar(5);
    s.size = 22.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

void f064_marker_tex(Figure& fig) {
    Series2D s = fewPoints();
    s.markerTex = "$\\beta$";
    s.size = 20.0f;
    addMarkers(mfAxes(fig), std::move(s));
}

// ═══ Tier 6 — fills & reference regions ═════════════════════════════════

void f065_fill_polygon(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    s.color = Color::fromRgba8(31, 119, 180);
    s.points = {{1, 1}, {4, 1}, {4, 3}, {2.5f, 4}, {1, 3}};
    ax->addPlot(std::make_unique<FillPlot>(std::move(s)));
    ax->setXlim(0, 5); ax->setYlim(0, 5);
}

void f066_fill_between(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 80);
    std::vector<float> y1, y2;
    for (float xi : x) {
        y1.push_back(std::sin(xi) + 1);
        y2.push_back(std::sin(xi) - 1);
    }
    ax->addPlot(std::make_unique<FillBetweenPlot>(
        x, y1, y2, Color::fromRgba8(31, 119, 180, 100)));
    ax->setXlim(0, 10); ax->setYlim(-2.2f, 2.2f);
}

void f067_fill_alpha(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->grid(true);
    Series2D s;
    s.color = Color::fromRgba8(31, 119, 180, 80);
    s.points = {{0, 0}, {10, 0}, {10, 5}, {0, 5}};
    ax->addPlot(std::make_unique<FillPlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(0, 10);
}

void f068_axhline(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->axhline(5.0f, Color::fromRgba8(214, 39, 40), 2.0f);
}

void f069_axvline(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->axvline(5.0f, Color::fromRgba8(44, 160, 44), 2.0f);
}

void f070_hlines(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->hlines({2, 5, 8}, 1.0f, 9.0f, Color::fromRgba8(31, 119, 180), 1.5f);
}

void f071_vlines(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->vlines({2, 5, 8}, 1.0f, 9.0f, Color::fromRgba8(31, 119, 180), 1.5f);
}

void f072_axhspan(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->axhspan(3.0f, 6.0f, Color::fromRgba8(255, 200, 60, 110));
}

void f073_axvspan(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->axvspan(3.0f, 6.0f, Color::fromRgba8(255, 200, 60, 110));
}

void f074_hatch_rect(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto& p = ax->addPatch(patch::Rectangle(2, 2, 6, 6));
    p.style.hatch = "//";
    p.style.face = Color::fromRgba8(200, 200, 200);
}

// ═══ Tier 7 — scales & limits ═══════════════════════════════════════════

void f075_xlim_ylim(Figure& fig) {
    auto* ax = mfAxes(fig);
    addLine(ax, sineSeries());
    ax->setXlim(2, 8); ax->setYlim(-0.5f, 0.5f);
}

void f076_invert_x(Figure& fig) {
    auto* ax = mfAxes(fig);
    addLine(ax, sineSeries());
    ax->invertXAxis();
}

void f077_invert_y(Figure& fig) {
    auto* ax = mfAxes(fig);
    addLine(ax, sineSeries());
    ax->invertYAxis();
}

void f078_log_y(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    auto x = linspace(0, 5, 60);
    for (float xi : x) s.points.push_back({xi, std::exp(xi)});
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setLogY(true);
    ax->setXlim(0, 5);
}

void f079_log_x(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    auto x = linspace(0.01f, 100, 60);
    for (float xi : x) s.points.push_back({xi, std::log(xi)});
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setLogX(true);
    ax->setYlim(-5, 5);
}

void f080_loglog(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    auto x = linspace(1, 1000, 80);
    for (float xi : x) s.points.push_back({xi, xi * xi});
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->loglog();
}

void f081_symlog(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    auto x = linspace(-100, 100, 120);
    for (float xi : x) s.points.push_back({xi, xi});
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXscale("symlog");
}

void f082_aspect_equal(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    s.points = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 0}};
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(-1, 9); ax->setYlim(-1, 9);
    ax->setAspectEqual();
}

void f083_categorical_x(Figure& fig) {
    auto* ax = mfAxes(fig);
    BarData bd;
    bd.heights = {3, 7, 5, 8};
    bd.labels = {"A", "B", "C", "D"};
    ax->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    ax->setXCategories({"A", "B", "C", "D"});
}

void f084_date_x(Figure& fig) {
    using namespace std::chrono;
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int m = 1; m <= 6; ++m) {
        float d = dates::dateToNum(sys_days{year{2024} / m / 1});
        s.points.push_back({d, float(m * m)});
    }
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->xaxis_date();
}

void f085_secondary_y(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    auto x = linspace(0, 10, 60);
    for (float xi : x) s.points.push_back({xi, std::sin(xi)});
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.1f, 1.1f);
    ax->secondaryYaxis([](float v) { return v * 57.2958f; },
                       [](float v) { return v / 57.2958f; }, "deg");
}

// ═══ Tier 8 — legend ════════════════════════════════════════════════════

void f086_legend_basic(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "signal";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
    ax->legend();
}

void f087_legend_loc(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "signal";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
    ax->legend().location = "lower left";
}

void f088_legend_ncols(Figure& fig) {
    auto* ax = mfAxes(fig);
    for (int i = 0; i < 4; ++i) {
        Series2D s;
        auto x = linspace(0, 10, 40);
        for (float xi : x)
            s.points.push_back({xi, std::sin(xi + float(i) * 0.4f) +
                                      float(i) * 0.4f - 0.6f});
        s.label = "s" + std::to_string(i);
        ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    }
    ax->setXlim(0, 10);
    ax->legend().ncols = 2;
}

void f089_legend_noframe(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "signal";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
    ax->legend().frameOn = false;
}

void f090_legend_title(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "signal";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
    ax->legend().title = "Series";
}

void f091_legend_outside(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "signal";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
    auto& lg = ax->legend();
    lg.location = "center left";
    lg.anchorX = 1.02f; lg.anchorY = 0.5f;
    lg.anchorSpace = CoordSystem::Axes;
}

void f092_legend_shadow(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "signal";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.3f, 1.3f);
    auto& lg = ax->legend();
    lg.shadow = true;
    lg.fancyBox = true;
}

// ═══ Tier 9 — colorbar ══════════════════════════════════════════════════

Grid2D smallGrid() {
    Grid2D g;
    g.width = 8; g.height = 6;
    g.xRange = {0, 8}; g.yRange = {0, 6};
    g.values.resize(48);
    for (uint32_t j = 0; j < 6; ++j)
        for (uint32_t i = 0; i < 8; ++i)
            g.values[j * 8 + i] = float(i + j);
    g.origin = "lower";  // mpl imshow(origin="lower")
    return g;
}

void f093_colorbar_basic(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    ax->style().colorbar.visible = true;
}

void f094_colorbar_extend(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.extend = "both";
}

void f095_colorbar_colormap(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid(),
                                            colormaps::plasma()));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.colormap = "plasma";
}

void f096_colorbar_width(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.width = 40.0f;
}

// ═══ Tier 10 — text & annotations ═══════════════════════════════════════

void f097_text_data(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->text(5, 5, "data point");
}

void f098_text_axes(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->text(0.5f, 0.5f, "axes coords", CoordSystem::Axes);
}

void f099_text_rotation(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* t = ax->text(5, 5, "rotated");
    t->rotation = -0.7854f;  // mpl rotation=45 (CCW) → -45° in Y-down space
}

void f100_text_halign(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->axvline(5.0f, Color::fromRgba8(180, 180, 180), 1.0f);
    auto* l = ax->text(5, 7, "left");
    l->halign = HAlign::Left;
    auto* c = ax->text(5, 5, "center");
    c->halign = HAlign::Center;
    auto* r = ax->text(5, 3, "right");
    r->halign = HAlign::Right;
}

void f101_text_bbox(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* t = ax->text(5, 5, "boxed");
    t->bboxFaceColor = Color::fromRgba8(255, 255, 160);
    t->bboxEdgeColor = Color::black();
}

void f102_mathtext(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* t = ax->text(5, 5, "$x^2 + \\alpha$");
    t->fontSize = 19.2f;
}

void f103_annotate_arrow(Figure& fig) {
    auto* ax = mfAxes(fig);
    addLine(ax, sineSeries());
    ax->annotate(4.71f, -1.0f, 6.5f, -0.5f, "min");
}

void f104_annotate_offset(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s = fewPoints();
    s.marker = MarkerStyle::Circle;
    ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    ax->setXlim(0, 6); ax->setYlim(0, 5);
    auto* t = ax->text(4, 4, "peak", CoordSystem::OffsetPoints);
    t->xyOffsetX = 8; t->xyOffsetY = 8;
}

void f105_unicode_text(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    ax->text(5, 5, "Größe α ≈ π");
}

void f116_arrowstyle_filled(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* a1 = ax->annotate(2, 2, 4, 5, "-|>");
    a1->arrowSpec = parseArrowStyle("-|>");
    auto* a2 = ax->annotate(7, 3, 5, 7, "-[");
    a2->arrowSpec = parseArrowStyle("-[");
    auto* a3 = ax->annotate(2, 8, 5, 6, "|-|");
    a3->arrowSpec = parseArrowStyle("|-|");
}

void f117_arrowstyle_double(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* a1 = ax->annotate(3, 3, 7, 3, "<|-|>");
    a1->arrowSpec = parseArrowStyle("<|-|>");
    auto* a2 = ax->annotate(3, 7, 7, 7, "<->");
    a2->arrowSpec = parseArrowStyle("<->");
}

void f118_arrowstyle_fancy(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* a1 = ax->annotate(3, 3, 6, 5, "fancy");
    a1->arrowSpec = parseArrowStyle("fancy");
    auto* a2 = ax->annotate(7, 8, 4, 6, "wedge");
    a2->arrowSpec = parseArrowStyle("wedge");
}


void f119_clip_path(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    // Line clipped to a circle + filled rect clipped to the same circle.
    auto lc = std::make_unique<LineCollection>(
        std::vector<std::vector<Point2D>>{
            {{0, 5}, {10, 5}}, {{0, 6.2f}, {10, 6.2f}}});
    lc->edgeColors = {Color::blue(), Color::red()};
    lc->lineWidths = {3.0f, 3.0f};
    lc->clipPath = Path::ellipse({5, 5}, 3.0f, 3.0f);
    ax->addPlot(std::move(lc));
    auto p = patch::Rectangle(3, 1, 4, 3);
    p.style.face = Color::fromRgba8(180, 220, 180);
    p.style.edge.a = 0;
    auto pc = std::make_unique<PatchCollection>(std::vector<Patch>{p});
    pc->clipPath = Path::ellipse({5, 5}, 3.0f, 3.0f);
    ax->addPlot(std::move(pc));
}

void f120_boxstyle(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto spec1 = *parseBoxStyle("sawtooth,pad=0.4");
    auto p1 = patch::FancyBboxPatch(0.8f, 5.5f, 3.4f, 3.4f, spec1);
    p1.style.face = Color::fromRgba8(200, 200, 255);
    ax->addPatch(p1);
    auto spec2 = *parseBoxStyle("roundtooth,pad=0.4");
    auto p2 = patch::FancyBboxPatch(5.2f, 5.5f, 3.4f, 3.4f, spec2);
    p2.style.face = Color::fromRgba8(200, 255, 200);
    ax->addPatch(p2);
    auto spec3 = *parseBoxStyle("round,pad=0.3,rounding_size=0.6");
    auto p3 = patch::FancyBboxPatch(3.2f, 1.0f, 3.6f, 3.0f, spec3);
    p3.style.face = Color::fromRgba8(255, 220, 200);
    ax->addPatch(p3);
}

void f121_arrow_bezier(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 10); ax->setYlim(0, 10);
    auto* a1 = ax->annotate(2, 2, 5, 6, "");
    a1->arrowSpec = parseArrowStyle("simple");
    a1->connection = parseConnectionStyle("arc3,rad=0.3");
    auto* a2 = ax->annotate(8, 2, 5, 6, "");
    a2->arrowSpec = parseArrowStyle("fancy");
    a2->connection = parseConnectionStyle("arc3,rad=-0.3");
    auto* a3 = ax->annotate(2, 9, 8, 8, "");
    a3->arrowSpec = parseArrowStyle("wedge,tail_width=0.5");
    a3->connection = parseConnectionStyle("arc3,rad=0.2");
}

// ═══ Tier 11 — multi-axes layout ════════════════════════════════════════

void f106_subplots_2x2(Figure& fig) {
    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 2; ++c) {
            auto* ax = fig.subplot2grid({2, 2}, {r, c});
            ax->setStyle(mfStyle());
            Series2D s;
            auto x = linspace(0, 5, 30);
            for (float xi : x)
                s.points.push_back({xi, std::sin(xi + float(r * 2 + c))});
            ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
        }
}

void f107_subplots_sharex(Figure& fig) {
    auto* a = fig.subplot2grid({2, 1}, {0, 0});
    auto* b = fig.subplot2grid({2, 1}, {1, 0});
    a->setStyle(mfStyle());
    b->setStyle(mfStyle());
    Series2D s1, s2;
    auto x = linspace(0, 10, 60);
    for (float xi : x) {
        s1.points.push_back({xi, std::sin(xi)});
        s2.points.push_back({xi, std::cos(xi * 0.5f)});
    }
    a->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    b->addPlot(std::make_unique<LinePlot>(std::move(s2)));
    a->setXlim(0, 10);
    b->shareX(*a);
}

void f108_twinx(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    auto x = linspace(0, 10, 60);
    for (float xi : x) s.points.push_back({xi, std::sin(xi)});
    s.color = Color::fromRgba8(31, 119, 180);
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.2f, 1.2f);
    auto* ax2 = fig.twinx(*ax);
    Series2D s2;
    for (float xi : x) s2.points.push_back({xi, std::cos(xi) * 50});
    s2.color = Color::fromRgba8(214, 39, 40);
    ax2->addPlot(std::make_unique<LinePlot>(std::move(s2)));
}

void f109_inset_axes(Figure& fig) {
    auto* ax = mfAxes(fig);
    addLine(ax, sineSeries());
    auto* inset = fig.insetAxes(*ax, 0.55f, 0.55f, 0.35f, 0.35f);
    inset->setStyle(mfStyle());
    Series2D s = sineSeries();
    s.color = Color::fromRgba8(214, 39, 40);
    inset->addPlot(std::make_unique<LinePlot>(std::move(s)));
    inset->setXlim(4, 6); inset->setYlim(-1.1f, 1.1f);
}

void f110_mosaic(Figure& fig) {
    auto axes = fig.subplotMosaic({{"A", "B"}, {"C", "C"}});
    for (auto& [name, ax] : axes) {
        ax->setStyle(mfStyle());
        Series2D s;
        auto x = linspace(0, 5, 30);
        for (float xi : x) s.points.push_back({xi, std::sin(xi)});
        ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    }
}

// ═══ Tier 12 — misc composite ═══════════════════════════════════════════

void f111_errorbar_caps(Figure& fig) {
    auto* ax = mfAxes(fig);
    std::vector<float> x = {1, 2, 3, 4, 5}, y = {1, 3, 2, 4, 3},
        yerr(5, 0.4f);
    ErrorbarConfig cfg;
    cfg.yerr = yerr;
    cfg.capSize = 5.0f;
    cfg.drawCaps = true;
    cfg.drawLine = false;
    ax->addPlot(std::make_unique<ErrorbarPlot>(x, y, cfg));
    ax->setXlim(0, 6); ax->setYlim(0, 5);
}

void f112_bar_edges(Figure& fig) {
    auto* ax = mfAxes(fig);
    BarData bd;
    bd.heights = {3, 7, 5, 8};
    bd.width = 0.6f;
    ax->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    ax->setXlim(-0.5f, 3.5f); ax->setYlim(0, 9);
}

void f113_step_post(Figure& fig) {
    auto* ax = mfAxes(fig);
    std::vector<float> x = {0, 1, 2, 3, 4, 5}, y = {1, 3, 2, 4, 3, 5};
    ax->addPlot(std::make_unique<StepPlot>(
        x, y, StepWhere::Post, Color::fromRgba8(31, 119, 180), 2.0f));
    ax->setXlim(-0.5f, 5.5f); ax->setYlim(0, 6);
}

void f114_eventplot_rows(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->eventplot({{1, 2, 4, 7}, {0.5f, 3, 6}, {2, 5, 8, 9}});
    ax->setXlim(0, 10); ax->setYlim(-1, 3);
}

void f115_table_bottom(Figure& fig) {
    auto* ax = mfAxes(fig);
    BarData bd;
    bd.heights = {3, 7, 5};
    ax->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    ax->setXlim(-0.5f, 2.5f); ax->setYlim(0, 9);
    ax->table({{"A", "B", "C"}, {"3", "7", "5"}}, "bottom");
}


// ═══ Tier 15 — extended coverage (122–145) ════════════════════════════════

void f122_colorbar_horizontal(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.orientation = "horizontal";
}

void f123_colorbar_shrink(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.shrink = 0.5f;
}

void f124_marker_tex_beta(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto s = fewPoints();
    s.setMarker("$\\beta$");
    s.size = 14.0f;
    ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
}

void f125_imshow_extent(Figure& fig) {
    auto* ax = mfAxes(fig);
    Grid2D g = smallGrid();
    g.xRange = {-2, 2};
    g.yRange = {-1, 1};
    ax->imshow(std::move(g), colormaps::viridis());
    ax->setXlim(-2.5, 2.5);
    ax->setYlim(-1.5, 1.5);
}

void f126_imshow_aspect_auto(Figure& fig) {
    auto* ax = mfAxes(fig);
    Grid2D g = smallGrid();
    g.width = 4; g.height = 16;    // tall image
    g.origin = "upper";            // mpl imshow(origin="upper")
    g.xRange = {-0.5f, 3.5f};      // mpl default extent (verbatim)
    g.yRange = {15.5f, -0.5f};
    g.values.resize(4 * 16);
    for (uint32_t j = 0; j < 16; ++j)
        for (uint32_t i = 0; i < 4; ++i)
            g.values[j * 4 + i] = float(i + j) / 19.0f;
    ax->imshow(std::move(g), colormaps::viridis(), "nearest", "auto");
}

void f127_inset_indicator(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    auto* in = ax->insetAxes(0.55f, 0.55f, 0.38f, 0.35f);
    in->setStyle(ax->style());
    in->addPlot(std::make_unique<LinePlot>(sineSeries()));
    in->setXlim(4.0f, 5.0f); in->setYlim(-1.0f, 0.0f);
    ax->indicateInsetZoom(*in);
}

void f128_sizebar(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    SizeBar bar;
    bar.size = 2.0f;
    bar.label = "2 units";
    ax->addSizeBar(std::move(bar));
}

void f129_anchored_text(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->addAnchoredText("upper left note", "upper left");
}

void f130_scatter3d_depthshade(Figure& fig) {
    auto* ax = mfAxes(fig);
    std::vector<float> x, y, z;
    for (int i = 0; i < 64; ++i) {
        float a = float(i) * 0.4f;
        x.push_back(std::cos(a) * (1.0f + float(i) / 64.0f));
        y.push_back(std::sin(a) * (1.0f + float(i) / 64.0f));
        z.push_back(float(i) / 16.0f - 2.0f);
    }
    auto cam = Camera3D::viewInit(30.0f, -60.0f);
    cam.aspect = float(kWidth) / float(kHeight);
    Viewport v; v.x = {-2, 2}; v.y = {-2, 2}; v.z = {-2, 2};
    cam.dataMin = {v.x.min, v.y.min, v.z.min};
    cam.dataMax = {v.x.max, v.y.max, v.z.max};
    ax->addPlot(std::make_unique<Axes3DPlot>(cam, v));
    auto p = std::make_unique<Scatter3D>(std::move(x), std::move(y),
                                         std::move(z));
    p->setCamera(cam);
    ax->addPlot(std::move(p));
}

void f131_scatter3d_view_init(Figure& fig) {
    // mpl ax.view_init(elev=0, azim=-90): XZ plane, z vertical.
    auto* ax = mfAxes(fig);
    std::vector<float> x = {0, 1, 0, -1, 0}, y = {0, 0, 0, 0, 0},
                          z = {0, 0, 1, 0, -1};
    auto cam = Camera3D::viewInit(0.0f, -90.0f);
    cam.aspect = float(kWidth) / float(kHeight);
    Viewport v; v.x = {-1.5, 1.5}; v.y = {-1.5, 1.5}; v.z = {-1.5, 1.5};
    cam.dataMin = {v.x.min, v.y.min, v.z.min};
    cam.dataMax = {v.x.max, v.y.max, v.z.max};
    ax->addPlot(std::make_unique<Axes3DPlot>(cam, v));
    Scatter3DConfig cfg;
    cfg.size = 8.0f;
    cfg.color = Color::red();
    cfg.depthshade = false;
    auto p = std::make_unique<Scatter3D>(std::move(x), std::move(y),
                                         std::move(z), cfg);
    p->setCamera(cam);
    ax->addPlot(std::move(p));
}

static Grid2D surfGrid() {
    Grid2D g;
    g.width = g.height = 30;
    g.xRange = {-3, 3}; g.yRange = {-3, 3};
    g.values.resize(30 * 30);
    for (uint32_t j = 0; j < 30; ++j)
        for (uint32_t i = 0; i < 30; ++i) {
            float x = -3 + float(i) / 29 * 6;
            float y = -3 + float(j) / 29 * 6;
            g.values[j * 30 + i] = std::sin(x) * std::cos(y);
        }
    return g;
}
static Camera3D surfCam() {
    auto cam = Camera3D::viewInit(30.0f, -60.0f, 0.0f, {0, 0, 0}, 8.0f);
    cam.aspect = float(kWidth) / float(kHeight);
    cam.dataMin = {-3, -3, -1}; cam.dataMax = {3, 3, 1};
    return cam;
}

void f132_surface_shade(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto cam = surfCam();
    Viewport v; v.x = {-3, 3}; v.y = {-3, 3}; v.z = {-1, 1};
    ax->addPlot(std::make_unique<Axes3DPlot>(cam, v));
    ax->addPlot(std::make_unique<SurfacePlot>(surfGrid(), cam));
}

void f133_surface_noshade(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto cam = surfCam();
    Viewport v; v.x = {-3, 3}; v.y = {-3, 3}; v.z = {-1, 1};
    ax->addPlot(std::make_unique<Axes3DPlot>(cam, v));
    auto p = std::make_unique<SurfacePlot>(surfGrid(), cam);
    p->shade = false;
    ax->addPlot(std::move(p));
}

static void quiverField(std::vector<float>& x, std::vector<float>& y,
                        std::vector<float>& u, std::vector<float>& v) {
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) {
            float px = float(i), py = float(j);
            x.push_back(px); y.push_back(py);
            u.push_back(-(py - 3.5f)); v.push_back(px - 3.5f);
        }
}

void f134_quiver_pivot_mid(Figure& fig) {
    auto* ax = mfAxes(fig);
    std::vector<float> x, y, u, v;
    quiverField(x, y, u, v);
    QuiverConfig cfg;
    cfg.pivot = QuiverConfig::Pivot::Middle;
    ax->addPlot(std::make_unique<QuiverPlot>(std::move(x), std::move(y),
                                             std::move(u), std::move(v), cfg));
    ax->setXlim(-1, 8); ax->setYlim(-1, 8);
}

void f135_quiver_headwidth(Figure& fig) {
    auto* ax = mfAxes(fig);
    std::vector<float> x, y, u, v;
    quiverField(x, y, u, v);
    QuiverConfig cfg;
    cfg.width = 0.005f;
    cfg.headwidth = 5.0f; cfg.headlength = 7.0f; cfg.headaxislength = 6.0f;
    ax->addPlot(std::make_unique<QuiverPlot>(std::move(x), std::move(y),
                                             std::move(u), std::move(v), cfg));
    ax->setXlim(-1, 8); ax->setYlim(-1, 8);
}

static void streamGrid(Grid2D& gu, Grid2D& gv, bool nanHole = false) {
    gu.width = gu.height = gv.width = gv.height = 20;
    gu.xRange = gv.xRange = {0, 3};
    gu.yRange = gv.yRange = {0, 3};
    gu.values.resize(400); gv.values.resize(400);
    for (int j = 0; j < 20; ++j)
        for (int i = 0; i < 20; ++i) {
            float x = float(i) / 19 * 3, y = float(j) / 19 * 3;
            gu.values[j * 20 + i] = -(y - 1.5f);
            gv.values[j * 20 + i] = x - 1.5f;
            if (nanHole && i >= 9 && i <= 11) {
                gu.values[j * 20 + i] = std::nanf("");
                gv.values[j * 20 + i] = std::nanf("");
            }
        }
}

void f136_streamplot_arrowsize(Figure& fig) {
    auto* ax = mfAxes(fig);
    Grid2D gu, gv;
    streamGrid(gu, gv);
    StreamConfig cfg;
    cfg.arrowsize = 2.0f;
    cfg.color = Color::fromRgba8(31, 119, 180);
    ax->addPlot(std::make_unique<StreamPlot>(std::move(gu), std::move(gv), cfg));
    ax->setXlim(0, 3); ax->setYlim(0, 3);
}

void f137_streamplot_nan_hole(Figure& fig) {
    auto* ax = mfAxes(fig);
    Grid2D gu, gv;
    streamGrid(gu, gv, /*nanHole=*/true);
    StreamConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180);
    ax->addPlot(std::make_unique<StreamPlot>(std::move(gu), std::move(gv), cfg));
    ax->setXlim(0, 3); ax->setYlim(0, 3);
}

void f138_clabel_gap(Figure& fig) {
    auto* ax = mfAxes(fig);
    Grid2D g;
    g.width = g.height = 30;
    g.xRange = {-3, 3}; g.yRange = {-3, 3};
    g.values.resize(30 * 30);
    for (uint32_t j = 0; j < 30; ++j)
        for (uint32_t i = 0; i < 30; ++i) {
            float x = -3 + float(i) / 29 * 6;
            float y = -3 + float(j) / 29 * 6;
            g.values[j * 30 + i] = x * x + y * y;
        }
    ContourConfig cfg;
    cfg.levels = {2.0f, 5.0f, 8.0f};
    cfg.cmap = &colormaps::viridis();
    cfg.clabel = true;
    ax->addPlot(std::make_unique<ContourPlot>(std::move(g), cfg));
}

void f139_log_clip(Figure& fig) {
    auto* ax = mfAxes(fig);
    Series2D s;
    for (float x : linspace(-2, 2, 40))
        s.points.push_back({x, x});   // half the points are ≤ 0
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setYscale("log");
    ax->setYlim(0.01, 3);
}

void f140_colorblind_cycle(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto st = mfStyle();
    st.colorblindSafe();
    ax->setStyle(st);
    ax->style().faceColor = Color::white();
    for (int i = 0; i < 4; ++i) {
        auto s = sineSeries();
        for (auto& p : s.points) p.y += float(i) * 0.5f;
        ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    }
}

void f141_legend_handlelength(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto s1 = sineSeries(); s1.label = "sin";
    auto s2 = sineSeries();
    for (auto& p : s2.points) p.y = -p.y;
    s2.label = "-sin";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    ax->addPlot(std::make_unique<LinePlot>(std::move(s2)));
    ax->style().legend.visible = true;
    ax->style().legend.handleLength = 4.0f;
}

void f142_legend_labelcolor(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto s1 = sineSeries(); s1.label = "sin";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    ax->style().legend.visible = true;
    ax->style().legend.labelColor = Color::red();
}

void f143_pie_explode(Figure& fig) {
    auto* ax = mfAxes(fig);
    PieData d;
    d.values = {30, 25, 20, 15, 10};
    d.labels = {"A", "B", "C", "D", "E"};
    d.explode = {0.08f};
    ax->addPlot(std::make_unique<PiePlot>(std::move(d)));
    ax->setAspect(AspectMode::Equal);
    ax->style().xAxis.visible = false;
    ax->style().yAxis.visible = false;
}

void f144_secondary_x(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10);
    // mpl secondary_xaxis('top', functions=(fwd, inv)) — label on top.
    ax->secondaryXaxis([](float x) { return x * 2.0f; },
                      [](float x) { return x * 0.5f; }, "double x");
}

void f145_mathtext_frac_sum(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    auto* t = ax->text(5.0f, 0.5f,
                       "$\\frac{x}{y} + \\sum_{i=0}^{n} i$");
    (void)t;
}

// ═══ Tier 16 — parity batch 10 (146-155) ═══════════════════════════════

void f146_locator_params(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    ax->locatorParams("x", 4);   // mpl ax.locator_params('x', nbins=4)
}

void f147_set_xbound(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    ax->setXbound(2.0f, 8.0f);   // mpl ax.set_xbound(2, 8)
}

void f148_markevery_int(Figure& fig) {
    Series2D s = sineSeries();
    s.marker = MarkerStyle::Circle;
    s.size = 10.0f;
    s.setMarkevery(5);           // mpl markevery=5
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
}

void f149_pie_startangle(Figure& fig) {
    auto* ax = mfAxes(fig);
    PieData d;
    d.values = {30, 25, 20, 15, 10};
    d.labels = {"A", "B", "C", "D", "E"};
    d.startAngle = 90.0f;
    d.counterclock = false;
    ax->pie(std::move(d));
}

void f150_pie_autopct(Figure& fig) {
    auto* ax = mfAxes(fig);
    PieData d;
    d.values = {30, 25, 20, 15, 10};
    d.labels = {"A", "B", "C", "D", "E"};
    d.autopct = "%1.1f%%";
    ax->pie(std::move(d));
}

void f151_fill_between_where(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 60);
    std::vector<float> y1, y2(60, 0.0f);
    std::vector<bool> where;
    for (float xi : x) {
        y1.push_back(std::sin(xi));
        where.push_back(xi > 2.0f && xi < 8.0f);
    }
    auto p = std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y1), std::move(y2),
        Color::fromRgba8(31, 119, 180, 128));
    p->setWhere(std::move(where), true);
    ax->addPlot(std::move(p));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
}

void f152_stackplot_wiggle(Figure& fig) {
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 40);
    std::vector<float> a, b, c;
    for (float xi : x) {
        a.push_back(std::sin(xi) + 1.5f);
        b.push_back(std::cos(xi * 0.7f) + 1.0f);
        c.push_back(std::sin(xi * 0.4f) * 0.5f + 1.0f);
    }
    auto p = std::make_unique<StackPlot>(
        std::move(x), std::vector<std::vector<float>>{a, b, c});
    p->setBaseline(StackBaseline::Wiggle);   // mpl baseline='wiggle'
    ax->addPlot(std::move(p));
}

void f153_label_outer(Figure& fig) {
    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 2; ++c) {
            auto* ax = fig.subplot2grid({2, 2}, {r, c});
            ax->setStyle(mfStyle());
            ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
            ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
            ax->labelOuter();
        }
}

void f154_axis_off(Figure& fig) {
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    ax->setAxisOff();            // mpl ax.axis('off')
}

void f155_imshow_origin_lower(Figure& fig) {
    auto* ax = mfAxes(fig);
    Grid2D g;
    g.width = 4; g.height = 4;
    g.values.resize(16);
    for (uint32_t j = 0; j < 4; ++j)
        for (uint32_t i = 0; i < 4; ++i)
            g.values[j * 4 + i] = (j == 0) ? 1.0f : 0.0f;  // hot top row
    g.xRange = {0, 4}; g.yRange = {0, 4};
    ax->imshow(std::move(g), colormaps::viridis(), "nearest",
               "equal", "lower");            // origin='lower' → hot at bottom
    ax->setXlim(-0.5f, 4.5f); ax->setYlim(-0.5f, 4.5f);
}

// ═══ Tier 17 — parity batch 11 (156-165) ═══════════════════════════════

void f156_marker_colors(Figure& fig) {
    // mpl markerfacecolor/markeredgecolor/markeredgewidth
    Series2D s = sineSeries();
    s.marker = MarkerStyle::Circle;
    s.size = 12.0f;
    s.setMarkevery(6);
    s.markerFaceColor = Color::red();
    s.markerEdgeColor = Color::black();
    s.markerEdgeWidth = 2.0f;
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
}

void f157_spine_center(Figure& fig) {
    // mpl spines['bottom'].set_position(('data', 0)) — classic
    // centered-axes figure.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    auto& b = ax->spine("bottom");
    b.posMode = Axes::SpineSpec::PosMode::Data;
    b.posAmount = 0.0f;
    b.positionSet = true;
    auto& l = ax->spine("left");
    l.posMode = Axes::SpineSpec::PosMode::Data;
    l.posAmount = 0.0f;
    l.positionSet = true;
    ax->setSpineVisible("top", false);
    ax->setSpineVisible("right", false);
}

void f158_spine_outward_bounds(Figure& fig) {
    // mpl set_position(('outward', 10)) + set_bounds(2, 8) +
    // set_color — bottom spine pushed out, clipped to x∈[2,8], red.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    auto& b = ax->spine("bottom");
    b.posMode = Axes::SpineSpec::PosMode::Outward;
    b.posAmount = 10.0f;
    b.positionSet = true;
    b.bounds = std::pair{2.0f, 8.0f};
    b.color = Color::red();
    ax->setSpineVisible("top", false);
    ax->setSpineVisible("right", false);
}

void f159_quiverkey(Figure& fig) {
    // mpl ax.quiverkey(Q, 0.9, 0.9, 1, '1 m/s')
    auto* ax = mfAxes(fig);
    std::vector<float> x, y, u, v;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            x.push_back(float(i)); y.push_back(float(j));
            u.push_back(1.0f); v.push_back(0.5f);
        }
    auto* q = static_cast<QuiverPlot*>(ax->addPlot(
        std::make_unique<QuiverPlot>(x, y, u, v)));
    ax->setXlim(-0.5f, 3.5f); ax->setYlim(-0.5f, 3.5f);
    ax->quiverKey(*q, 0.9f, 0.9f, 1.0f, "1 m/s", "E");
}

void f160_xkcd_sketch(Figure& fig) {
    // mpl plt.xkcd() — path.sketch wobble on a line.
    auto* ax = mfAxes(fig);
    ax->setSketchParams(1.0f, 100.0f, 2.0f);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.4, 1.4);
}

void f161_clip_off(Figure& fig) {
    // mpl clip_on=False — the artist draws across the whole figure.
    auto* ax = mfAxes(fig);
    auto* p = static_cast<LinePlot*>(ax->addPlot(lineOf(
        {{0.0f, 0.0f}, {10.0f, 40.0f}}, Color::red())));
    p->clipOn = false;
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
}

void f162_sticky_edges_bar(Figure& fig) {
    // mpl bar: y=0 is a sticky edge — autoscale keeps the baseline
    // pinned at 0 instead of padding below it.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<BarPlot>(
        BarData{.heights = {1.0f, 3.0f, 2.0f, 4.0f, 3.0f}}));
    ax->autoscale();
}

void f163_stairs_fill(Figure& fig) {
    // mpl ax.stairs(values, edges, fill=True)
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<StairsPlot>(
        std::vector<float>{1.0f, 3.0f, 2.0f, 4.0f, 2.0f},
        std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f},
        Color::blue(), 1.5f, true));
    ax->setXlim(0, 5); ax->setYlim(0, 4.5f);
}

void f164_pcolor(Figure& fig) {
    // mpl ax.pcolor(C) — flat-shaded quad mesh.
    auto* ax = mfAxes(fig);
    std::vector<float> x{0, 1, 2, 3, 4}, y{0, 1, 2, 3};
    std::vector<float> vals;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 4; ++i)
            vals.push_back(float(i * j) / 12.0f);
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.shading = PcmShading::Flat;
    ax->addPlot(std::make_unique<PcolormeshPlot>(
        x, y, vals, 4, 3, std::move(cfg)));
    ax->setXlim(0, 4); ax->setYlim(0, 3);
}

void f165_inset_zoom(Figure& fig) {
    // mpl ax.inset_axes + ax.indicate_inset_zoom
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 200);
    std::vector<Point2D> pts;
    for (float xi : x) pts.push_back({xi, std::sin(xi * 3.0f)});
    Series2D s; s.points = std::move(pts);
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    auto* inner = ax->insetAxes(0.55f, 0.55f, 0.4f, 0.4f);
    inner->addPlot(std::make_unique<LinePlot>(sineSeries()));
    inner->setXlim(2.0f, 4.0f); inner->setYlim(-0.6f, 0.6f);
    ax->indicateInsetZoom(*inner);
}

// ═══ Tier 18 — parity batch 12 (166-170) ═══════════════════════════════

void f166_patheffects_stroke(Figure& fig) {
    // mpl patheffects.withStroke(linewidth=4, foreground='black') —
    // dark outline under the line.
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.color = Color::blue();
    s.lineWidth = 2.0f;
    auto lp = std::make_unique<LinePlot>(std::move(s));
    PathEffect stroke;
    stroke.kind = PathEffect::Kind::Stroke;
    stroke.foreground = Color::black();
    stroke.lineWidth = 4.0f;
    stroke.thenNormal = true;   // mpl withStroke
    lp->pathEffects = {stroke};
    ax->addPlot(std::move(lp));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
}

void f167_patheffects_shadow(Figure& fig) {
    // mpl patheffects.SimplePatchShadow() — offset filled shadow.
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int i = 0; i < 8; ++i)
        s.points.push_back({float(i) * 1.2f + 0.5f,
                            std::sin(float(i)) * 0.6f + 0.5f});
    s.color = Color::red();
    s.size = 14.0f;
    auto sp = std::make_unique<ScatterPlot>(std::move(s));
    PathEffect sh;
    sh.kind = PathEffect::Kind::PatchShadow;
    sh.thenNormal = true;
    sp->pathEffects = {sh};
    ax->addPlot(std::move(sp));
    ax->setXlim(0, 10); ax->setYlim(-0.5, 1.5);
}

void f168_ticks_both(Figure& fig) {
    // mpl ax.xaxis/yaxis.set_ticks_position('both') — tick marks on
    // all four sides, labels stay near-side.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    ax->setXTicksPosition("both");
    ax->setYTicksPosition("both");
}

void f169_transform_transaxes(Figure& fig) {
    // mpl ax.plot(..., transform=ax.transAxes) — the diagonal runs in
    // axes coordinates, ignoring the data viewport.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    Series2D s;
    s.points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    s.color = Color::red();
    s.lineWidth = 2.0f;
    auto lp = std::make_unique<LinePlot>(std::move(s));
    lp->transform = ax->transAxes();
    ax->addPlot(std::move(lp));
}

void f170_colorbar_ticks(Figure& fig) {
    // mpl fig.colorbar(im, ticks=...) + set_ticklabels + minorticks_on.
    auto* ax = mfAxes(fig);
    Grid2D grid;
    grid.width = 4; grid.height = 4;
    grid.xRange = {-0.5f, 3.5f};   // mpl imshow default extent
    grid.yRange = {3.5f, -0.5f};   // (verbatim; origin='upper' inverts y)
    for (uint32_t j = 0; j < 4; ++j)
        for (uint32_t i = 0; i < 4; ++i)
            grid.values.push_back(float(i + j * 4) / 15.0f);
    auto& hm = ax->imshow(std::move(grid));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.mappable = &hm;
    cb.ticks = {0.0f, 0.5f, 1.0f};
    cb.tickLabels = {"low", "mid", "high"};
    cb.minorTicksOn = true;
}

void f171_scatter_c(Figure& fig) {
    // mpl ax.scatter(x, y, c=values, cmap='viridis') + colorbar —
    // per-point scalar colormapping.
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int i = 0; i < 8; ++i)
        s.points.push_back({float(i) * 1.2f + 0.5f,
                            std::sin(float(i)) * 0.6f + 0.5f});
    s.size = 14.0f;
    auto sp = std::make_unique<ScatterPlot>(std::move(s));
    auto* spp = sp.get();
    for (int i = 0; i < 8; ++i)
        spp->array_.push_back(float(i) / 7.0f);
    ax->addPlot(std::move(sp));
    ax->setXlim(0, 10); ax->setYlim(-0.5, 1.5);
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.mappable = spp;
}

void f172_scatter_s(Figure& fig) {
    // mpl ax.scatter(x, y, s=sizes) — per-point marker areas.
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int i = 0; i < 6; ++i)
        s.points.push_back({float(i) * 1.6f + 0.8f, 0.5f});
    s.color = Color::blue();
    s.size = 8.0f;
    auto sp = std::make_unique<ScatterPlot>(std::move(s));
    // mpl s is pt² area → diameter px = sqrt(s)·dpi/72 (dpi=200).
    for (int i = 0; i < 6; ++i)
        sp->sizes_.push_back(std::sqrt(20.0f + float(i) * 60.0f) *
                             200.0f / 72.0f);
    ax->addPlot(std::move(sp));
    ax->setXlim(0, 10); ax->setYlim(0, 1);
}

void f173_text_boxstyle(Figure& fig) {
    // mpl ax.text(..., bbox=dict(boxstyle='round', fc='wheat', ec='k')).
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    TextAnnotation t;
    t.text = "peak";
    t.x = 5.0f; t.y = 0.8f;
    t.halign = HAlign::Center;
    t.valign = VAlign::Center;
    t.hasBbox = true;
    t.bboxFaceColor = Color::fromRgba8(0xf5, 0xde, 0xb3);  // wheat
    t.bboxEdgeColor = Color::black();
    t.boxStyle = BoxStyleSpec{BoxStyleSpec::Kind::Round, 0.3f};
    ax->texts().push_back(t);
}

void f174_legend_anchor(Figure& fig) {
    // mpl ax.legend(bbox_to_anchor=(1, 1), loc='upper left',
    //               handles=..., labels=..., edgecolor='red').
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.label = "sine";
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    auto& lg = ax->legend();
    lg.location = "upper left";
    lg.anchorX = 1.0f; lg.anchorY = 1.0f;
    lg.explicitLabels = {"wave"};
    lg.edgeColor = Color::red();
    lg.faceColor = Color{1, 1, 1, 1};
}

void f175_imshow_norm(Figure& fig) {
    // mpl ax.imshow(C, norm=LogNorm(), cmap='viridis') + colorbar.
    auto* ax = mfAxes(fig);
    Grid2D grid;
    grid.width = 4; grid.height = 4;
    grid.xRange = {-0.5f, 3.5f};
    grid.yRange = {3.5f, -0.5f};   // mpl default extent, origin='upper'
    for (uint32_t j = 0; j < 4; ++j)
        for (uint32_t i = 0; i < 4; ++i)
            grid.values.push_back(std::pow(10.0f, float(i + j * 4) / 15.0f * 2.0f));
    grid.valueRange = {1.0f, 100.0f};
    auto& hm = ax->imshow(std::move(grid));
    hm.setNorm(std::make_shared<LogNorm>());
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.mappable = &hm;
}

void f176_font_family(Figure& fig) {
    // mpl ax.text(..., family='serif'/'sans-serif'/'monospace').
    auto* ax = mfAxes(fig);
    ax->setXlim(0, 1); ax->setYlim(0, 1);
    const char* fams[3] = {"serif", "sans-serif", "monospace"};
    const float ys[3] = {0.75f, 0.5f, 0.25f};
    for (int i = 0; i < 3; ++i) {
        auto* t = ax->text(0.05f, ys[i], fams[i], CoordSystem::Axes);
        t->font.family = fams[i];
        t->fontSize = 14.0f;
        t->font.size = 14.0f;
    }
}

void f177_tick_labelsize(Figure& fig) {
    // mpl ax.tick_params(axis='x', labelsize=14) / y labelsize=7.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    ax->style().xAxis.tickFont.size = 14.0f;
    ax->style().yAxis.tickFont.size = 7.0f;
}

void f178_fig_text(Figure& fig) {
    // mpl fig.text(0.5, 0.02, ...) + fig.suptitle/supxlabel with kwargs.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    fig.suptitle("suptitle");
    fig.style().title.font.size = 14.0f;
    fig.style().title.font.weight = "bold";
    fig.supxlabel("supxlabel");
    fig.supXlabelFont.size = 9.0f;
    TextAnnotation t;
    t.coords = CoordSystem::Figure;
    t.x = 0.5f; t.y = 0.5f;
    t.text = "fig text";
    t.color = Color::red();
    t.fontSize = 11.0f;
    t.font.size = 11.0f;
    t.halign = HAlign::Center;
    fig.texts().push_back(t);
}

void f179_imshow_rgb(Figure& fig) {
    // mpl ax.imshow(rgb) with an (H,W,3) float array — direct RGBA path.
    auto* ax = mfAxes(fig);
    Grid2D grid;
    grid.width = 8; grid.height = 8;
    grid.xRange = {-0.5f, 7.5f};
    grid.yRange = {7.5f, -0.5f};  // mpl default extent, origin='upper'
    for (uint32_t j = 0; j < 8; ++j)
        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t r = uint32_t(31 * i), g = uint32_t(31 * j);
            uint32_t b = 128, a = 255;
            grid.rgba.push_back(r | (g << 8) | (b << 16) | (a << 24));
        }
    ax->imshow(std::move(grid));
}

void f180_annotate_fontsize(Figure& fig) {
    // mpl ax.annotate('big', xy=..., xytext=..., fontsize=20,
    //                 fontweight='bold', arrowprops={'arrowstyle':'->'}).
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<LinePlot>(sineSeries()));
    ax->setXlim(0, 10); ax->setYlim(-1.2, 1.2);
    auto* a = ax->annotate(5.0f, -0.96f, 6.5f, -0.6f, "big",
                           CoordSystem::Data);
    a->arrowStyle = ArrowStyle::Arrow;
    a->fontSize = 20.0f;
    a->font.size = 20.0f;
    a->font.weight = "bold";
}


void f181_gridspec_ratios(Figure& fig) {
    // mpl GridSpec(2, 3, width_ratios=[2,1,1], height_ratios=[1,2])
    // with an axes spanning the top row.
    auto gs = std::make_shared<GridSpec>(2, 3);
    fig.adoptGrid(gs);
    gs->widthRatios = {2, 1, 1};
    gs->heightRatios = {1, 2};
    auto* top = fig.addAxes(gs->at(0, 0, 1, 3));
    top->setStyle(mfStyle());
    top->addPlot(std::make_unique<LinePlot>(sineSeries()));
    for (uint32_t c = 0; c < 3; ++c) {
        auto* ax = fig.addAxes(gs->at(1, c));
        ax->setStyle(mfStyle());
        Series2D s;
        s.points = {{0, float(c)}, {1, float(c + 1)}};
        ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    }
}

void f182_gridspec_nested(Figure& fig) {
    // mpl: gs = GridSpec(1, 2); ax0 at gs[0,0]; nested GridSpec(2,1)
    // inside gs[0,1] with two stacked axes.
    auto gs = std::make_shared<GridSpec>(1, 2);
    fig.adoptGrid(gs);
    auto* left = fig.addAxes(gs->at(0, 0));
    left->setStyle(mfStyle());
    left->addPlot(std::make_unique<LinePlot>(sineSeries()));
    auto nested = gs->at(0, 1).nested(2, 1);
    fig.adoptGrid(nested);
    for (uint32_t r = 0; r < 2; ++r) {
        auto* ax = fig.addAxes(nested->at(r, 0));
        ax->setStyle(mfStyle());
        Series2D s;
        s.points = {{0, float(r)}, {1, float(1 - r)}};
        ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    }
}

void f183_artist_props(Figure& fig) {
    // mpl: scatter.set_zorder(5); line.set_alpha(0.4);
    // hidden.set_visible(False).
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 60);
    Series2D s1 = sineSeries();
    s1.color = Color::blue();
    s1.alpha = 0.4f;
    s1.lineWidth = 3.0f;
    ax->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    // Red scatter drawn above (zorder 5) — green hidden line skipped.
    Series2D s2;
    for (int i = 0; i < 12; ++i)
        s2.points.push_back({float(i), 0.5f});
    s2.color = Color::red();
    s2.marker = MarkerStyle::Circle;
    // mpl s=64 (pt²) → 8pt diameter → px = 8 * dpi/72.
    s2.size = 8.0f * 200.0f / 72.0f;
    auto sc = std::make_unique<ScatterPlot>(std::move(s2));
    sc->zorder = 5.0f;
    ax->addPlot(std::move(sc));
    Series2D s3;
    s3.points = {{0, -0.9f}, {10, 0.9f}};
    s3.color = Color::fromRgba8(0, 180, 0);
    s3.lineWidth = 4.0f;
    auto hidden = std::make_unique<LinePlot>(std::move(s3));
    hidden->visible = false;
    ax->addPlot(std::move(hidden));
}

void f184_collection_props(Figure& fig) {
    // mpl scatter(x, y, s=sizes, c=colors) then
    // coll.set_edgecolor('k'); coll.set_linewidth(1.5).
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 20);
    Series2D s;
    s.marker = MarkerStyle::Circle;
    s.markerEdgeColor = Color::black();
    s.markerEdgeWidth = 1.5f * 200.0f / 72.0f;  // mpl 1.5pt
    for (int i = 0; i < 20; ++i)
        s.points.push_back({x[i], 0.5f + 0.3f * std::sin(x[i])});
    auto sc = std::make_unique<ScatterPlot>(std::move(s));
    std::vector<float> sizes;
    for (int i = 0; i < 20; ++i) {
        sc->colors_.push_back(Color::fromRgba8(
            uint8_t(30 + 10 * i), uint8_t(80), uint8_t(220 - 10 * i)));
        // mpl s=(5+i*0.5)² pt² → diameter (5+i*0.5)pt → px at 200dpi.
        sizes.push_back((5.0f + float(i) * 0.5f) * 200.0f / 72.0f);
    }
    sc->setSizes(std::move(sizes));
    ax->addPlot(std::move(sc));
    ax->setXlim(-0.5f, 10.5f); ax->setYlim(0.0f, 1.0f);
}

void f185_return_handles(Figure& fig) {
    // mpl: hist returns (n, bins, patches) — annotate at a returned
    // bin edge; errorbar returns an ErrorbarContainer.
    auto* ax = mfAxes(fig);
    std::vector<float> data;
    for (int i = 0; i < 200; ++i)
        data.push_back(float(std::sin(float(i) * 0.31) * 2.0 +
                             std::cos(float(i) * 0.13)));
    HistConfig hcfg; hcfg.bins = HistBinMethod::Fixed;
    hcfg.binCount = 8;
    ax->addPlot(std::make_unique<HistPlot>(std::move(data), hcfg));
    ax->axvline(1.0f, Color::red(), 2.0f);
    ax->setXlim(-3.5f, 3.5f);
}

void f186_tri_explicit(Figure& fig) {
    // mpl: tri = mtri.Triangulation(x, y, tris);
    // ax.tripcolor(tri, facecolors) + ax.triplot(tri, 'k-', lw=0.7).
    auto* ax = mfAxes(fig);
    std::vector<float> x = {0, 1, 2, 0, 1, 2, 0.5f, 1.5f};
    std::vector<float> y = {0, 0, 0, 1, 1, 1, 0.5f, 0.5f};
    std::vector<Triangle> tris = {{0, 1, 6}, {1, 6, 7}, {1, 2, 7},
                                  {0, 6, 3}, {6, 3, 4}, {6, 4, 7},
                                  {7, 4, 5}, {2, 7, 5}};
    std::vector<float> face = {0.1f, 0.3f, 0.5f, 0.7f,
                               0.2f, 0.9f, 0.4f, 0.6f};
    ax->addPlot(
        std::make_unique<TripcolorPlot>(x, y, tris, face));
    TriplotConfig tcfg;
    tcfg.color = Color::black();
    tcfg.lineWidth = 0.7f * 200.0f / 72.0f;
    ax->addPlot(std::make_unique<TriplotPlot>(x, y, tris, tcfg));
    ax->setXlim(-0.1f, 2.1f); ax->setYlim(-0.1f, 1.1f);
}

void f187_named_containers(Figure& fig) {
    // mpl: ax.errorbar → ErrorbarContainer; ax.stem → StemContainer;
    // ax.eventplot → [EventCollection]. 3 stacked axes, one each.
    auto gs = std::make_shared<GridSpec>(3, 1);
    fig.adoptGrid(gs);
    auto* ax = fig.addAxes(gs->at(0, 0));
    ax->setStyle(mfStyle());
    auto x = linspace(0, 10, 12);
    std::vector<float> y, ye;
    for (float xi : x) {
        y.push_back(std::sin(xi));
        ye.push_back(0.15f + 0.05f * std::abs(std::sin(xi * 3)));
    }
    ErrorbarConfig ec;
    ec.yerr = ye;
    const float pt2px = 200.0f / 72.0f;
    ec.capSize = 4.0f * pt2px;           // mpl capsize=4 (points)
    ec.drawCaps = true;
    ec.markerSize = 4.0f * pt2px;        // mpl ms=4
    ec.lineWidth = 1.5f * pt2px;         // mpl lines.linewidth=1.5pt
    ec.errorbarWidth = 1.5f * pt2px;     // mpl elinewidth→lw
    ax->addPlot(std::make_unique<ErrorbarPlot>(x, y, ec));
    ax->setYlim(-1.5f, 1.5f);
    auto* ax2 = fig.addAxes(gs->at(1, 0));
    ax2->setStyle(mfStyle());
    std::vector<float> sy;
    for (int i = 0; i < 12; ++i) sy.push_back(std::cos(float(i) * 0.7f));
    StemConfig scfg;
    scfg.markerSize = 6.0f * pt2px;   // mpl lines.markersize=6pt
    scfg.lineWidth = 1.5f * pt2px;    // mpl stemlines lw=1.5pt
    scfg.baselineWidth = 1.5f * pt2px;
    ax2->addPlot(std::make_unique<StemPlot>(linspace(0, 10, 12), sy,
                                          scfg));
    ax2->setYlim(-1.3f, 1.3f);
    auto* ax3 = fig.addAxes(gs->at(2, 0));
    ax3->setStyle(mfStyle());
    ax3->addPlot(std::make_unique<EventPlot>(
        std::vector<std::vector<float>>{{1, 3, 5, 7},
                                        {0.5f, 2.5f, 6.5f},
                                        {2, 4, 8}}));
    ax3->setXlim(0, 9); ax3->setYlim(-0.6f, 2.6f);
}

void f188_date_locators(Figure& fig) {
    // mpl: ax.plot(days, y); ax.xaxis.set_major_locator(
    //     mdates.MonthLocator(interval=2));
    // ax.xaxis.set_major_formatter(mdates.DateFormatter('%b %d'))
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int d = 0; d <= 180; d += 3)
        s.points.push_back(
            {18262.0f + float(d), std::sin(float(d) * 0.06f)});
    s.color = Color::blue();
    s.lineWidth = 1.5f * 200.0f / 72.0f;  // mpl lw=1.5pt
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXLocator(
        std::make_shared<dates::MonthLocator>(2));
    ax->setXFormatter(
        std::make_shared<dates::DateFormatter>("%b %d"));
    ax->setXlim(18262.0f, 18442.0f);
    ax->setYlim(-1.2f, 1.2f);
}

void f189_anchored_artists(Figure& fig) {
    // mpl offsetbox: AnchoredText("anchored", 'upper left') +
    // AnchoredSizeBar(transData, 2, '2 units', 'lower right').
    auto* ax = mfAxes(fig);
    Series2D s = sineSeries();
    s.lineWidth = 1.5f * 200.0f / 72.0f;  // mpl lw=1.5pt
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto& at = ax->addAnchoredText("anchored", "upper left");
    at.pad = 0.4f; at.borderpad = 0.5f; at.frameon = true;
    SizeBar sb;
    sb.size = 2.0f; sb.label = "2 units"; sb.loc = "lower right";
    sb.pad = 0.2f; sb.borderpad = 0.5f; sb.sep = 4.0f;
    sb.frameon = true;
    ax->addSizeBar(sb);
}

void f190_concise_dates(Figure& fig) {
    // mpl: MonthLocator + DayLocator(7) minor + ConciseDateFormatter.
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int d = 0; d <= 60; ++d)
        s.points.push_back(
            {18262.0f + float(d), std::cos(float(d) * 0.1f)});
    s.color = Color::fromRgba8(214, 39, 40);
    s.lineWidth = 1.5f * 200.0f / 72.0f;  // mpl lw=1.5pt
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXLocator(
        std::make_shared<dates::MonthLocator>(1));
    ax->setXMinorLocator(
        std::make_shared<dates::DayLocator>(7));
    ax->setXFormatter(
        std::make_shared<dates::ConciseDateFormatter>());
    ax->setXlim(18262.0f, 18322.0f);
    ax->setYlim(-1.2f, 1.2f);
}

// ═══ Tier 23 — scale / units / animation / sankey / colorbar ═══════════

void f191_log_base2(Figure& fig) {
    // mpl: ax.set_xscale("log", base=2) → ticks at 1,2,4,8,... labels 2^k.
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int i = 0; i <= 8; ++i) {
        float x = float(1 << i);
        s.points.push_back({x, std::sqrt(x)});
    }
    s.color = Color::fromRgba8(31, 119, 180);
    s.lineWidth = 1.5f * 200.0f / 72.0f;
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXscale(AxisScale::log(2.0f));
    ax->setXlim(0.8f, 300.0f);
    ax->setYlim(0.0f, 20.0f);
}

void f192_symlog_scale(Figure& fig) {
    // mpl: ax.set_yscale("symlog", linthresh=2) — linear ±2, log beyond.
    auto* ax = mfAxes(fig);
    Series2D s;
    auto xs = linspace(-1.0f, 1.0f, 60);
    for (float x : xs)
        s.points.push_back({x, x * x * x * 120.0f});
    s.color = Color::fromRgba8(214, 39, 40);
    s.lineWidth = 1.5f * 200.0f / 72.0f;
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setYscale(AxisScale::symlog(2.0f, 1.0f));
    ax->setXlim(-1.0f, 1.0f);
    ax->setYlim(-130.0f, 130.0f);
}

void f193_sankey(Figure& fig) {
    // mpl: Sankey(ax=ax).add(flows, labels, orientations).finish().
    auto* ax = mfAxes(fig);
    ax->setXlim(-0.2f, 1.3f);
    ax->setYlim(-1.2f, 1.2f);
    Sankey sk(*ax);
    sk.patchLabels = {"Sys"};
    sk.add({1.0f, 0.5f, -0.8f, -0.7f}, {"in A", "in B", "out C", "out D"},
           {1, -1, 1, -1});
    auto diagrams = sk.finish();
    (void)diagrams;
}

void f194_colorbar_fmt_extend(Figure& fig) {
    // mpl: fig.colorbar(im, extend="both", format="%.1f", extendfrac=0.1).
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    auto& cb = ax->style().colorbar;
    cb.visible = true;
    cb.extend = "both";
    cb.extendfrac = 0.1f;
    cb.format = "%.1f";
    cb.ticks = {0, 6, 12};
}

void f195_cax_colorbar(Figure& fig) {
    // mpl: cax, kw = make_axes(ax); fig.colorbar(im, cax=cax) — the strip
    // fills the cax rect with no axes chrome.
    auto* ax = mfAxes(fig);
    ax->addPlot(std::make_unique<HeatmapPlot>(smallGrid()));
    auto* cax = fig.addAxesFraction(0.88f, 0.15f, 0.035f, 0.7f);
    cax->setStyle(mfStyle());
    auto& cb = cax->style().colorbar;
    cb.visible = true;
    cb.caxMode = true;
    cb.cmapPtr = &Colormap::byName("viridis");
    cb.explicitRange = Range{0.0f, 13.0f};
    cb.ticks = {0, 4, 8, 12};
}

// ═══ Tier 24 — projections / legend / spines / mlab / markers ═══════

void f196_polar_theta_grid(Figure& fig) {
    // mpl: ax = fig.add_subplot(projection='polar'); spiral + 8
    // thetagrids (degree labels).
    auto* ax = mfAxes(fig);
    ax->setProjection("polar");
    Series2D s;
    for (int i = 0; i <= 200; ++i) {
        float th = float(i) * 4.0f * float(M_PI) / 200.0f;
        float r = 0.15f + 0.85f * th / (4.0f * float(M_PI));
        s.points.push_back({th, r});
    }
    s.color = Color::fromRgba8(31, 119, 180);
    s.lineWidth = 1.5f * 200.0f / 72.0f;
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setRmax(1.0f);
    ax->setThetagrids({0, 45, 90, 135, 180, 225, 270, 315});
}

void f197_mollweide_geo(Figure& fig) {
    // mpl: ax = fig.add_subplot(projection='mollweide'); ax.grid(True)
    // + scatter lon/lat points (radian data space).
    auto* ax = mfAxes(fig);
    ax->setProjection("mollweide");
    ax->style().xAxis.grid = true;
    ax->style().yAxis.grid = true;
    Series2D s;
    for (int i = 0; i < 12; ++i) {
        float lon = -2.8f + float(i) * 0.5f;
        float lat = std::sin(float(i) * 1.3f) * 1.2f;
        s.points.push_back({lon, lat});
    }
    s.color = Color::fromRgba8(214, 39, 40);
    s.marker = MarkerStyle::Circle;
    s.size = 12.0f;   // mpl scatter s=20pt² → ~4.5pt diameter
    ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
}

void f198_spine_positions(Figure& fig) {
    // mpl: ax.spines['bottom'].set_position(('data', 0));
    //      ax.spines['left'].set_position(('axes', 0.5));
    //      ax.spines['top'/'right'].set_visible(False)
    auto* ax = mfAxes(fig);
    Series2D s;
    for (int i = 0; i <= 100; ++i) {
        float x = -1.5f + float(i) * 0.03f;
        s.points.push_back({x, std::sin(x * 3.0f) * 0.8f});
    }
    s.color = Color::fromRgba8(44, 160, 44);
    s.lineWidth = 1.5f * 200.0f / 72.0f;
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setXlim(-1.5f, 1.5f);
    ax->setYlim(-1.0f, 1.0f);
    auto& bottom = ax->spine("bottom");
    bottom.posMode = Axes::SpineSpec::PosMode::Data;
    bottom.posAmount = 0.0f;
    bottom.positionSet = true;
    auto& left = ax->spine("left");
    left.posMode = Axes::SpineSpec::PosMode::Axes;
    left.posAmount = 0.5f;
    left.positionSet = true;
    ax->setSpineVisible("top", false);
    ax->setSpineVisible("right", false);
    ax->touch();
}

void f199_legend_numpoints(Figure& fig) {
    // mpl: ax.plot(marker='o', label='line'); ax.scatter(label='pts');
    //      ax.fill_between(label='area'); ax.legend(numpoints=2).
    auto* ax = mfAxes(fig);
    Series2D l;
    for (int i = 0; i <= 8; ++i)
        l.points.push_back({float(i), std::sin(float(i) * 0.7f)});
    l.color = Color::fromRgba8(31, 119, 180);
    l.lineWidth = 1.5f * 200.0f / 72.0f;
    l.marker = MarkerStyle::Circle;
    l.size = 16.7f;  // mpl markersize=6pt
    l.label = "line";
    ax->addPlot(std::make_unique<LinePlot>(std::move(l)));
    Series2D sc;
    for (int i = 0; i <= 8; ++i)
        sc.points.push_back({float(i), -0.5f + 0.2f * std::cos(float(i))});
    sc.color = Color::fromRgba8(214, 39, 40);
    sc.marker = MarkerStyle::Circle;
    sc.size = 11.1f;  // mpl scatter s=16pt² → 4pt diameter
    sc.label = "pts";
    ax->addPlot(std::make_unique<ScatterPlot>(std::move(sc)));
    auto& lg = ax->style().legend;
    lg.visible = true;
    lg.location = "lower right";
    lg.numpoints = 2;
    lg.scatterpoints = 3;
}

void f200_marker_styles(Figure& fig) {
    // mpl: scatter rows with different markers/fillstyles.
    auto* ax = mfAxes(fig);
    struct Row { MarkerStyle m; MarkerFill f; const char* tag; };
    Row rows[] = {
        {MarkerStyle::Circle,  MarkerFill::Full,  "o"},
        {MarkerStyle::Circle,  MarkerFill::Left,  "o left"},
        {MarkerStyle::Square,  MarkerFill::Full,  "s"},
        {MarkerStyle::Triangle, MarkerFill::None, "^ none"},
        {MarkerStyle::Star,    MarkerFill::Full,  "*"},
        {MarkerStyle::X,       MarkerFill::Full,  "x"},
        {MarkerStyle::Polygon, MarkerFill::Full,  "(5,0)"},
    };
    for (size_t r = 0; r < std::size(rows); ++r) {
        Series2D s;
        for (int i = 0; i < 5; ++i)
            s.points.push_back({float(i) * 0.5f,
                                float(r) * 0.5f});
        s.color = Color::fromRgba8(31, 119, 180);
        s.marker = rows[r].m;
        s.markerFill = rows[r].f;
        s.markerNumsides = 5;
        s.size = 19.6f;  // mpl scatter s=50pt² → ~7pt diameter
        ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    }
    ax->setXlim(-0.5f, 2.5f);
    ax->setYlim(-0.5f, 3.5f);
}

void f201_hatch_bars(Figure& fig) {
    // mpl: ax.bar([0,1,2], [3,7,5], hatch='//') — hatch overlay clipped
    // to the bar rects (mpl hatch.color=black default).
    auto* ax = mfAxes(fig);
    BarData d;
    d.heights = {3, 7, 5};
    d.colors = {Color::fromRgba8(31, 119, 180),
                Color::fromRgba8(31, 119, 180),
                Color::fromRgba8(31, 119, 180)};
    ax->addPlot(std::make_unique<BarPlot>(d));
    std::vector<std::vector<Point2D>> polys;
    for (size_t i = 0; i < d.heights.size(); ++i) {
        float x0 = float(i) - 0.4f, x1 = float(i) + 0.4f;
        polys.push_back({{x0, 0.0f}, {x1, 0.0f},
                         {x1, d.heights[i]}, {x0, d.heights[i]}});
    }
    auto coll = std::make_unique<PolyCollection>(std::move(polys));
    coll->faceColors = {Color::transparent()};
    // mpl: bar edgecolor='none' → hatch.color (black), lw=0 →
    // no edge stroke.
    coll->edgeColors = {Color::black()};
    coll->lineWidths = {0.0f};
    coll->hatch = "//";
    ax->addPlot(std::move(coll));
}

void f202_hatch_fill(Figure& fig) {
    // mpl: ax.fill_between(x, 0, y, hatch='x')
    auto* ax = mfAxes(fig);
    auto x = linspace(0, 10, 60);
    std::vector<float> y;
    for (float xi : x) y.push_back(std::sin(xi));
    ax->addPlot(std::make_unique<FillBetweenPlot>(
        x, y, std::vector<float>(x.size(), 0.0f),
        Color::fromRgba8(31, 119, 180, 128)));
    std::vector<Point2D> poly;
    poly.push_back({x.front(), 0.0f});
    for (size_t i = 0; i < x.size(); ++i)
        poly.push_back({x[i], y[i]});
    poly.push_back({x.back(), 0.0f});
    auto coll = std::make_unique<PolyCollection>(
        std::vector<std::vector<Point2D>>{std::move(poly)});
    coll->faceColors = {Color::transparent()};
    // mpl: hatch = edgecolor ('face' → the fill color incl. alpha).
    coll->edgeColors = {Color::fromRgba8(31, 119, 180, 128)};
    coll->lineWidths = {0.0f};
    coll->hatch = "x";
    ax->addPlot(std::move(coll));
    ax->setXlim(0, 10); ax->setYlim(-1.2f, 1.2f);
}

void f203_hatch_pie(Figure& fig) {
    // mpl: ax.pie([30,25,20,15,10], hatch=['//','x','-','|','+'])
    auto* ax = mfAxes(fig);
    PieData d;
    d.values = {30, 25, 20, 15, 10};
    ax->pie(d);
    // Per-wedge hatch overlays (mpl wedgeprops hatch patterns).
    static const char* hatches[] = {"//", "x", "-", "|", "+"};
    float total = 0.0f;
    for (float v : d.values) total += v;
    float theta = d.startAngle / 360.0f;  // turns
    for (size_t i = 0; i < d.values.size(); ++i) {
        float t0 = theta, t1 = theta + d.values[i] / total;
        theta = t1;
        std::vector<Point2D> w{d.center};
        for (int s = 0; s <= 60; ++s) {
            float a = 2.0f * float(M_PI) *
                      (t0 + (t1 - t0) * float(s) / 60.0f);
            w.push_back({d.center.x + d.radius * std::cos(a),
                         d.center.y + d.radius * std::sin(a)});
        }
        auto coll = std::make_unique<PolyCollection>(
            std::vector<std::vector<Point2D>>{std::move(w)});
        coll->faceColors = {Color::transparent()};
        // mpl: wedge edgecolor='none' → hatch.color (black).
        coll->edgeColors = {Color::black()};
        coll->lineWidths = {0.0f};
        coll->hatch = hatches[i];
        ax->addPlot(std::move(coll));
    }
}

void f204_patches_boxstyle(Figure& fig) {
    // mpl: FancyBboxPatch(boxstyle='round'), Wedge, FancyArrowPatch.
    auto* ax = mfAxes(fig);
    BoxStyleSpec bs;
    bs.kind = BoxStyleSpec::Kind::Round;
    bs.pad = 0.3f;
    auto& p1 = ax->addPatch(patch::FancyBboxPatch(1, 1, 4, 3, bs));
    p1.style.face = Color::fromRgba8(31, 119, 180);
    p1.style.edge = Color::black();
    auto& p2 = ax->addPatch(patch::Wedge({7.5f, 2.0f}, 1.4f, 30.0f, 300.0f));
    p2.style.face = Color::fromRgba8(255, 127, 14);
    p2.style.edge = Color::black();
    auto& p3 = ax->addPatch(
        patch::FancyArrowPatch({2.0f, 5.0f}, {8.0f, 6.5f}, 0.35f, 0.7f));
    p3.style.face = Color::fromRgba8(44, 160, 44);
    p3.style.edge = Color::black();
    ax->setXlim(0, 10); ax->setYlim(0, 8);
}

void f205_tight_layout(Figure& fig) {
    // mpl: two stacked subplots + suptitle + fig.tight_layout().
    auto* a = fig.subplot2grid({2, 1}, {0, 0});
    auto* b = fig.subplot2grid({2, 1}, {1, 0});
    a->setStyle(mfStyle());
    b->setStyle(mfStyle());
    Series2D s1, s2;
    auto x = linspace(0, 10, 60);
    for (float xi : x) {
        s1.points.push_back({xi, std::sin(xi)});
        s2.points.push_back({xi, std::cos(xi)});
    }
    a->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    b->addPlot(std::make_unique<LinePlot>(std::move(s2)));
    a->setXlim(0, 10); b->setXlim(0, 10);
    a->setTitle("Top");
    b->setTitle("Bottom");
    b->style().xAxis.label = "x";
    fig.setTitle("Figure suptitle");
    fig.setTightLayout(true);
}

// ═══ Tier 26 — parity batch 20 (206–210) ════════════════════════════════

void f206_contour_xy_ranges(Figure& fig) {
    // mpl: ax.contour(X, Y, Z) — X/Y coordinate ranges map the grid to
    // data space (x in [-3,3], y in [0,4]) rather than index space.
    auto* ax = mfAxes(fig);
    Grid2D g;
    g.width = 30; g.height = 30;
    g.xRange = {-3, 3}; g.yRange = {0, 4};
    g.values.resize(30 * 30);
    for (uint32_t j = 0; j < 30; ++j)
        for (uint32_t i = 0; i < 30; ++i) {
            float x = -3 + float(i) / 29 * 6;
            float y = float(j) / 29 * 4;
            g.values[j * 30 + i] =
                std::sin(x) * std::cos(y - 2.0f);
        }
    ContourConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.levels = {-0.6f, -0.2f, 0.2f, 0.6f};
    ax->addPlot(std::make_unique<ContourPlot>(std::move(g), cfg));
    ax->setXlim(-3, 3); ax->setYlim(0, 4);
}

void f207_quiver_meshgrid(Figure& fig) {
    // mpl: ax.quiver(x, y, U, V) — 1-D coord vectors + 2-D field
    // expanded meshgrid-style; radial outward field.
    auto* ax = mfAxes(fig);
    std::vector<float> x, y, u, v;
    for (int j = 0; j < 6; ++j)
        for (int i = 0; i < 8; ++i) {
            float px = float(i), py = float(j);
            x.push_back(px); y.push_back(py);
            u.push_back(px - 3.5f); v.push_back(py - 2.5f);
        }
    ax->addPlot(std::make_unique<QuiverPlot>(std::move(x), std::move(y),
                                             std::move(u), std::move(v)));
    ax->setXlim(-1, 8); ax->setYlim(-1, 6);
}

void f208_barbs(Figure& fig) {
    // mpl: ax.barbs(X, Y, U, V) — wind barbs on a grid.
    auto* ax = mfAxes(fig);
    std::vector<float> x, y, u, v;
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 7; ++i) {
            x.push_back(float(i)); y.push_back(float(j));
            u.push_back(5.0f + 10.0f * i);
            v.push_back(15.0f * float(j) - 30.0f);
        }
    ax->addPlot(std::make_unique<BarbsPlot>(std::move(x), std::move(y),
                                            std::move(u), std::move(v)));
    ax->setXlim(-1, 7); ax->setYlim(-1, 5);
}

void f209_table_scaled(Figure& fig) {
    // mpl: t = ax.table(...); t.scale(1.4, 1.6) — per-cell scale.
    auto* ax = mfAxes(fig);
    BarData bd;
    bd.heights = {3, 7, 5};
    ax->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    ax->setXlim(-0.5f, 2.5f); ax->setYlim(0, 9);
    auto& t = ax->table({{"A", "B", "C"}, {"3", "7", "5"}}, "bottom");
    t.scaleX = 1.4f;
    t.scaleY = 1.6f;
}

void f210_axline_slope(Figure& fig) {
    // mpl: ax.axline((2,1), slope=0.5) + ax.axline((6,4),(8,7)).
    auto* ax = mfAxes(fig);
    ax->axline({2.0f, 1.0f}, 0.5f,
               Color::fromRgba8(31, 119, 180), 2.0f);
    ax->axline({6.0f, 4.0f}, {8.0f, 7.0f},
               Color::fromRgba8(255, 127, 14), 2.0f);
    ax->setXlim(0, 10); ax->setYlim(0, 8);
}

// ═══ Registry ═══════════════════════════════════════════════════════════

struct Feature {
    const char* name;
    void (*fn)(Figure&);
};

const Feature kFeatures[] = {
    {"001_blank_canvas",       f001_blank_canvas},
    {"002_fig_facecolor",      f002_fig_facecolor},
    {"003_axes_facecolor",     f003_axes_facecolor},
    {"004_spines_box",         f004_spines_box},
    {"005_spines_left_bottom", f005_spines_left_bottom},
    {"006_spines_none",        f006_spines_none},
    {"007_axes_inset_rect",    f007_axes_inset_rect},
    {"008_suptitle",           f008_suptitle},
    {"009_ticks_major_x",      f009_ticks_major_x},
    {"010_ticks_major_y",      f010_ticks_major_y},
    {"011_ticks_none",         f011_ticks_none},
    {"012_ticks_minor",        f012_ticks_minor},
    {"013_ticks_dir_in",       f013_ticks_dir_in},
    {"014_ticks_dir_inout",    f014_ticks_dir_inout},
    {"015_ticks_long",         f015_ticks_long},
    {"016_ticks_wide",         f016_ticks_wide},
    {"017_ticks_top_right",    f017_ticks_top_right},
    {"018_ticklabels_default", f018_ticklabels_default},
    {"019_ticklabels_fixed",   f019_ticklabels_fixed},
    {"020_ticklabels_format",  f020_ticklabels_format},
    {"021_ticklabels_sci",     f021_ticklabels_sci},
    {"022_ticklabels_rotation",f022_ticklabels_rotation},
    {"023_ticklabels_hidden",  f023_ticklabels_hidden},
    {"024_minor_ticklabels",   f024_minor_ticklabels},
    {"025_tick_nbins",         f025_tick_nbins},
    {"026_grid_major",         f026_grid_major},
    {"027_grid_x_only",        f027_grid_x_only},
    {"028_grid_y_only",        f028_grid_y_only},
    {"029_grid_minor",         f029_grid_minor},
    {"030_grid_both",          f030_grid_both},
    {"031_grid_dashed",        f031_grid_dashed},
    {"032_grid_color",         f032_grid_color},
    {"033_grid_linewidth",     f033_grid_linewidth},
    {"034_grid_axisbelow",     f034_grid_axisbelow},
    {"035_xlabel",             f035_xlabel},
    {"036_ylabel",             f036_ylabel},
    {"037_label_fontsize",     f037_label_fontsize},
    {"038_title",              f038_title},
    {"039_title_color",        f039_title_color},
    {"040_title_bold",         f040_title_bold},
    {"041_title_pad",          f041_title_pad},
    {"042_label_color",        f042_label_color},
    {"043_line_solid",         f043_line_solid},
    {"044_line_color",         f044_line_color},
    {"045_line_width",         f045_line_width},
    {"046_line_dashed",        f046_line_dashed},
    {"047_line_dotted",        f047_line_dotted},
    {"048_line_dashdot",       f048_line_dashdot},
    {"049_line_dashes_custom", f049_line_dashes_custom},
    {"050_line_alpha",         f050_line_alpha},
    {"051_line_zorder",        f051_line_zorder},
    {"052_line_prop_cycle",    f052_line_prop_cycle},
    {"053_marker_circle",      f053_marker_circle},
    {"054_marker_square",      f054_marker_square},
    {"055_marker_diamond",     f055_marker_diamond},
    {"056_marker_triangle",    f056_marker_triangle},
    {"057_marker_star",        f057_marker_star},
    {"058_marker_plus",        f058_marker_plus},
    {"059_marker_size",        f059_marker_size},
    {"060_marker_hollow",      f060_marker_hollow},
    {"061_marker_edge_color",  f061_marker_edge_color},
    {"062_marker_with_line",   f062_marker_with_line},
    {"063_marker_path",        f063_marker_path},
    {"064_marker_tex",         f064_marker_tex},
    {"065_fill_polygon",       f065_fill_polygon},
    {"066_fill_between",       f066_fill_between},
    {"067_fill_alpha",         f067_fill_alpha},
    {"068_axhline",            f068_axhline},
    {"069_axvline",            f069_axvline},
    {"070_hlines",             f070_hlines},
    {"071_vlines",             f071_vlines},
    {"072_axhspan",            f072_axhspan},
    {"073_axvspan",            f073_axvspan},
    {"074_hatch_rect",         f074_hatch_rect},
    {"075_xlim_ylim",          f075_xlim_ylim},
    {"076_invert_x",           f076_invert_x},
    {"077_invert_y",           f077_invert_y},
    {"078_log_y",              f078_log_y},
    {"079_log_x",              f079_log_x},
    {"080_loglog",             f080_loglog},
    {"081_symlog",             f081_symlog},
    {"082_aspect_equal",       f082_aspect_equal},
    {"083_categorical_x",      f083_categorical_x},
    {"084_date_x",             f084_date_x},
    {"085_secondary_y",        f085_secondary_y},
    {"086_legend_basic",       f086_legend_basic},
    {"087_legend_loc",         f087_legend_loc},
    {"088_legend_ncols",       f088_legend_ncols},
    {"089_legend_noframe",     f089_legend_noframe},
    {"090_legend_title",       f090_legend_title},
    {"091_legend_outside",     f091_legend_outside},
    {"092_legend_shadow",      f092_legend_shadow},
    {"093_colorbar_basic",     f093_colorbar_basic},
    {"094_colorbar_extend",    f094_colorbar_extend},
    {"095_colorbar_colormap",  f095_colorbar_colormap},
    {"096_colorbar_width",     f096_colorbar_width},
    {"097_text_data",          f097_text_data},
    {"098_text_axes",          f098_text_axes},
    {"099_text_rotation",      f099_text_rotation},
    {"100_text_halign",        f100_text_halign},
    {"101_text_bbox",          f101_text_bbox},
    {"102_mathtext",           f102_mathtext},
    {"103_annotate_arrow",     f103_annotate_arrow},
    {"104_annotate_offset",    f104_annotate_offset},
    {"105_unicode_text",       f105_unicode_text},
    {"106_subplots_2x2",       f106_subplots_2x2},
    {"107_subplots_sharex",    f107_subplots_sharex},
    {"108_twinx",              f108_twinx},
    {"109_inset_axes",         f109_inset_axes},
    {"110_mosaic",             f110_mosaic},
    {"111_errorbar_caps",      f111_errorbar_caps},
    {"112_bar_edges",          f112_bar_edges},
    {"113_step_post",          f113_step_post},
    {"114_eventplot_rows",     f114_eventplot_rows},
    {"115_table_bottom",       f115_table_bottom},
    {"116_arrowstyle_filled",  f116_arrowstyle_filled},
    {"117_arrowstyle_double",  f117_arrowstyle_double},
    {"118_arrowstyle_fancy",   f118_arrowstyle_fancy},
    {"119_clip_path",          f119_clip_path},
    {"120_boxstyle",           f120_boxstyle},
    {"121_arrow_bezier",       f121_arrow_bezier},
    {"122_colorbar_horizontal",f122_colorbar_horizontal},
    {"123_colorbar_shrink",    f123_colorbar_shrink},
    {"124_marker_tex_beta",    f124_marker_tex_beta},
    {"125_imshow_extent",      f125_imshow_extent},
    {"126_imshow_aspect_auto", f126_imshow_aspect_auto},
    {"127_inset_indicator",    f127_inset_indicator},
    {"128_sizebar",            f128_sizebar},
    {"129_anchored_text",      f129_anchored_text},
    {"130_scatter3d_depthshade",f130_scatter3d_depthshade},
    {"131_scatter3d_view_init",f131_scatter3d_view_init},
    {"132_surface_shade",      f132_surface_shade},
    {"133_surface_noshade",    f133_surface_noshade},
    {"134_quiver_pivot_mid",   f134_quiver_pivot_mid},
    {"135_quiver_headwidth",   f135_quiver_headwidth},
    {"136_streamplot_arrowsize",f136_streamplot_arrowsize},
    {"137_streamplot_nan_hole",f137_streamplot_nan_hole},
    {"138_clabel_gap",         f138_clabel_gap},
    {"139_log_clip",           f139_log_clip},
    {"140_colorblind_cycle",   f140_colorblind_cycle},
    {"141_legend_handlelength",f141_legend_handlelength},
    {"142_legend_labelcolor",  f142_legend_labelcolor},
    {"143_pie_explode",        f143_pie_explode},
    {"144_secondary_x",        f144_secondary_x},
    {"145_mathtext_frac_sum",  f145_mathtext_frac_sum},
    {"146_locator_params",     f146_locator_params},
    {"147_set_xbound",         f147_set_xbound},
    {"148_markevery_int",      f148_markevery_int},
    {"149_pie_startangle",     f149_pie_startangle},
    {"150_pie_autopct",        f150_pie_autopct},
    {"151_fill_between_where", f151_fill_between_where},
    {"152_stackplot_wiggle",   f152_stackplot_wiggle},
    {"153_label_outer",        f153_label_outer},
    {"154_axis_off",           f154_axis_off},
    {"155_imshow_origin_lower",f155_imshow_origin_lower},
    {"156_marker_colors",      f156_marker_colors},
    {"157_spine_center",       f157_spine_center},
    {"158_spine_outward_bounds",f158_spine_outward_bounds},
    {"159_quiverkey",          f159_quiverkey},
    {"160_xkcd_sketch",        f160_xkcd_sketch},
    {"161_clip_off",           f161_clip_off},
    {"162_sticky_edges_bar",   f162_sticky_edges_bar},
    {"163_stairs_fill",        f163_stairs_fill},
    {"164_pcolor",             f164_pcolor},
    {"165_inset_zoom",         f165_inset_zoom},
    {"166_patheffects_stroke", f166_patheffects_stroke},
    {"167_patheffects_shadow", f167_patheffects_shadow},
    {"168_ticks_both",         f168_ticks_both},
    {"169_transform_transaxes", f169_transform_transaxes},
    {"170_colorbar_ticks",     f170_colorbar_ticks},
    {"171_scatter_c",          f171_scatter_c},
    {"172_scatter_s",          f172_scatter_s},
    {"173_text_boxstyle",      f173_text_boxstyle},
    {"174_legend_anchor",      f174_legend_anchor},
    {"175_imshow_norm",        f175_imshow_norm},
    {"176_font_family",        f176_font_family},
    {"177_tick_labelsize",     f177_tick_labelsize},
    {"178_fig_text",           f178_fig_text},
    {"179_imshow_rgb",         f179_imshow_rgb},
    {"180_annotate_fontsize",  f180_annotate_fontsize},
    {"181_gridspec_ratios",    f181_gridspec_ratios},
    {"182_gridspec_nested",    f182_gridspec_nested},
    {"183_artist_props",       f183_artist_props},
    {"184_collection_props",   f184_collection_props},
    {"185_return_handles",     f185_return_handles},
    {"186_tri_explicit",       f186_tri_explicit},
    {"187_named_containers",   f187_named_containers},
    {"188_date_locators",      f188_date_locators},
    {"189_anchored_artists",   f189_anchored_artists},
    {"190_concise_dates",      f190_concise_dates},
    {"191_log_base2",          f191_log_base2},
    {"192_symlog_scale",       f192_symlog_scale},
    {"193_sankey",             f193_sankey},
    {"194_colorbar_fmt_extend",f194_colorbar_fmt_extend},
    {"195_cax_colorbar",       f195_cax_colorbar},
    {"196_polar_theta_grid",   f196_polar_theta_grid},
    {"197_mollweide_geo",      f197_mollweide_geo},
    {"198_spine_positions",    f198_spine_positions},
    {"199_legend_numpoints",   f199_legend_numpoints},
    {"200_marker_styles",      f200_marker_styles},
    {"201_hatch_bars",         f201_hatch_bars},
    {"202_hatch_fill",         f202_hatch_fill},
    {"203_hatch_pie",          f203_hatch_pie},
    {"204_patches_boxstyle",   f204_patches_boxstyle},
    {"205_tight_layout",       f205_tight_layout},
    {"206_contour_xy_ranges",  f206_contour_xy_ranges},
    {"207_quiver_meshgrid",    f207_quiver_meshgrid},
    {"208_barbs",              f208_barbs},
    {"209_table_scaled",       f209_table_scaled},
    {"210_axline_slope",       f210_axline_slope},
};

} // namespace

int main(int argc, char** argv) {
    std::string outDir = "gallery_micro/volcano";
    std::vector<std::string> only;
    int shardK = 0, shardN = 1;
    bool list = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--list") {
            list = true;
        } else if (a.starts_with("--only=")) {
            std::string csv{a.substr(7)};
            size_t pos = 0;
            while (pos <= csv.size()) {
                auto comma = csv.find(',', pos);
                only.push_back(csv.substr(
                    pos, comma == std::string::npos ? comma
                                                  : comma - pos));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (a.starts_with("--shard=")) {
            if (std::sscanf(a.data() + 8, "%d/%d", &shardK, &shardN) != 2 ||
                shardN < 1) {
                std::cerr << "bad --shard=K/N\n";
                return 2;
            }
        } else if (!a.starts_with("--")) {
            outDir = a;
        }
    }

    // --list doesn't need Vulkan.
    if (list) {
        for (auto& f : kFeatures) std::cout << f.name << "\n";
        return 0;
    }

    auto selected = [&](const Feature& f) {
        if (!only.empty()) {
            for (auto& o : only)
                if (std::string_view(f.name).find(o) != std::string_view::npos)
                    return true;
            return false;
        }
        // Shard by registry index.
        size_t idx = size_t(&f - kFeatures);
        return int(idx % size_t(shardN)) == shardK;
    };

    std::cout << "Generating VolcanoPlot microfeatures in " << outDir << "/"
              << (shardN > 1 ? " (shard " + std::to_string(shardK) + "/" +
                               std::to_string(shardN) + ")" : "")
              << "\n";
    MicroCtx ctx(outDir);
    int count = 0;
    for (auto& f : kFeatures) {
        if (!selected(f)) continue;
        Figure fig(1, 1);
        // 800x600 canvas == 4x3in at 200 dpi (matches the mpl side).
        fig.style().dpi = 200.0f;
        f.fn(fig);
        ctx.render(fig, f.name);
        ++count;
    }
    std::cout << "Done. Generated " << count << " microfeatures.\n";
    return 0;
}
