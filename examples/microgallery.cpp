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
#include <volcano/encode/ImageEncoder.hpp>

#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/FillPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>

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

constexpr uint32_t kWidth = 400;
constexpr uint32_t kHeight = 300;

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

/// One axes with matplotlib-default-like style (white bg, no grid, ticks on).
Axes* mfAxes(Figure& fig) {
    auto* ax = fig.addAxes();
    ax->setStyle(styles::defaultStyle());
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
    ax->setStyle(styles::defaultStyle());
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
    t->fontSize = 1.6f;
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

// ═══ Tier 11 — multi-axes layout ════════════════════════════════════════

void f106_subplots_2x2(Figure& fig) {
    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 2; ++c) {
            auto* ax = fig.subplot2grid({2, 2}, {r, c});
            ax->setStyle(styles::defaultStyle());
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
    a->setStyle(styles::defaultStyle());
    b->setStyle(styles::defaultStyle());
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
    inset->setStyle(styles::defaultStyle());
    Series2D s = sineSeries();
    s.color = Color::fromRgba8(214, 39, 40);
    inset->addPlot(std::make_unique<LinePlot>(std::move(s)));
    inset->setXlim(4, 6); inset->setYlim(-1.1f, 1.1f);
}

void f110_mosaic(Figure& fig) {
    auto axes = fig.subplotMosaic({{"A", "B"}, {"C", "C"}});
    for (auto& [name, ax] : axes) {
        ax->setStyle(styles::defaultStyle());
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
        f.fn(fig);
        ctx.render(fig, f.name);
        ++count;
    }
    std::cout << "Done. Generated " << count << " microfeatures.\n";
    return 0;
}
