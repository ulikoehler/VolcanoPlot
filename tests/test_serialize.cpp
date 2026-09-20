// tests/test_serialize.cpp — Figure ↔ JSON round-trip
#include "PlotTestHarness.hpp"

#include <volcano/plot/Serialize.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>

#include <gtest/gtest.h>
#include <filesystem>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

std::unique_ptr<Figure> sampleFigure() {
    auto fig = std::make_unique<Figure>(2, 1);
    auto* ax = fig->addAxes(0, 0);
    ax->setXlim(0, 10);
    ax->setYlim(0, 10);
    ax->style().xAxis.label = "X axis";
    ax->style().yAxis.label = "Y axis";
    ax->setTitle("Title");
    ax->setXscale("linear");
    ax->legend();
    ax->style().yAxis.grid = true;

    Series2D line;
    line.points = {{0, 0}, {5, 5}, {10, 2}};
    line.label = "line";
    line.color = Color::fromRgba8(255, 0, 0, 255);
    line.lineWidth = 2.5f;
    line.lineStyle = LineStyle::Dashed;
    ax->addPlot(std::make_unique<LinePlot>(std::move(line)));

    auto* ax2 = fig->addAxes(1, 0);
    ax2->setXlim(0, 3);
    ax2->setYlim(0, 8);
    Series2D sc;
    sc.points = {{0, 1}, {1, 3}, {2, 7}};
    sc.marker = MarkerStyle::Diamond;
    sc.size = 10.0f;
    sc.color = Color::blue();
    ax2->addPlot(std::make_unique<ScatterPlot>(std::move(sc)));
    BarData bd;
    bd.heights = {2, 4, 6};
    bd.width = 0.5f;
    ax2->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    ErrorbarConfig ec;
    ec.yerr = {0.5f, 0.5f, 0.5f};
    ec.label = "err";
    ax2->addPlot(std::make_unique<ErrorbarPlot>(
        std::vector<float>{0, 1, 2}, std::vector<float>{1, 3, 7},
        std::move(ec)));
    return fig;
}

} // namespace

TEST(Serialize, RoundTripStructure) {
    auto fig = sampleFigure();
    auto json = figureToJson(*fig);
    auto fig2 = figureFromJson(json);
    ASSERT_TRUE(fig2);
    ASSERT_EQ(fig2->placements().size(), 2u);

    auto* ax = fig2->placements()[0].axes.get();
    EXPECT_EQ(ax->xlim().min, 0.0f);
    EXPECT_EQ(ax->xlim().max, 10.0f);
    EXPECT_EQ(ax->style().xAxis.label, "X axis");
    EXPECT_EQ(ax->style().title.text, "Title");
    EXPECT_TRUE(ax->style().legend.visible);
    EXPECT_TRUE(ax->style().yAxis.grid);
    ASSERT_EQ(ax->plots().size(), 1u);
    auto* lp = dynamic_cast<LinePlot*>(ax->plots()[0].get());
    ASSERT_TRUE(lp);
    EXPECT_EQ(lp->series().points.size(), 3u);
    EXPECT_EQ(lp->series().label, "line");
    EXPECT_EQ(lp->series().lineStyle, LineStyle::Dashed);
    EXPECT_FLOAT_EQ(lp->series().lineWidth, 2.5f);

    auto* ax2 = fig2->placements()[1].axes.get();
    ASSERT_EQ(ax2->plots().size(), 3u);
    EXPECT_TRUE(dynamic_cast<ScatterPlot*>(ax2->plots()[0].get()));
    EXPECT_TRUE(dynamic_cast<BarPlot*>(ax2->plots()[1].get()));
    EXPECT_TRUE(dynamic_cast<ErrorbarPlot*>(ax2->plots()[2].get()));
}

TEST(Serialize, EscapesAndSpecials) {
    auto fig = std::make_unique<Figure>(1, 1);
    auto* ax = fig->addAxes();
    ax->setTitle("a \"quote\"\nnewline");
    Series2D s;
    s.points = {{0.5f, 1.5f}};
    s.markerTex = "\\alpha";
    ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    auto fig2 = figureFromJson(figureToJson(*fig));
    ASSERT_TRUE(fig2);
    auto* ax2 = fig2->placements()[0].axes.get();
    EXPECT_EQ(ax2->style().title.text, "a \"quote\"\nnewline");
    auto* sp = dynamic_cast<ScatterPlot*>(ax2->plots()[0].get());
    ASSERT_TRUE(sp);
    EXPECT_EQ(sp->series().markerTex, "\\alpha");
}

TEST(Serialize, UnknownTypesSkipped) {
    std::string_view j = R"({"version":1,"grid":{"rows":1,"cols":1},
        "axes":[{"row":0,"col":0,"plots":[
            {"type":"wat","foo":1},
            {"type":"line","points":[[0,0],[1,1]]}
        ]}]})";
    auto fig = figureFromJson(j);
    ASSERT_TRUE(fig);
    ASSERT_EQ(fig->placements().size(), 1u);
    EXPECT_EQ(fig->placements()[0].axes->plots().size(), 1u);
}

TEST(Serialize, MalformedReturnsNull) {
    EXPECT_FALSE(figureFromJson("{oops"));
    EXPECT_FALSE(figureFromJson("[1,2]"));
    EXPECT_FALSE(figureFromJson(""));
}

TEST(Serialize, FileRoundTripAndIdenticalRender) {
    auto path = std::filesystem::temp_directory_path() / "volcano_fig.json";
    Image img1, img2, img3;
    // Harness (Vulkan device) must outlive any rendered figure — plot
    // renderers release GPU buffers in their destructors.
    {
        PlotTestHarness h1(256, 256);
        auto fig = sampleFigure();
        ASSERT_TRUE(saveFigureJson(*fig, path));
        img1 = h1.render(*fig);
        img3 = h1.render(*fig);   // determinism baseline
    }
    {
        PlotTestHarness h2(256, 256);
        auto fig2 = loadFigureJson(path);
        ASSERT_TRUE(fig2);
        img2 = h2.render(*fig2);
    }
    ASSERT_EQ(img1.width(), img2.width());
    auto countDiff = [](const Image& a, const Image& b) {
        size_t d = 0;
        for (uint32_t y = 0; y < a.height(); ++y)
            for (uint32_t x = 0; x < a.width(); ++x) {
                auto p = a.get(x, y), q = b.get(x, y);
                if (p.r != q.r || p.g != q.g || p.b != q.b) ++d;
            }
        return d;
    };
    EXPECT_EQ(countDiff(img1, img3), 0u) << "baseline re-render unstable";
    if (countDiff(img1, img2) != 0) {
        img1.save("/tmp/ser_orig.png");
        img2.save("/tmp/ser_loaded.png");
    }
    EXPECT_EQ(countDiff(img1, img2), 0u)
        << "round-tripped figure should render identically";
    std::filesystem::remove(path);
}
