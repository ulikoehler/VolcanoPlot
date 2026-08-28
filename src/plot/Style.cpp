// volcano/plot/Style.cpp — matplotlib-style presets
#include "volcano/plot/Style.hpp"

namespace volcano::plot {

namespace styles {

FigureStyle defaultStyle() {
    FigureStyle s;
    s.faceColor = Color::white();
    s.xAxis.color = Color::black();
    s.yAxis.color = Color::black();
    s.xAxis.gridColor = Color::fromRgba8(200, 200, 200);
    s.yAxis.gridColor = Color::fromRgba8(200, 200, 200);
    return s;
}

FigureStyle ggplotStyle() {
    FigureStyle s;
    s.styleName = "ggplot";
    s.faceColor = Color::fromRgba8(238, 238, 238); // light gray
    s.xAxis.color = Color::black();
    s.yAxis.color = Color::black();
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.xAxis.gridColor = Color::white();
    s.yAxis.gridColor = Color::white();
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    s.title.font.size = 14.0f;
    s.title.font.weight = "bold";
    return s;
}

FigureStyle seabornStyle() {
    FigureStyle s;
    s.styleName = "seaborn";
    s.faceColor = Color::fromRgba8(247, 247, 247);
    s.xAxis.gridColor = Color::white();
    s.yAxis.gridColor = Color::white();
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    return s;
}

FigureStyle darkBackground() {
    FigureStyle s;
    s.styleName = "dark_background";
    s.faceColor = Color::black();
    s.title.color = Color::white();
    s.xAxis.color = Color::white();
    s.yAxis.color = Color::white();
    s.xAxis.gridColor = Color::fromRgba8(80, 80, 80);
    s.yAxis.gridColor = Color::fromRgba8(80, 80, 80);
    s.legend.faceColor = Color::fromRgba8(40, 40, 40, 200);
    s.legend.edgeColor = Color::white();
    return s;
}

FigureStyle grayscale() {
    FigureStyle s = defaultStyle();
    s.styleName = "grayscale";
    s.xAxis.gridColor = Color::fromRgba8(160, 160, 160);
    s.yAxis.gridColor = Color::fromRgba8(160, 160, 160);
    return s;
}

} // namespace styles

} // namespace volcano::plot
